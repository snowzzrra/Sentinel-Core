# Local inspection protocol, version 1

This is one factual query of an already initialized Sentinel Core. It is separate
from [C ABI 1](../include/sentinel_core.h). Both read the Core-owned snapshot;
the service has no second status owner. Compatibility uses wire version and
required inspection capability bits, not Core version-string equality.

## Endpoint and identity

Endpoint: `\\.\pipe\sentinel_core.inspect.v1.<decimal host PID>`. Sentinel never
opens or serves `\\.\pipe\meathook_interface_rpc`. Windows named pipes provide a
local, process-targeted mechanism without a network listener or service install.

The server creates one message-mode duplex instance with `FILE_FLAG_OVERLAPPED`,
`FILE_FLAG_FIRST_PIPE_INSTANCE` and `PIPE_REJECT_REMOTE_CLIENTS`. Its protected
DACL contains one allow ACE for the process token's **logon SID**, with
`FILE_GENERIC_READ | FILE_WRITE_DATA` (`0x0012008b`). There is no Everyone,
Administrators or default-permission fallback; missing logon identity fails service
startup. Read attributes allow Windows to query the pipe's actual server PID.
Generic write is deliberately excluded because it includes
`FILE_CREATE_PIPE_INSTANCE`. A client requests only the allowed mask and uses
`SECURITY_IDENTIFICATION` impersonation level. The test inspects the resulting
DACL and verifies that a generic-write open is denied.

The client opens the requested process with query-limited/synchronize rights,
retains its handle through the query, reads its creation time/image path through
Windows, and obtains `GetNamedPipeServerProcessId` from the connected pipe.
Requested PID, OS server PID, reply PID and creation time must agree; the retained
process must still be alive. A 128-bit `BCryptGenRandom` instance ID changes with
each successful inspection startup, including a Core restart within one process.
Treat `(PID, process_created, instance_id)` as identity. Never reuse a cached reply
after process exit. An exit immediately after a successful query is naturally
possible; this is a snapshot, not a continuing liveness guarantee.

The instance ID is not a secret or authentication token. The OS image-name label
`doom_executable_name_match` is not proof of an untampered game. A same-logon
process can deny service by occupying an endpoint; first-instance creation fails
and a querying client rejects its wrong OS PID. This is reliable local identity,
not protection from a malicious process with equivalent privileges.

## Encoding

Unsigned integers use explicit **little-endian** bytes. Each request or response
is one pipe message of at most **512 bytes**, with no raw structs, padding,
pointers, native enums or terminators on the wire. A connection serves one
request/reply; the client then closes it.

The 16-byte header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u32 | Magic `0x50494353`, bytes `SCIP` |
| 4 | u16 | Wire version, currently 1 |
| 6 | u16 | Operation, currently 1 (identify and read current status) |
| 8 | u32 | Payload byte length, exactly total message length minus 16 |
| 12 | u32 | Result; request must use 0 |

The request payload is exactly one **u64 required inspection capability mask**.
Bit 0 is identify/status; all other bits are currently unavailable. A zero mask
also permits the same query. Core ABI capabilities use a separate namespace:
bit 0 is in-process inspection and bit 1 is explicit lifecycle. Neither describes
gameplay support or grants remote lifecycle control.

Result codes: 0 success; 1 incompatible wire version; 2 required inspection
capability unavailable; 3 unsupported operation; 4 malformed request. Rejections
use a version-1/operation-1 header and no payload. No lifecycle or reconfiguration
operation exists. Wrong operation/version/capabilities do not change Core state.

