/*
 * Sinush — Sinux interactive shell (ring 3)
 *
 * The first program a Sinux user talks to.  It prints the `sinux>`
 * prompt, runs a short self-test transcript on startup (so the boot
 * log itself proves the shell parses and executes commands), then
 * serves interactive commands from /dev/tty0 (PS/2 keyboard or
 * headless serial — the kernel tty feeds both).
 *
 * Built-ins : help, echo, ls, cat, cd, getpid, exit
 * Externals : fork + execve, PATH = /bin:/sbin (seeded from the
 *             ext2 disk into the root ramfs at boot)
 *
 * Build: make -C userspace/sinush   (needs ../libc/libc.a first)
 * Install: /bin/sinush on the Sinux disk image.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>

#define STDIN  0
#define STDOUT 1
#define STDERR 2

#define LINE_MAX  256
#define MAX_ARGS  16
#define DENTS_MAX 2048
#define IO_CHUNK  1024

static char  shell_cwd[256] = "/";
static char  line_buf[LINE_MAX];
static char *args[MAX_ARGS + 1];
static char  dents_buf[DENTS_MAX];
static char  io_buf[IO_CHUNK];

static char *child_envp[] = {
    "PATH=/bin:/sbin",
    "HOME=/root",
    "TERM=vt100",
    NULL
};

/* ── tiny output helper ─────────────────────────────────────── */
static void
putstr(int fd, const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    if (n) write(fd, s, n);
}

/* ── path helpers ───────────────────────────────────────────── */
static void
join_path(const char *base, const char *rel, char *out, size_t max)
{
    size_t n = 0;

    if (!rel || !rel[0] || rel[0] != '/') {
        while (base[n] && n + 1 < max) { out[n] = base[n]; n++; }
        if (rel && rel[0]) {
            if (n > 0 && out[n - 1] != '/' && n + 1 < max)
                out[n++] = '/';
            size_t j = 0;
            while (rel[j] && n + 1 < max) { out[n] = rel[j]; n++; j++; }
        }
    } else {
        while (rel[n] && n + 1 < max) { out[n] = rel[n]; n++; }
    }
    out[n] = '\0';
}

static int
has_slash(const char *s)
{
    while (*s) { if (*s == '/') return 1; s++; }
    return 0;
}

/* ── built-ins ──────────────────────────────────────────────── */
static void
builtin_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) putstr(STDOUT, " ");
        putstr(STDOUT, argv[i]);
    }
    putstr(STDOUT, "\n");
}

static void
builtin_ls(int argc, char **argv)
{
    char path[256];
    join_path(shell_cwd, argc > 1 ? argv[1] : shell_cwd,
              path, sizeof(path));

    int count = getdents(path, dents_buf, sizeof(dents_buf));
    if (count < 0) {
        putstr(STDERR, "ls: cannot list ");
        putstr(STDERR, path);
        putstr(STDERR, "\n");
        return;
    }
    char *p = dents_buf;
    for (int i = 0; i < count; i++) {
        putstr(STDOUT, p);
        putstr(STDOUT, "  ");
        while (*p) p++;
        p++;
    }
    if (count) putstr(STDOUT, "\n");
}

static void
builtin_cat(int argc, char **argv)
{
    if (argc < 2) {
        putstr(STDERR, "cat: missing file\n");
        return;
    }
    for (int a = 1; a < argc; a++) {
        char path[256];
        join_path(shell_cwd, argv[a], path, sizeof(path));
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            putstr(STDERR, "cat: cannot open ");
            putstr(STDERR, argv[a]);
            putstr(STDERR, "\n");
            continue;
        }
        ssize_t n;
        while ((n = read(fd, io_buf, sizeof(io_buf))) > 0)
            write(STDOUT, io_buf, (size_t)n);
        close(fd);
    }
}

static void
builtin_cd(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "..")) {
        size_t len = strlen(shell_cwd);
        if (len <= 1) return;
        char *p = shell_cwd + len - 1;
        if (*p == '/') p--;
        while (p > shell_cwd && *p != '/') p--;
        if (p == shell_cwd) shell_cwd[1] = '\0';
        else *p = '\0';
        return;
    }

    const char *dst = argc > 1 ? argv[1] : "/";
    char path[256];
    join_path(shell_cwd, dst, path, sizeof(path));

    if (getdents(path, dents_buf, 64) < 0) {
        putstr(STDERR, "cd: no such directory: ");
        putstr(STDERR, argv[1]);
        putstr(STDERR, "\n");
        return;
    }
    size_t i = 0;
    while (path[i] && i + 1 < sizeof(shell_cwd)) {
        shell_cwd[i] = path[i];
        i++;
    }
    shell_cwd[i] = '\0';
    size_t len = strlen(shell_cwd);
    if (len > 1 && shell_cwd[len - 1] == '/')
        shell_cwd[len - 1] = '\0';
}

