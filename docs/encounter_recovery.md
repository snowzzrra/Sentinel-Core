# Encounter recovery

`ap_finish_encounter` takes no arguments. Its console callback queues the captured
map stamp; `manual_tick` admits and executes the request on the game thread.
Admission requires a live player in an admitted mission, a stable map generation,
no menu or alternate controlled entity, and active managers with living AI members.
Manager membership selects actors; position is irrelevant.

Recovery stops maintenance and staggered producers, closes the managers' Core
recipe requests, and applies the vanilla `killAI` sequence to their AI. Each frame
repeats this operation for those managers, including later waves. The script runs
its waits and completion effects. Recovery reads its completion latch and stops
after all selected managers complete, a lifecycle change, or 60 seconds. An external
event-flag wait with no live AI ends the attempt as unknown after a two-second grace
period. Diagnostics name the manager and wait type. Repeating the command reports
the same attempt. History retains 16 attempts; progress reports run every five seconds.

## Native evidence

Addresses refer to the Windows retail image with base `0x140000000`. New call
targets have 32-byte prologue checks; validation failure disables this command.
Calls require the game thread and a current map stamp, checked between native calls.

| Address | Qualified operation / signature |
| --- | --- |
| `0x1407bc2d0` | Registers `killAI` with callback `0x1407b37c0`. |
| `0x1407b37c0` | Vanilla callback, `void(const NativeArgs*)`; iterates AI, not suitable for an ownership-scoped call. |
| `0x1411f8700` | `void(dispatcher, player, victim, player, damage, damage, uint8_t)`; native kill notification. Dispatcher is `world+0x126648`. |
| `0x14120e730` | `void(compiler, inflictor, attacker, victim, int group, int attack, decl, weaponTag, float amount)`; forwards to death dispatch `0x14120e790` with listener flag zero. Compiler is `world+0xac660`. |
| `0x140682380` | `int64_t*(clock, int64_t* output, int kind)`; obtains the native step used by `killAI`. |
| `0x1417fae60` | `int32_t*(int32_t* output, int64_t nativeTicks)`; converts the removal delay with the engine's time scale. |
| `0x1417dd390` | `uintptr_t(entity, event, int32_t delay, uint32_t argc, ...)`; posts removal event `0x146bebf80` with zero arguments. Vanilla wrapper `0x1417dd470` supplies zero argc. |
| `0x140ba3070` | `uintptr_t(manager, output, uint64_t mask, const char* label)`; stops maintenance workers. |

The vanilla callback uses `damageSuicide`, obtained from the current challenge
entity's global data (`+0x35f8`, then `+0xc0`). It passes `decl+0x90` to the kill
notification, then uses attack type 5, group -1, `*(decl+0xad8)`, `decl+0x9c8`, and
damage amount zero for death analytics. It posts removal after twice the native
step. Recovery uses the same sequence with its owned victim list.
Vanilla `0x140719500` resolves the actor's native owner reference at `+0x167d0`.
Recovery defers owner-bound actors while draining the other members, allowing
their hosts to finish their normal death/removal path.

Decompiler excerpts:

```cpp
// 0x1407bc2d0
AddCommand(system, "killAI", FUN_1407b37c0, "kills all AI in the level", 0, 0);
// 0x1407b37c0
FUN_1411f8700(world->dispatcher, player, ai, player, decl + 0x90, decl + 0x90, 0);
FUN_14120e730(world + 0xac660, player, player, ai, -1, 5,
              *(decl + 0xad8), decl + 0x9c8, 0.0f);
FUN_1417dd470(ai, 0x146bebf80, milliseconds(2 * nativeStep));
// 0x1421735c0, event dispatcher
case 0x3dc: FUN_140ba3070(manager, output, mask, label); break;
```

Event registration `0x1402b74b0` associates `stopMaintainingAICount` with `0x3dc`.
`0x140ba3070` filters maintenance workers by type/label, removes each from the
manager's `+0xcc0/+0xcc8` list, and invokes its native destructor. Recovery supplies
SPAWN_ANY (`1<<44`) and an empty label. Staggered tick `0x140bbea50` returns on
`worker+0xb9`; successful exhaustion decrements `+0xa8` to zero and sets `+0xb9`.
Recovery sets those two completion fields and leaves wait objects intact.

Manager activation `0x140bac170` requires a nonnegative script index (`+0xda0`)
and enabled state (`+0xba0`), then sets `+0xdb2`. The script's normal update
`0x140bbcc80` advances its timeline, runs encounter-end effects, sets `+0xdb3`,
and reports completion. Recovery reads these fields and never calls that update
or writes the completion latch. AI lists are at `+0xc80/+0xcf0/+0xd08` with
`0x68`-byte records. World manager records are `0x20` bytes at
`world+0x126658/+0x126660`.

`idPlayer::GetControl` (`0x14143c000`) reads the alternate-control stack at
`+0x8898/+0x88a0`. `ResetControl` (`0x141439220`) treats the player itself as
null and clears that stack for normal control. Recovery requires an empty stack.

Suppression marks the current generation and actor registration before native
notification. `Model::publish` retains validation, deduplication, and emitted/dead
bookkeeping while skipping the killsanity counter. Native notifications still run.
Closing recipe requests preserves that state in the existing checkpoint encoding.

## Refusals

Whole maps: `e1m4_boss`, `e2m4_boss`, `e3m4_boss`, `e5m4_boss`.

Additional map/manager pairs:

| Map | Manager |
| --- | --- |
| `e3m3_maykr` | `stage2_encounter_manager` |
| `e4m2_swamp` | `boss_encounter_manager_1` |
| `e4m3_mcity` | `platformarea_samuel_encounter_manager_dlc1_1` |

Protected actor instance names: `boss_intro_ai_superheavy_doomhunter_stage1_1`,
`confront_samuel_ai`, `beast_of_the_arena_ai_boss_gladiator_1`,
`game_boss_ai_boss_gladiator_1`, `khan_boss_intro_ida2`,
`intro_icon_stage_1_idai2`, `platformarea_samuel_ai_boss_samuel_cin`,
`stage_1_ai_boss_darklord_reveals_ai`, `stage_1_ai_boss_facing_ai`.
An actor whose RTTI type contains `Boss` or `boss` also protects its manager.

The actor names and boss managers match the content safety restrictions and the
retail encounter catalog. The Blood Swamps manager is refused conservatively:
its catalog entries use boss spawn groups. Its forest-arena manager is separate
and is eligible.

Event-flag predicate `0x140ba8970` reads kind `+0xa8` and latch `+0xac`.
Kind 3 completes from an empty AI population and exhausted producers; recovery
treats it as combat. Other unset event flags are external waits and diagnostics
include their numeric kind.

## In-game verification

- Run the command in an ordinary arena and during a later wave. Check doors,
  music, relays, location-check events, and checkpoint triggers.
- Confirm unrelated managers' AI survive and killsanity does not increase.
- Repeat during the drain; verify a single attempt and final completion.
- Try hub, menu, loading, traversal, death, and each boss refusal.
- Reload a checkpoint during recovery; verify cancellation and closed producers.
- Exercise a player-trigger wait; verify an unknown outcome naming the wait.

Offline harnesses prove selection, queue state, producer/request closure and death
bookkeeping. Retail script timing, removal scheduling, checkpoint timing and the
resulting doors/music/AP events require an in-game run.
