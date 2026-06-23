#include "init.h"
#include "process.h"
#include "scheduler.h"
#include "elf.h"
#include "usermode.h"
#include "../fs/vfs.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../lib/printk.h"
#include "../../lib/string.h"

extern void fork_child_stub(void);   /* arch/x86_64/boot.asm */

/*
 * proc_spawn_init
 *
 * Flow:
 *   1. Open /sbin/init (fallback: /bin/init)
 *   2. Read ELF into kernel heap
 *   3. proc_create() — new PCB with fresh pml4 + kstack
 *   4. Map user stack into init's pml4
 *   5. elf_load() → init's pml4
 *   6. Temporarily switch CR3 to init's pml4, build user stack
 *      (argc=1, argv=["init"], envp), switch CR3 back
 *   7. Inherit fd 0/1/2 (/dev/tty0) from kernel process (PID 0)
 *   8. Build arch_switch frame on init's kstack pointing to
 *      fork_child_stub (reuses stage-2 mechanism)
 *   9. sched_add() — the scheduler launches it on the next tick
 */
int
proc_spawn_init(void)
{
    /* ── 1. find init binary ─────────────────────────────────── */
    static const char *paths[] = { "/sbin/init", "/bin/init", NULL };
    file_t      *f         = NULL;
    const char  *init_path = NULL;

    for (int i = 0; paths[i]; i++) {
        f = vfs_open(paths[i], O_RDONLY);
        if (f) { init_path = paths[i]; break; }
    }
    if (!f) {
        printk(KERN_ERR "init: no binary found "
                        "(put ELF at /sbin/init or /bin/init)\n");
        return -1;
    }

    /* ── 2. read ELF ─────────────────────────────────────────── */
    size_t   sz  = (size_t)f->inode->size;
    uint8_t *buf = kmalloc(sz);
    if (!buf) { vfs_close(f); return -ENOMEM; }
    vfs_read(f, buf, sz);
    vfs_close(f);

    /* ── 3. allocate process ──────────────────────────────────── */
    process_t *p = proc_create();
    if (!p) { kfree(buf); return -ENOMEM; }

    /* ── 4. map user stack into init's pml4 ──────────────────── */
    usermode_map_stack(p->pml4);

    /* ── 5. load ELF ─────────────────────────────────────────── */
    elf_info_t info = elf_load(p->pml4, buf, sz);
    kfree(buf);
    if (!info.valid) {
        p->state = PROC_UNUSED;
        printk(KERN_ERR "init: invalid ELF at %s\n", init_path);
        return -1;
    }

    p->brk_start = (info.load_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    p->brk       = p->brk_start;
    kstrcpy(p->cwd, "/");

    /* ── 6. build user stack inside init's address space ─────────
     *
     * We must write to the user stack VAs (near USER_STACK_TOP).
     * Those VAs are only mapped in init's pml4, so we temporarily
     * switch CR3.  The kernel (boot stack + heap) is identity-mapped
     * in every pml4 (entries 256-511), so kernel code keeps running
     * correctly after the switch.
     */
    process_t *kp = proc_find(0);          /* kernel process (PID 0) */
    vmm_switch(p->pml4);                   /* → init's address space */

    char *argv[] = { (char *)init_path, NULL };
    char *envp[] = {
        "PATH=/bin:/sbin:/usr/bin",
        "HOME=/root",
        "TERM=vt100",
        NULL
    };

    uint64_t user_rsp = build_user_stack(USER_STACK_TOP,
                                          1,    argv,
                                          3,    envp);

    vmm_switch(kp->pml4);                 /* ← back to kernel's pml4 */

    /* ── 7. inherit stdin / stdout / stderr ─────────────────── */
    for (int i = 0; i < 3; i++) {
        p->fds[i] = kp->fds[i];
        if (p->fds[i].file)
            p->fds[i].file->refcount++;
    }

    /* ── 8. build arch_switch frame on init's kstack ────────────
     *
     * Layout consumed by arch_switch restore sequence:
     *   popfq / pop r15 / pop r14 / pop r13 / pop r12 / pop rbp / pop rbx / ret
     *
     *   r12 = entry point  → fork_child_stub: mov rcx, r12  (user RIP)
     *   r13 = user RSP     → fork_child_stub: mov rsp, r13
     *   r14 = user RFLAGS  → fork_child_stub: mov r11, r14
     *
     * fork_child_stub then executes sysretq → ring 3.
     */
    uint64_t *sp = (uint64_t *)(p->kstack + KSTACK_SIZE);

    *--sp = (uint64_t)fork_child_stub;  /* ret  */
    *--sp = 0;                           /* rbx  */
    *--sp = 0;                           /* rbp  */
    *--sp = info.entry;                  /* r12 = entry point     */
    *--sp = user_rsp;                    /* r13 = user RSP        */
    *--sp = 0x202ULL;                    /* r14 = RFLAGS (IF=1)   */
    *--sp = 0;                           /* r15  */
    *--sp = 0x202ULL;                    /* rflags for popfq      */

    p->rsp = (uint64_t)sp;

    /* ── 9. add to scheduler ─────────────────────────────────── */
    sched_add(p);

    printk(KERN_INFO "init: PID %d queued from %s "
                     "(entry=0x%x rsp=0x%x)\n",
           p->pid, init_path, info.entry, user_rsp);
    return 0;
}
