/**
 * jobs.h — pipeline execution + background job control
 *
 * A pipeline is a chain of program stages (kernel/prog.h) with optional
 * `<`/`>` device redirects. pipeline_run_once() runs it synchronously
 * once, either printing the result to the terminal or writing it to a
 * `>` device -- except backgrounded jobs with no `>` redirect, which
 * print nothing: a job like `toggle ... &` normally has nothing to say
 * ("idle") on most ticks, and printing that to the terminal every
 * JOB_POLL_MS would spam it badly. Foreground commands always print
 * (there's no other way to see the result); a background job with a
 * `>` still writes there every tick same as always.
 *
 * Backgrounding (`&`) doesn't fork -- there's no process model here,
 * just kernel/task.h's cooperative scheduler. A fixed pool of MAX_JOBS
 * slots is registered as real tasks once, at boot (jobs_init()), each
 * normally a no-op; starting a job fills a slot and it starts running
 * on its own schedule, "killing" it just flips the slot back to
 * inactive. kernel/task.h itself is untouched -- no task_unregister()
 * needed, and no way to pass a slot index through its plain function
 * pointer, so each slot still needs its own distinct trampoline
 * function. jobs.cpp generates them via an X-macro now (was hand-
 * duplicated one-by-one through MAX_JOBS=4 -- fine at 4, not at the
 * 8+ concurrent jobs the phase-machine wiring plus UI jobs actually
 * need) -- growing MAX_JOBS is a one-line list edit in jobs.cpp now,
 * not retyping N near-identical functions.
 *
 * All jobs share one fixed poll interval (JOB_POLL_MS) -- not
 * independently tunable per job yet. 150ms was chosen for button-press
 * responsiveness (toggle); still plenty slow enough for a thermostat.
 */
#pragma once

#define JOB_MAX_STAGES 4
#define JOB_MAX_ARGS   6 // slot 0 is the program name itself; bumped from 4
                         // for adjust's <gate> <target-on> <target-off> <step>
#define JOB_TOK_MAX    24
#define MAX_JOBS       20 // must match jobs.cpp's JOB_SLOTS X-macro list, entry-for-entry.
                          // Bumped from 12 -- the boot script alone is about to
                          // reach 11 jobs, and LED wiring (the next piece) will
                          // add more; given real headroom this time, same lesson
                          // as every other capacity bump this session.
#define STAGE_BUF     1024 // ls's own listing grows with every device/program
                            // added -- 256 silently truncated it once (the bug
                            // that surfaced the whole snprintf-overcounting
                            // class of fix), then 512 hit the same wall again,
                            // cleanly this time (the fixed ls just stops rather
                            // than corrupt anything), once dev/pid.cpp's six
                            // devices pushed the real listing to 538 bytes.
                            // Sized with real headroom this time instead of
                            // matching the count exactly.
#define JOB_POLL_MS    150

typedef struct {
    char argv[JOB_MAX_ARGS][JOB_TOK_MAX];
    int  argc;
} pipeline_stage_t;

typedef struct {
    int  nstages;
    pipeline_stage_t stages[JOB_MAX_STAGES];
    bool has_redirect_out;
    char redirect_out[JOB_TOK_MAX];
    bool has_redirect_in;
    char redirect_in[JOB_TOK_MAX];
} pipeline_t;

void pipeline_run_once(pipeline_t *p, bool print_if_no_redirect);

void jobs_init(void);
int  job_start(const pipeline_t *p);  // returns job id (1..MAX_JOBS), or -1 if all slots busy
void job_list(void);
bool job_kill(int id);
