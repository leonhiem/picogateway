#include "task.h"
#include "pico/time.h"
#include <cstdio>
#include <cstring>

#define TASK_MAX 32 // 2026-08-21: bumped from 8 -- shell+tpo (2) plus
                    // MAX_JOBS job slots (jobs.h, now 12) already needs
                    // 14; given real headroom rather than sized to
                    // match, same lesson as fs.h/prog.h's caps this
                    // session.
                    // 2026-08-21: bumped again, 24 -> 32, alongside
                    // MAX_JOBS growing to 20 (shell+tpo+20 = 22, too
                    // close to 24 for comfort).

typedef struct {
    const char *name;
    task_fn_t   fn;
    uint32_t    period_ms;
    absolute_time_t next_run;
} task_t;

static task_t tasks[TASK_MAX];
static int task_count = 0;

void task_register(const char *name, task_fn_t fn, uint32_t period_ms)
{
    if (task_count >= TASK_MAX) {
        // Loud on purpose -- kernel/fs.h's fs_register() and
        // kernel/prog.h's prog_register() both had this exact silent-
        // return TODO before being fixed the same way; this is that
        // same fix applied here before it caused the same bug a third
        // time (it was about to: MAX_JOBS's growth would have silently
        // dropped job slots past the old TASK_MAX=8 with zero trace).
        printf("task_register: table full (%d), dropped %s\n", TASK_MAX, name);
        return;
    }

    tasks[task_count].name      = name;
    tasks[task_count].fn        = fn;
    tasks[task_count].period_ms = period_ms;
    tasks[task_count].next_run  = make_timeout_time_ms(period_ms);
    task_count++;
}

void task_run(void)
{
    for (int i = 0; i < task_count; i++) {
        if (absolute_time_diff_us(get_absolute_time(), tasks[i].next_run) < 0) {
            tasks[i].next_run = make_timeout_time_ms(tasks[i].period_ms);
            tasks[i].fn();
        }
    }
}

bool task_postpone(const char *name, uint32_t period_ms)
{
    for (int i = 0; i < task_count; i++) {
        if (strcmp(tasks[i].name, name) == 0) {
            tasks[i].next_run = make_timeout_time_ms(period_ms);
            return true;
        }
    }
    return false;
}
