; Preserve the complete x64 register argument ABI, including private exports.
; The resolver runs on the calling thread; DLL attachment only starts the worker.
EXTERN sc_resolve_system_export:PROC
EXTERN __imp_GetLastError:QWORD
EXTERN __imp_SetLastError:QWORD
.code
FORWARD MACRO name, index
PUBLIC name
name PROC FRAME
    sub rsp, 0A8h
    .allocstack 0A8h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    movdqu [rsp+60h], xmm2
    movdqu [rsp+70h], xmm3
    call qword ptr [__imp_GetLastError]
    mov [rsp+80h], eax
    mov ecx, index
    call sc_resolve_system_export
    mov [rsp+88h], rax
    mov ecx, [rsp+80h]
    call qword ptr [__imp_SetLastError]
    mov rax, [rsp+88h]
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    mov r8, [rsp+30h]
    mov r9, [rsp+38h]
    movdqu xmm0, [rsp+40h]
    movdqu xmm1, [rsp+50h]
    movdqu xmm2, [rsp+60h]
    movdqu xmm3, [rsp+70h]
    add rsp, 0A8h
    jmp rax
name ENDP
ENDM
FORWARD sc_forward_0, 0
FORWARD sc_forward_1, 1
FORWARD sc_forward_2, 2
FORWARD sc_forward_3, 3
FORWARD sc_forward_4, 4
END
