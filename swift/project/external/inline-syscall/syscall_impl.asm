; MASM x64 assembly implementation of syscalls for MSVC
; Windows x64 calling convention: RCX, RDX, R8, R9, then stack
; Syscall convention: RAX (syscall #), R10, RDX, R8, R9, then stack

.CODE

; syscall_impl0(uint32_t id)
syscall_impl0 PROC
    mov eax, ecx        ; syscall number in EAX
    syscall
    ret
syscall_impl0 ENDP

; syscall_impl1(uint32_t id, void* a1)
syscall_impl1 PROC
    mov r10, rdx        ; first arg to R10
    mov eax, ecx        ; syscall number in EAX
    syscall
    ret
syscall_impl1 ENDP

; syscall_impl2(uint32_t id, void* a1, void* a2)
syscall_impl2 PROC
    mov r10, rdx        ; first arg to R10
    mov rdx, r8         ; second arg to RDX
    mov eax, ecx        ; syscall number in EAX
    syscall
    ret
syscall_impl2 ENDP

; syscall_impl3(uint32_t id, void* a1, void* a2, void* a3)
syscall_impl3 PROC
    mov r10, rdx        ; first arg to R10
    mov rdx, r8         ; second arg to RDX
    mov r8, r9          ; third arg to R8
    mov eax, ecx        ; syscall number in EAX
    syscall
    ret
syscall_impl3 ENDP

; syscall_impl4(uint32_t id, void* a1, void* a2, void* a3, void* a4)
syscall_impl4 PROC
    mov r10, rdx        ; first arg to R10
    mov rdx, r8         ; second arg to RDX
    mov r8, r9          ; third arg to R8
    mov r9, [rsp+28h]   ; fourth arg from stack to R9
    mov eax, ecx        ; syscall number in EAX
    syscall
    ret
syscall_impl4 ENDP

; syscall_impl5(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5)
syscall_impl5 PROC
    sub rsp, 48h        ; allocate shadow space + arg space
    mov r10, rdx        ; first arg to R10
    mov rdx, r8         ; second arg to RDX
    mov r8, r9          ; third arg to R8
    mov r9, [rsp+70h]   ; fourth arg from original stack
    mov rax, [rsp+78h]  ; fifth arg from original stack
    mov [rsp+28h], rax  ; store fifth arg on new stack
    mov eax, ecx        ; syscall number in EAX
    syscall
    add rsp, 48h        ; restore stack
    ret
syscall_impl5 ENDP

; syscall_impl6(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6)
syscall_impl6 PROC
    sub rsp, 48h        ; allocate shadow space + arg space
    mov r10, rdx        ; first arg to R10
    mov rdx, r8         ; second arg to RDX
    mov r8, r9          ; third arg to R8
    mov r9, [rsp+70h]   ; fourth arg from original stack
    mov rax, [rsp+78h]  ; fifth arg from original stack
    mov [rsp+28h], rax  ; store fifth arg on new stack
    mov rax, [rsp+80h]  ; sixth arg from original stack
    mov [rsp+30h], rax  ; store sixth arg on new stack
    mov eax, ecx        ; syscall number in EAX
    syscall
    add rsp, 48h        ; restore stack
    ret
syscall_impl6 ENDP

; syscall_impl7(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7)
syscall_impl7 PROC
    sub rsp, 48h        ; allocate shadow space + arg space
    mov r10, rdx        ; first arg to R10
    mov rdx, r8         ; second arg to RDX
    mov r8, r9          ; third arg to R8
    mov r9, [rsp+70h]   ; fourth arg from original stack
    mov rax, [rsp+78h]  ; fifth arg
    mov [rsp+28h], rax
    mov rax, [rsp+80h]  ; sixth arg
    mov [rsp+30h], rax
    mov rax, [rsp+88h]  ; seventh arg
    mov [rsp+38h], rax
    mov eax, ecx        ; syscall number in EAX
    syscall
    add rsp, 48h        ; restore stack
    ret
syscall_impl7 ENDP

; syscall_impl8(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8)
syscall_impl8 PROC
    sub rsp, 58h        ; allocate shadow space + arg space
    mov r10, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, [rsp+80h]
    mov rax, [rsp+88h]
    mov [rsp+28h], rax
    mov rax, [rsp+90h]
    mov [rsp+30h], rax
    mov rax, [rsp+98h]
    mov [rsp+38h], rax
    mov rax, [rsp+0A0h]
    mov [rsp+40h], rax
    mov eax, ecx
    syscall
    add rsp, 58h
    ret
syscall_impl8 ENDP

; syscall_impl9(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8, void* a9)
syscall_impl9 PROC
    sub rsp, 58h
    mov r10, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, [rsp+80h]
    mov rax, [rsp+88h]
    mov [rsp+28h], rax
    mov rax, [rsp+90h]
    mov [rsp+30h], rax
    mov rax, [rsp+98h]
    mov [rsp+38h], rax
    mov rax, [rsp+0A0h]
    mov [rsp+40h], rax
    mov rax, [rsp+0A8h]
    mov [rsp+48h], rax
    mov eax, ecx
    syscall
    add rsp, 58h
    ret
syscall_impl9 ENDP

; syscall_impl10(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8, void* a9, void* a10)
syscall_impl10 PROC
    sub rsp, 68h
    mov r10, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, [rsp+90h]
    mov rax, [rsp+98h]
    mov [rsp+28h], rax
    mov rax, [rsp+0A0h]
    mov [rsp+30h], rax
    mov rax, [rsp+0A8h]
    mov [rsp+38h], rax
    mov rax, [rsp+0B0h]
    mov [rsp+40h], rax
    mov rax, [rsp+0B8h]
    mov [rsp+48h], rax
    mov rax, [rsp+0C0h]
    mov [rsp+50h], rax
    mov eax, ecx
    syscall
    add rsp, 68h
    ret
syscall_impl10 ENDP

; syscall_impl11(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8, void* a9, void* a10, void* a11)
syscall_impl11 PROC
    sub rsp, 68h
    mov r10, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, [rsp+90h]
    mov rax, [rsp+98h]
    mov [rsp+28h], rax
    mov rax, [rsp+0A0h]
    mov [rsp+30h], rax
    mov rax, [rsp+0A8h]
    mov [rsp+38h], rax
    mov rax, [rsp+0B0h]
    mov [rsp+40h], rax
    mov rax, [rsp+0B8h]
    mov [rsp+48h], rax
    mov rax, [rsp+0C0h]
    mov [rsp+50h], rax
    mov rax, [rsp+0C8h]
    mov [rsp+58h], rax
    mov eax, ecx
    syscall
    add rsp, 68h
    ret
syscall_impl11 ENDP

; syscall_impl12(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8, void* a9, void* a10, void* a11, void* a12)
syscall_impl12 PROC
    sub rsp, 78h
    mov r10, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, [rsp+0A0h]
    mov rax, [rsp+0A8h]
    mov [rsp+28h], rax
    mov rax, [rsp+0B0h]
    mov [rsp+30h], rax
    mov rax, [rsp+0B8h]
    mov [rsp+38h], rax
    mov rax, [rsp+0C0h]
    mov [rsp+40h], rax
    mov rax, [rsp+0C8h]
    mov [rsp+48h], rax
    mov rax, [rsp+0D0h]
    mov [rsp+50h], rax
    mov rax, [rsp+0D8h]
    mov [rsp+58h], rax
    mov rax, [rsp+0E0h]
    mov [rsp+60h], rax
    mov eax, ecx
    syscall
    add rsp, 78h
    ret
syscall_impl12 ENDP

; syscall_impl13(uint32_t id, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8, void* a9, void* a10, void* a11, void* a12, void* a13)
syscall_impl13 PROC
    sub rsp, 78h
    mov r10, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, [rsp+0A0h]
    mov rax, [rsp+0A8h]
    mov [rsp+28h], rax
    mov rax, [rsp+0B0h]
    mov [rsp+30h], rax
    mov rax, [rsp+0B8h]
    mov [rsp+38h], rax
    mov rax, [rsp+0C0h]
    mov [rsp+40h], rax
    mov rax, [rsp+0C8h]
    mov [rsp+48h], rax
    mov rax, [rsp+0D0h]
    mov [rsp+50h], rax
    mov rax, [rsp+0D8h]
    mov [rsp+58h], rax
    mov rax, [rsp+0E0h]
    mov [rsp+60h], rax
    mov rax, [rsp+0E8h]
    mov [rsp+68h], rax
    mov eax, ecx
    syscall
    add rsp, 78h
    ret
syscall_impl13 ENDP

END
