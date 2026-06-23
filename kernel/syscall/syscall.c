#include "syscall.h"
#include "../ipc/pipe.h"
#include "../ipc/signal.h"
#include "../proc/elf.h"
#include "../proc/usermode.h"
#include "../../mm/vmm.h"
#include "../fs/vfs.h"
#include "../proc/process.h"
#include "../proc/scheduler.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"
#include "../../lib/printk.h"
#include "../../arch/x86_64/pit.h"
#include <stdint.h>

extern void syscall_arch_init(void);

static int64_t
sys_write(int fd, const void *buf, size_t n)
{
    process_t *p = proc_current();
    if (fd < 0 || fd >= MAX_FDS) return -EBADF;
    if (!p->fds[fd].file)        return -EBADF;
    return vfs_write(p->fds[fd].file, buf, n);
}

static int64_t
sys_read(int fd, void *buf, size_t n)
{
    process_t *p = proc_current();
    if (fd < 0 || fd >= MAX_FDS) return -EBADF;
    if (!p->fds[fd].file)        return -EBADF;
    return vfs_read(p->fds[fd].file, buf, n);
}

static int64_t
sys_open(const char *path, int flags, int mode)
{
    (void)mode;
    process_t *p = proc_current();
    file_t    *f = vfs_open(path, flags);
    if (!f) return -ENOENT;
    int fd = proc_alloc_fd(p, f);
    if (fd < 0) { vfs_close(f); return -EMFILE; }
    return fd;
}

static int64_t
sys_close(int fd)
{
    process_t *p = proc_current();
    if (fd < 0 || fd >= MAX_FDS || !p->fds[fd].file) return -EBADF;
    vfs_close(p->fds[fd].file);
    proc_free_fd(p, fd);
    return 0;
}

static int64_t sys_getpid(void)  { return proc_current()->pid;  }
static int64_t sys_getppid(void) { return proc_current()->ppid; }

static int64_t
sys_brk(uint64_t addr)
{
    process_t *p = proc_current();
    if (addr == 0) return (int64_t)p->brk;
    if (addr < p->brk_start) return -EINVAL;
    p->brk = addr;
    return (int64_t)addr;
}

static int64_t
sys_exit(int code)
{
    proc_exit(proc_current(), code);
    sched_yield();
    for(;;) __asm__ volatile("hlt");
    return 0;
}

/* Defined in boot.asm — written at every SYSCALL entry */
extern uint64_t user_ctx_rip;
extern uint64_t user_ctx_rflags;
extern uint64_t user_ctx_rsp;

/* Defined in boot.asm — first thing a forked child runs */
extern void fork_child_stub(void);

static int64_t
sys_fork(void)
{
    process_t *parent = proc_current();

    /* ── 1. clone address space ──────────────────────────────────── */
    uint64_t *child_pml4 = vmm_clone_pml4(parent->pml4);
    if (!child_pml4) return -ENOMEM;

    /* ── 2. allocate child PCB ───────────────────────────────────── */
    process_t *child = proc_create();
    if (!child) { vmm_destroy_pml4(child_pml4); return -ENOMEM; }

    child->pml4  = child_pml4;
    child->ppid  = parent->pid;
    child->brk   = parent->brk;
    kstrcpy(child->cwd, parent->cwd);

    /* ── 3. inherit open file descriptors ────────────────────────── */
    for (int i = 0; i < MAX_FDS; i++) {
        child->fds[i] = parent->fds[i];
        if (child->fds[i].file)
            child->fds[i].file->refcount++;
    }

    /* ── 4. build arch_switch frame on child's kernel stack ─────────
     *
     * arch_switch restores (in pop order):
     *   popfq, pop r15, pop r14, pop r13, pop r12, pop rbp, pop rbx, ret
     *
     * We encode the user context in callee-saved regs so that
     * fork_child_stub (the ret target) can read them without
     * needing separate arguments:
     *
     *   r12 = user RIP    (where syscall returns to in user space)
     *   r13 = user RSP    (user stack before our SYSCALL pushes)
     *   r14 = user RFLAGS
     */
    uint64_t *sp = (uint64_t *)(child->kstack + KSTACK_SIZE);

    *--sp = (uint64_t)fork_child_stub; /* ret  → first thing child runs   */
    *--sp = 0;                          /* rbx                              */
    *--sp = 0;                          /* rbp                              */
    *--sp = user_ctx_rip;              /* r12  → user RIP                  */
    *--sp = user_ctx_rsp;              /* r13  → user RSP (pre-syscall)    */
    *--sp = user_ctx_rflags;           /* r14  → user RFLAGS               */
    *--sp = 0;                          /* r15                              */
    *--sp = 0x202ULL;                  /* rflags (IF=1, reserved bit 1)    */

    child->rsp = (uint64_t)sp;

    /* ── 5. schedule ─────────────────────────────────────────────── */
    sched_add(child);

    return child->pid;   /* parent gets child PID; child gets 0 via stub */
}

