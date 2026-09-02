/**
 * kernel/task.h — picoos minimal cooperative task scheduler
 *
 * This is the seed of the reusable "OS" part of picoos: a fixed table of
 * named, periodic, run-to-completion tasks, dispatched from a single loop
 * on one core. No preemption, no per-task stacks, no blocking — a task
 * function must do its work and return quickly, same discipline as the
 * hand-rolled scheduling loop this replaces.
 *
 * Deliberately tiny. The intent is to grow this file (or add siblings next
 * to it, e.g. a future chan.h) as picoos grows toward real processes and
 * message passing — not to design that in up front.
 */
#pragma once

#include <stdint.h>

typedef void (*task_fn_t)(void);

/* Register a periodic task. fn is called by task_run() once every
 * period_ms, best-effort (no guarantee of exact timing — this is
 * cooperative, not real-time). Safe to call only before the scheduler
 * loop starts; there is no task_unregister() yet. */
void task_register(const char *name, task_fn_t fn, uint32_t period_ms);

/* Call repeatedly from main()'s loop. Runs any task whose period has
 * elapsed since its last run, then returns — never blocks. */
void task_run(void);

/* Reset a named task's deadline, as if it had just run. Lets one task
 * explicitly postpone another — e.g. suppressing a check right after a
 * state transition it knows would otherwise trigger a false positive.
 * Returns false if no task with that name is registered. */
bool task_postpone(const char *name, uint32_t period_ms);
