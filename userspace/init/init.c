/*
 * Sinux init — PID 1, runs in ring 3
 *
 * This is the first user-space process.  It receives control from
 * fork_child_stub (via sysretq) with a proper SysV AMD64 initial
 * stack (argc / argv / envp).
 *
 * Responsibilities at this stage:
 *   • Prove ring-3 isolation: all I/O goes through syscalls
 *   • Provide a simple interactive shell for development
 *   • Reap zombie children with wait4()
 *
 * Build:
 *   x86_64-elf-gcc -ffreestanding -nostdlib -static \
 *       -o /sbin/init init.c ../libc/src/syscall_wrap.S \
 *       ../libc/src/start.S ../libc/src/string.c
 *
 * Then place the resulting ELF in VFS at /sbin/init.
 */

#include "../libc/include/syscall.h"
#include "../libc/include/types.h"

/* ── minimal libc ──────────────────────────────────────────────── */
static int64_t
xwrite(int fd, const char *s, uint64_t n)
{
    return syscall(SYS_WRITE, (uint64_t)fd, (uint64_t)s, n);
}

static int64_t
xread(int fd, char *buf, uint64_t n)
{
    return syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, n);
}

static void
puts_fd(int fd, const char *s)
{
    uint64_t len = 0;
    while (s[len]) len++;
    xwrite(fd, s, len);
}

static int
strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static uint64_t
strlen(const char *s)
{
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ── simple shell ──────────────────────────────────────────────── */
#define STDIN  0
#define STDOUT 1
#define STDERR 2

#define MAX_ARGS 16
#define BUF_SIZE 256

static int
parse(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) { *p = '\0'; p++; }
    }
    argv[argc] = (char *)0;
    return argc;
}

static void
run_command(int argc, char **argv)
{
    if (argc == 0) return;

    /* built-ins */
    if (!strcmp(argv[0], "exit")) {
        puts_fd(STDOUT, "init: bye\n");
        syscall(SYS_EXIT, 0, 0, 0);
    }

    if (!strcmp(argv[0], "help")) {
        puts_fd(STDOUT,
            "  exit           — exit init\n"
            "  getpid         — print PID\n"
            "  help           — this list\n"
            "  (any path)     — fork + execve that binary\n");
        return;
    }

    if (!strcmp(argv[0], "getpid")) {
        int64_t pid = syscall(SYS_GETPID, 0, 0, 0);
        char buf[32];
        /* itoa */
        int i = 30; buf[31] = '\0';
        if (pid == 0) { buf[i--] = '0'; } else {
            int64_t v = pid;
            while (v > 0) { buf[i--] = '0' + (int)(v % 10); v /= 10; }
        }
        puts_fd(STDOUT, "PID ");
        puts_fd(STDOUT, buf + i + 1);
        puts_fd(STDOUT, "\n");
        return;
    }

    /* fork + execve */
    char *envp[] = { "PATH=/bin:/sbin", "HOME=/root", (char *)0 };

    int64_t child = syscall(SYS_FORK, 0, 0, 0);
    if (child < 0) {
        puts_fd(STDERR, "init: fork failed\n");
        return;
    }
    if (child == 0) {
        /* child: exec the command */
        syscall(SYS_EXECVE, (uint64_t)argv[0],
                             (uint64_t)argv,
                             (uint64_t)envp);
        puts_fd(STDERR, "init: exec failed: ");
        puts_fd(STDERR, argv[0]);
        puts_fd(STDERR, "\n");
        syscall(SYS_EXIT, 1, 0, 0);
    }

    /* parent: wait for child */
    int status = 0;
    syscall(SYS_WAIT4, (uint64_t)child, (uint64_t)&status, 0);
}

int
main(int argc, char **argv, char **envp)
{
    (void)envp;

    puts_fd(STDOUT, "\n*** Sinux init (PID 1, ring 3) ***\n");
    puts_fd(STDOUT, "Type 'help' for commands.\n\n");

    char line[BUF_SIZE];
    char *args[MAX_ARGS];

    for (;;) {
        /* reap any zombie children */
        while (syscall(SYS_WAIT4, (uint64_t)-1,
                       (uint64_t)0, 1 /* WNOHANG */) > 0)
            ;

        puts_fd(STDOUT, "init# ");

        int64_t n = xread(STDIN, line, BUF_SIZE - 1);
        if (n <= 0) continue;

        /* strip newline */
        if (n > 0 && line[n - 1] == '\n') n--;
        line[n] = '\0';

        int nargs = parse(line, args, MAX_ARGS);
        run_command(nargs, args);
    }

    return 0;
}
