/* echo — print arguments separated by spaces. */
#include <stdio.h>

int
main(int argc, char *argv[], char *envp[])
{
    (void)envp;

    for (int i = 1; i < argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
    return 0;
}
