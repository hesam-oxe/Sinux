#include "../include/types.h"
#include "../include/syscall.h"
#include "../include/unistd.h"
#include "../include/fcntl.h"

extern int64_t syscall(uint64_t nr, ...);

ssize_t read(int fd, void *buf, size_t n)  { return (ssize_t)syscall(SYS_READ,  (uint64_t)fd, (uint64_t)buf, (uint64_t)n); }
ssize_t write(int fd, const void *buf, size_t n) { return (ssize_t)syscall(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)n); }
int     open(const char *p, int flags)    { return (int)syscall(SYS_OPEN, (uint64_t)p, (uint64_t)flags, 0); }
int     close(int fd)                       { return (int)syscall(SYS_CLOSE, (uint64_t)fd); }
int     getdents(const char *p, char *buf, size_t len) {
    return (int)syscall(SYS_GETDENTS, (uint64_t)p, (uint64_t)buf, (uint64_t)len);
}
pid_t   getpid(void)                        { return (pid_t)syscall(SYS_GETPID); }
pid_t   getppid(void)                       { return (pid_t)syscall(SYS_GETPPID); }
pid_t   fork(void)                          { return (pid_t)syscall(SYS_FORK); }
void    _exit(int code)                     { syscall(SYS_EXIT, (uint64_t)code); for(;;); }
int     pipe(int fds[2])                    { return (int)syscall(SYS_PIPE, (uint64_t)fds); }
pid_t   wait4(pid_t pid, int *st, int opt)  { return (pid_t)syscall(SYS_WAIT4, (uint64_t)pid, (uint64_t)st, (uint64_t)opt); }
int     kill(pid_t pid, int sig)            { return (int)syscall(SYS_KILL, (uint64_t)pid, (uint64_t)sig); }
unsigned int sleep(unsigned int s)          { syscall(SYS_SLEEP, (uint64_t)s, 0); return 0; }

int execve(const char *p, char *const av[], char *const ev[]) {
    return (int)syscall(SYS_EXECVE, (uint64_t)p, (uint64_t)av, (uint64_t)ev);
}
