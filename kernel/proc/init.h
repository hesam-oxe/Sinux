#pragma once

/*
 * proc_spawn_init — launch PID 1 from /sbin/init or /bin/init
 *
 * Reads the ELF binary, creates a ring-3 process, builds the
 * initial user stack (argc/argv/envp), sets up an arch_switch
 * frame, and adds the process to the scheduler queue.
 *
 * Returns 0 on success, negative on error (binary not found, bad ELF, …).
 * Does NOT call _enter_usermode — the caller must enter the idle loop
 * so the scheduler can switch to init when it gets its first timeslice.
 */
int proc_spawn_init(void);
