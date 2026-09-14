; ──────────────────────────────────────────────────────────────────────
; AP startup trampoline for Sinux.
;
; The BSP copies this section to physical 0x7000 and sends a STARTUP IPI
; with vector 0x07, so the AP begins execution at _trampoline_start in
; real mode. The stub walks real mode → 32-bit protected mode → 64-bit
; long mode and finally jumps to the C entry point (ap_entry) with the
; logical CPU id in RDI.
;
; Before sending the SIPI, the BSP patches the four parameter slots below
; (they live inside this section, i.e. below 1 MiB where the AP can read
; them): see smp_bringup_aps() in cpu.c.
; ──────────────────────────────────────────────────────────────────────

[section .trampoline]
[bits 16]

global _trampoline_start
global _trampoline_end
global _trampoline_pml4_slot
global _trampoline_stack_slot
global _trampoline_entry_slot
global _trampoline_cpu_id_slot

_trampoline_start:
    jmp short entry16
    nop

; ── parameter slots, patched by the BSP before the SIPI ──
align 8
_trampoline_pml4_slot:    dq 0    ; CR3 for the AP (kernel PML4)
_trampoline_stack_slot:   dq 0    ; RSP for the AP
_trampoline_entry_slot:   dq 0    ; 64-bit C entry point
_trampoline_cpu_id_slot:  dq 0    ; logical CPU id → RDI

entry16:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6F00            ; scratch stack below the trampoline page

    ; Fast A20 gate (QEMU keeps A20 enabled; harmless either way)
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al

    lgdt [gdtdesc16]

    ; Enter 32-bit protected mode
    mov eax, cr0
    or  eax, 1                 ; PE
    mov cr0, eax
    jmp 0x08:entry32

[bits 32]
entry32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x6F00

    ; PAE + PGE
    mov eax, cr4
    or  eax, 0x20 | 0x80
    mov cr4, eax

    ; CR3 = kernel PML4 (it identity-maps this trampoline page)
    mov eax, [_trampoline_pml4_slot]
    mov cr3, eax

    ; EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 0x100
    wrmsr

    ; Enable paging → IA-32e mode
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    jmp 0x18:entry64

[bits 64]
entry64:
    mov ax, 0x20               ; trampoline GDT data selector
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, [_trampoline_stack_slot]
    mov rdi, [_trampoline_cpu_id_slot]
    mov rax, [_trampoline_entry_slot]
    jmp rax

; ── trampoline GDT ──
align 8
gdt16:
    dq 0                       ; 0x00 null
    dq 0x00CF9A000000FFFF      ; 0x08 32-bit code
    dq 0x00CF92000000FFFF      ; 0x10 32-bit data
    dq 0x00AF9B000000FFFF      ; 0x18 64-bit code
    dq 0x00CF92000000FFFF      ; 0x20 data
gdt16_end:

gdtdesc16:
    dw gdt16_end - gdt16 - 1
    dd gdt16

align 16
_trampoline_end:
