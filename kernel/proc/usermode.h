#pragma once
#include <stdint.h>
#include "process.h"

void usermode_map_stack(uint64_t *pml4);
uint64_t build_user_stack(uint64_t stack_top,
                           int argc, char **argv,
                           int envc, char **envp);
void usermode_exec(process_t *proc,
                   uint64_t   entry,
                   uint64_t   load_end,
                   int        argc,
                   char     **argv,
                   int        envc,
                   char     **envp);