static int64_t
sys_wait4(pid_t pid, int *status, int options)
{
    (void)options;
    process_t *p = proc_current();
    while (1) {
        process_t *child = proc_find(pid);
        if (!child) return -EINVAL;
        if (child->state == PROC_ZOMBIE) {
            if (status) *status = child->exit_code;
            child->state = PROC_UNUSED;
            return pid;
        }
        p->state = PROC_WAITING;
        sched_yield();
    }
}

static int64_t
sys_nanosleep(uint64_t seconds, uint64_t nanoseconds)
{
    uint64_t ms = seconds * 1000 + nanoseconds / 1000000;
    sched_sleep(ms);
    return 0;
}

typedef int64_t (*syscall_fn_t)(uint64_t,uint64_t,uint64_t,
                                 uint64_t,uint64_t,uint64_t);

#define SYSCALL_ENTRY(fn) ((syscall_fn_t)(void*)(fn))

int64_t
syscall_entry(uint64_t nr,
              uint64_t a1, uint64_t a2, uint64_t a3,
              uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    switch (nr) {
    case SYS_READ:    return sys_read((int)a1, (void*)a2, (size_t)a3);
    case SYS_WRITE:   return sys_write((int)a1, (void*)a2, (size_t)a3);
    case SYS_OPEN:    return sys_open((char*)a1, (int)a2, (int)a3);
    case SYS_CLOSE:   return sys_close((int)a1);
    case SYS_BRK:     return sys_brk(a1);
    case SYS_GETPID:  return sys_getpid();
    case SYS_GETPPID: return sys_getppid();
    case SYS_FORK:    return sys_fork();
    case SYS_EXIT:    return sys_exit((int)a1);
    case SYS_WAIT4:   return sys_wait4((pid_t)a1,(int*)a2,(int)a3);
    case SYS_SLEEP:   return sys_nanosleep(a1, a2);
    case SYS_PIPE:    { int fds[2]; int r=pipe_create(fds);
                        if(!r){*(int*)a1=fds[0];*((int*)a1+1)=fds[1];} return r; }
    case SYS_KILL:    return (int64_t)signal_send((int)a1, (int)a2);
    case SYS_SIGACTION: {
        sigaction_t act = { .handler=(sighandler_t)a2, .mask=0, .flags=0 };
        return signal_set_handler((int)a1, &act);
    }
    case SYS_EXECVE: {
        const char  *path      = (const char *)a1;
        char       **user_argv = (char **)a2;
        char       **user_envp = (char **)a3;

        /* ── copy argv[] to kernel buffers ───────────────────────── */
        #define EXEC_MAX_ARGS 64
        int   argc = 0;
        char *kargv[EXEC_MAX_ARGS + 1];
        kmemset(kargv, 0, sizeof(kargv));

        if (user_argv) {
            while (argc < EXEC_MAX_ARGS && user_argv[argc]) {
                size_t len  = kstrlen(user_argv[argc]) + 1;
                kargv[argc] = kmalloc(len);
                if (!kargv[argc]) {
                    for (int j = 0; j < argc; j++) kfree(kargv[j]);
                    return -ENOMEM;
                }
                kmemcpy(kargv[argc], user_argv[argc], len);
                argc++;
            }
        }
        kargv[argc] = NULL;

        /* ── copy envp[] to kernel buffers ───────────────────────── */
        int   envc = 0;
        char *kenvp[EXEC_MAX_ARGS + 1];
        kmemset(kenvp, 0, sizeof(kenvp));

        if (user_envp) {
            while (envc < EXEC_MAX_ARGS && user_envp[envc]) {
                size_t len  = kstrlen(user_envp[envc]) + 1;
                kenvp[envc] = kmalloc(len);
                if (!kenvp[envc]) {
                    for (int j = 0; j < envc; j++) kfree(kenvp[j]);
                    for (int j = 0; j < argc; j++) kfree(kargv[j]);
                    return -ENOMEM;
                }
                kmemcpy(kenvp[envc], user_envp[envc], len);
                envc++;
            }
        }
        kenvp[envc] = NULL;

        /* ── read ELF from filesystem ────────────────────────────── */
        file_t *f = vfs_open(path, O_RDONLY);
        if (!f) {
            for (int j = 0; j < argc; j++) kfree(kargv[j]);
            for (int j = 0; j < envc; j++) kfree(kenvp[j]);
            return -ENOENT;
        }
        size_t   sz  = (size_t)f->inode->size;
        uint8_t *buf = kmalloc(sz);
        if (!buf) {
            vfs_close(f);
            for (int j = 0; j < argc; j++) kfree(kargv[j]);
            for (int j = 0; j < envc; j++) kfree(kenvp[j]);
            return -ENOMEM;
        }
        vfs_read(f, buf, sz);
        vfs_close(f);

        /* ── load ELF into a fresh address space ─────────────────── */
        uint64_t *new_pml4 = vmm_new_pml4();
        if (!new_pml4) {
            kfree(buf);
            for (int j = 0; j < argc; j++) kfree(kargv[j]);
            for (int j = 0; j < envc; j++) kfree(kenvp[j]);
            return -ENOMEM;
        }
        elf_info_t info = elf_load(new_pml4, buf, sz);
        kfree(buf);
        if (!info.valid) {
            vmm_destroy_pml4(new_pml4);
            for (int j = 0; j < argc; j++) kfree(kargv[j]);
            for (int j = 0; j < envc; j++) kfree(kenvp[j]);
            return -EINVAL;
        }

        /* ── replace process address space ───────────────────────── */
        process_t *p       = proc_current();
        uint64_t  *old_pml4 = p->pml4;
        p->pml4 = new_pml4;

        /*
         * Note: old_pml4 is freed AFTER usermode_exec builds the new
         * user stack (which needs the kernel stack to still be usable).
         * vmm_destroy_pml4 only frees user pages (entries 0-255) and
         * those physical pages are identity-mapped, so it's safe to
         * call even after switching CR3.
         */

        /* ── jump to user space — does not return ────────────────── */
        usermode_exec(p, info.entry, info.load_end,
                      argc, kargv, envc, kenvp);

        /* unreachable — kept for error-path clarity */
        vmm_destroy_pml4(old_pml4);
        for (int j = 0; j < argc; j++) kfree(kargv[j]);
        for (int j = 0; j < envc; j++) kfree(kenvp[j]);
        return 0;
    }
    default:
        printk(KERN_WARNING "unknown syscall %u\n", nr);
        return -EINVAL;
    }
}

void
syscall_init(void) {
    syscall_arch_init();
    printk(KERN_INFO "syscall: SYSCALL/SYSRET enabled\n");
}
