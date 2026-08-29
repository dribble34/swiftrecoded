; Indirect syscall trampoline for anti_debug.
;
; Instead of writing our own `syscall; ret` bytes into an unbacked RX page,
; we jump into an existing `syscall; ret` gadget inside the real, loaded
; ntdll.dll. RIP during the transition lives in ntdll's .text (backed,
; signed by Microsoft), which is what an EDR/AC scanner will see when it
; walks thread state. Only setup lives in our own DLL's .text, which is
; also backed by a proper section.
;
; The C initialiser resolves the gadget address once (scan ntdll for the
; three-byte pattern 0f 05 c3) and stores it in anti_debug_g_syscall_gadget.

.code

EXTRN anti_debug_g_syscall_gadget:QWORD

; NTSTATUS anti_debug_do_syscall(
;     DWORD  ssn,
;     void*  arg0,
;     void*  arg1,
;     void*  arg2,
;     void*  arg3,
;     void*  arg4 )
;
; Six-arg wrapper is enough for every NT function this module uses
; (NtQueryInformationProcess: 5 args, NtSetInformationThread: 4 args).
;
; Incoming Win64 ABI:
;   rcx = ssn
;   rdx = arg0
;   r8  = arg1
;   r9  = arg2
;   [rsp+28h] = arg3
;   [rsp+30h] = arg4
;
; Syscall ABI expects:
;   eax = ssn
;   r10 = arg0     (kernel clobbers rcx; r10 is the preserved copy)
;   rdx = arg1
;   r8  = arg2
;   r9  = arg3
;   [rsp+28h] = arg4
;
; So we shift args left by one and stash the ssn in eax.
anti_debug_do_syscall PROC
    mov eax, ecx                          ; eax = ssn
    mov r10, rdx                          ; r10 = arg0
    mov rdx, r8                           ; rdx = arg1
    mov r8,  r9                           ; r8  = arg2
    mov r9,  qword ptr [rsp+28h]          ; r9  = arg3
    mov r11, qword ptr [rsp+30h]          ; r11 = arg4 (temp)
    mov qword ptr [rsp+28h], r11          ; store arg4 at syscall's expected slot
    jmp qword ptr [anti_debug_g_syscall_gadget]
anti_debug_do_syscall ENDP

END