static void
builtin_help(void)
{
    putstr(STDOUT,
        "Sinush built-ins:\n"
        "  help            this list\n"
        "  echo <txt...>   print text\n"
        "  ls [path]       list directory\n"
        "  cat <file...>   print files\n"
        "  cd <path>       change directory\n"
        "  getpid          print shell PID\n"
        "  exit            leave the shell\n"
        "Anything else runs as /bin/<cmd> or /sbin/<cmd>.\n");
}

/* ── external commands: fork + execve + wait ────────────────── */
static void
run_external(char **argv)
{
    pid_t child = fork();
    if (child < 0) {
        putstr(STDERR, "sinush: fork failed\n");
        return;
    }
    if (child == 0) {
        if (argv[0][0] != '/' && !has_slash(argv[0])) {
            char full[256];
            join_path("/bin", argv[0], full, sizeof(full));
            execve(full, argv, child_envp);
            join_path("/sbin", argv[0], full, sizeof(full));
            execve(full, argv, child_envp);
        } else {
            execve(argv[0], argv, child_envp);
        }
        putstr(STDERR, "sinush: command not found: ");
        putstr(STDERR, argv[0]);
        putstr(STDERR, "\n");
        _exit(127);
    }

    int status = 0;
    wait4(child, &status, 0);
}

/* ── parser + dispatcher ────────────────────────────────────── */
static int
parse(char *line, char **argv, int max)
{
    int   argc = 0;
    char *p    = line;

    while (*p && argc < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
    }
    argv[argc] = NULL;
    return argc;
}

static void
run_line(char *line)
{
    int argc = parse(line, args, MAX_ARGS);
    if (argc == 0) return;

    if (!strcmp(args[0], "exit")) {
        putstr(STDOUT, "bye\n");
        exit(0);
    } else if (!strcmp(args[0], "help")) {
        builtin_help();
    } else if (!strcmp(args[0], "echo")) {
        builtin_echo(argc, args);
    } else if (!strcmp(args[0], "ls")) {
        builtin_ls(argc, args);
    } else if (!strcmp(args[0], "cat")) {
        builtin_cat(argc, args);
    } else if (!strcmp(args[0], "cd")) {
        builtin_cd(argc, args);
    } else if (!strcmp(args[0], "getpid")) {
        printf("PID %d\n", (long long)getpid());
    } else {
        run_external(args);
    }
}

/* ── entry ──────────────────────────────────────────────────── */
int
main(int argc, char *argv[], char *envp[])
{
    (void)argc; (void)argv; (void)envp;

    putstr(STDOUT,
        "\n*** Sinush v1.0-alpha — Sinux interactive shell ***\n"
        "Type 'help' for commands.\n\n");

    /* Startup self-test: the boot log itself proves the shell
     * parses built-ins, lists /bin and reads files. */
    static const char *demo[] = {
        "echo Sinux speaks!",
        "ls /bin",
        "cat /etc/hostname",
        NULL
    };
    for (int i = 0; demo[i]; i++) {
        putstr(STDOUT, "sinux> ");
        putstr(STDOUT, demo[i]);
        putstr(STDOUT, "\n");
        size_t j = 0;
        while (demo[i][j] && j + 1 < sizeof(line_buf)) {
            line_buf[j] = demo[i][j];
            j++;
        }
        line_buf[j] = '\0';
        run_line(line_buf);
    }
    putstr(STDOUT, "\n");

    for (;;) {
        putstr(STDOUT, "sinux> ");

        ssize_t n = read(STDIN, line_buf, sizeof(line_buf) - 1);
        if (n <= 0) continue;

        while (n > 0 && (line_buf[n - 1] == '\n' ||
                         line_buf[n - 1] == '\r'))
            n--;
        line_buf[n] = '\0';

        run_line(line_buf);
    }

    return 0;
}
