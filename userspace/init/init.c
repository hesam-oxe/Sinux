/*
 * Sinux init — PID 1, runs in ring 3
 *
 * The first user-space process.  It receives control from
 * fork_child_stub (via sysretq) with a proper SysV AMD64 initial
 * stack (argc / argv / envp).
 *
 * Responsibilities:
 *   • Prove ring-3 isolation: all I/O goes through syscalls
 *   • Spawn /bin/sinush as the system shell
 *   • Reap it with wait4() and respawn it if it ever exits
 *     (classic init behaviour — the machine always has a shell)
 *
 * Build: make -C userspace/init   (needs ../libc/libc.a first)
 * Install: /sbin/init on the Sinux disk image; at boot the kernel
 * copies it into the root ramfs, then proc_spawn_init() runs it.
 */

#include <stdio.h>
#include <unistd.h>
#include <syscall.h>

static char *shell_argv[] = { "sinush", NULL };
static char *shell_envp[] = {
    "PATH=/bin:/sbin",
    "HOME=/root",
    "TERM=vt100",
    NULL
};

int
main(int argc, char *argv[], char *envp[])
{
    (void)argc; (void)argv; (void)envp;

    printf("\n*** Sinux init (PID %d, ring 3) ***\n",
           (long long)getpid());

    for (;;) {
        printf("init: launching /bin/sinush ...\n");

        pid_t child = fork();
        if (child < 0) {
            printf("init: fork failed, retrying...\n");
            sleep(1);
            continue;
        }

        if (child == 0) {
            execve("/bin/sinush", shell_argv, shell_envp);
            printf("init: cannot exec /bin/sinush\n");
            _exit(1);
        }

        int   status = 0;
        pid_t w      = wait4(child, &status, 0);
        printf("init: shell (PID %d) exited, respawning...\n",
               (long long)w);
        sleep(1);
    }

    return 0;
}
