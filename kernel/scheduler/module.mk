# SMP runqueue scheduler module for Sinux
#
# Compiled on every build (so bit-rot is caught early) but NOT linked into
# the kernel yet: kernel/proc/scheduler.c still provides the live scheduling
# API (sched_init/sched_add/... used by pit.c, syscall.c, signal.c, init.c),
# and both modules export sched_tick/sched_yield/sched_sleep/sched_wake,
# which collide at link time. Once the proc/scheduler.c callers migrate to
# the runqueue API, move the object back into OBJS.

SCHED_COMPILE_ONLY_OBJS := $(BUILD)/kernel/scheduler/sched.o

compile-check: $(SCHED_COMPILE_ONLY_OBJS)

$(BUILD)/kernel/scheduler/%.o: kernel/scheduler/%.c | $(BUILD)/kernel/scheduler
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/kernel/scheduler:
	mkdir -p $@
