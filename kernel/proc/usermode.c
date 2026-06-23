#include "usermode.h"
#include "process.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"
#include "../../arch/x86_64/gdt.h"
#include "../../lib/printk.h"

extern void _enter_usermode(uint64_t entry, uint64_t rsp);

void
usermode_map_stack(uint64_t *pml4)
{
    uint64_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;

    for (uint64_t va = stack_bottom; va < USER_STACK_TOP; va += PAGE_SIZE) {
        void *page = pmm_alloc();
        if (!page) return;
        kmemset(page, 0, PAGE_SIZE);
        vmm_map(pml4, va,
                (uint64_t)page,
                VMM_PRESENT | VMM_WRITABLE | VMM_USER);
    }
}

/*
 * build_user_stack — lay out the SysV AMD64 initial stack
 *
 * Stack layout at the returned RSP (grows downward from stack_top):
 *
 *   high │ envp strings (null-terminated)       │
 *        │ argv strings (null-terminated)        │
 *        │ padding (16-byte align)               │
 *        │ NULL          ← end of envp[]         │
 *        │ envp[envc-1]                          │
 *        │ …                                     │
 *        │ envp[0]                               │
 *        │ NULL          ← end of argv[]         │
 *        │ argv[argc-1]                          │
 *        │ …                                     │
 *        │ argv[0]                               │
 *   RSP → argc (uint64_t)                        │
 *
 * _start pops argc, then reads argv[] and envp[] from the stack.
 */
#define EXEC_MAX_ARGS 64

uint64_t
build_user_stack(uint64_t stack_top,
                 int argc, char **argv,
                 int envc, char **envp)
{
    char *uarg[EXEC_MAX_ARGS];   /* user-VAs for argv strings */
    char *uenv[EXEC_MAX_ARGS];   /* user-VAs for envp strings */

    uint8_t *sp = (uint8_t *)stack_top;

    /* ── copy strings onto stack (high → low address) ───── */
    for (int i = envc - 1; i >= 0; i--) {
        size_t len = kstrlen(envp[i]) + 1;
        sp -= len;
        kmemcpy(sp, envp[i], len);
        uenv[i] = (char *)sp;
    }
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = kstrlen(argv[i]) + 1;
        sp -= len;
        kmemcpy(sp, argv[i], len);
        uarg[i] = (char *)sp;
    }

    /* ── 16-byte align ───────────────────────────────────── */
    sp = (uint8_t *)((uint64_t)sp & ~15ULL);

    /* ── envp pointer array (NULL-terminated) ────────────── */
    sp -= 8; *(uint64_t *)sp = 0;
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 8; *(uint64_t *)sp = (uint64_t)uenv[i];
    }

    /* ── argv pointer array (NULL-terminated) ────────────── */
    sp -= 8; *(uint64_t *)sp = 0;
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8; *(uint64_t *)sp = (uint64_t)uarg[i];
    }

    /* ── argc ────────────────────────────────────────────── */
    sp -= 8; *(uint64_t *)sp = (uint64_t)argc;

    return (uint64_t)sp;   /* RSP for _start */
}

void
usermode_exec(process_t *proc, uint64_t entry, uint64_t load_end,
              int argc, char **argv, int envc, char **envp)
{
    if (!proc->pml4) return;

    usermode_map_stack(proc->pml4);

    proc->brk_start = (load_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    proc->brk       = proc->brk_start;

    gdt_set_kernel_stack((uint64_t)(proc->kstack + KSTACK_SIZE));

    vmm_switch(proc->pml4);

    uint64_t user_rsp = build_user_stack(USER_STACK_TOP,
                                          argc, argv,
                                          envc, envp);

    printk(KERN_INFO "exec: entry=0x%x rsp=0x%x argc=%d\n",
           entry, user_rsp, argc);

    _enter_usermode(entry, user_rsp);
}