Success payload offsets, relative to the end of the header:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32 | Core ABI version (1) |
| 4 | u64 | Core ABI capabilities (3) |
| 12 | u64 | Available wire inspection capabilities (1) |
| 20 | u32 | Actual host PID |
| 24 | u64 | Process creation time: Windows FILETIME ticks since 1601 UTC |
| 32 | 16 bytes | Opaque Core inspection instance ID |
| 48 | u32 | Core state: 0 cold, 1 ready, 2 stopped |
| 52 | u32 | Inspection service: 0 stopped, 1 listening, 2 stopping, 3 failed |
| 56 | u32 | Inspection service Win32 error (0 when healthy) |
| 60 | u32 | Engine integration: 0 unavailable |
| 64 | u32 | Gameplay safety: 0 unprobed |
| 68 | u32 | Game-build compatibility: 0 unprobed |
| 72 | u32 | Last Core lifecycle result, from the C ABI |
| 76 | u32 | Core initialization count |
| 80 | u16 + bytes | Core version length, then 1–31 printable ASCII bytes |
| variable | u16 + bytes | Source/build identity length, then 1–64 printable ASCII bytes |

The current source/config identity is a 64-character SHA256 of CMake's enumerated
production inputs and forwarding configuration. It is not a DLL checksum, signed
provenance or game-build compatibility profile. JSON emits creation time as a
decimal **string** to preserve all 64 bits and instance ID as 32 hexadecimal digits.

Malformed/truncated input within the size bound receives result 4 where writable.
An oversized pipe message or disconnected peer is disconnected without a reply;
no request-sized allocation is made. The next client can still query. Replies
must have exact lengths and supported types; a truncated/invalid reply is CLI
exit 9. See [CLI exit codes](../README.md#3-build-and-use).

## State and lifecycle

Core owns serialized lifecycle operations and a separately locked snapshot.
Initialization runs outside DllMain, initializes Core and starts one inspection
worker. `SC_READY` alone does not mean service startup succeeded: a failed start
returns the additive `SC_INSPECTION_FAILURE` result through C ABI 1, retaining
the existing status layout. A functioning IPC response describes availability
at that instant. Engine integration and gameplay safety have independent fields.

The service admits one client at a time. Idle connection waits wake at most once
per second or immediately for shutdown. Each admitted connection has a **1000 ms
total request/reply/client-close deadline**. The probe has one **50–10000 ms**
connection/write/read budget (default 2000 ms); an absent endpoint returns
immediately. Busy endpoints use `WaitNamedPipe`, not a spin loop. There is no
worker-per-client allocation, unbounded queue or per-request log stream.

All pipe I/O uses overlapped operations. Deadline/shutdown cancels the owned
operation with `CancelIoEx` and drains its kernel completion before reusing the
buffer/event. Cancellation drain depends on the Windows pipe driver, not on a
client sending data; it is not an additional peer-response wait. After writing a
reply the server waits within the same deadline for client close, so disconnect
does not discard unread data. It never calls blocking `FlushFileBuffers`.

Explicit shutdown stops admission, signals cancellation and joins the worker
outside the snapshot lock, with a **5000 ms join limit**. Only successful join
closes owned service handles. Failure retains the module and handles; the caller
must retry shutdown and must not unload. The bootstrap likewise marks itself
stopped only when Core shutdown succeeds. Lifecycle callers remain responsible
for quiescing other in-process ABI users before unloading.

DllMain does no inspection startup, I/O draining or detach joins. Normal process
termination relies on OS reclamation. These rules preserve bootstrap behavior;
they do not solve unloading future engine hooks. AP networking, engine memory,
commands, save access and gameplay mutations are outside this protocol.

## Validation scope

`live_inspection_contract` launches separate non-game hosts and separate CLI
processes. It covers simultaneous PID endpoints, OS PID/creation agreement,
identity across reconnect/relaunch, CLI output/exit codes, DACL restrictions,
malformed/oversized/version/capability requests, absent/busy/stalled endpoints,
impostor PID/creation replies, disconnect recovery and shutdown with a held client.
The seven existing ABI/lifecycle/forwarder/import tests remain in CTest.

These are harness results. Remote and alternate-logon denial are enforced through
Windows flags/ACLs; no remote machine or second interactive account was used for
an empirical cross-session test. DOOM validation follows the
[manual Windows smoke procedure](windows-smoke.md).
