/* cat — concatenate files (or stdin) to stdout. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static char buf[1024];

int
main(int argc, char *argv[], char *envp[])
{
    (void)envp;

    if (argc < 2) {
        ssize_t n;
        while ((n = read(0, buf, sizeof(buf))) > 0)
            write(1, buf, (size_t)n);
        return 0;
    }

    int rc = 0;
    for (int a = 1; a < argc; a++) {
        int fd = open(argv[a], O_RDONLY);
        if (fd < 0) {
            printf("cat: cannot open %s\n", argv[a]);
            rc = 1;
            continue;
        }
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, (size_t)n);
        close(fd);
    }
    return rc;
}
