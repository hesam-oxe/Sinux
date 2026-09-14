/* ls — list directory entries, one row, via SYS_GETDENTS. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char dents[2048];

int
main(int argc, char *argv[], char *envp[])
{
    (void)envp;

    const char *path = argc > 1 ? argv[1] : "/";

    int n = getdents(path, dents, sizeof(dents));
    if (n < 0) {
        printf("ls: cannot list %s\n", path);
        return 1;
    }

    char *p = dents;
    for (int i = 0; i < n; i++) {
        printf("%s  ", p);
        while (*p) p++;
        p++;
    }
    if (n) printf("\n");
    return 0;
}
