/**
 * prog/lora.cpp — lora: bring up (or re-bring-up) the LoRa radio
 *
 * Step 10: gateway.cpp's main() never calls lora_init() (the old
 * blocking retry loop) any more -- LoRa.begin() only runs from here,
 * through lora_try_start() (lora.cpp), which makes at most one real
 * attempt per call and returns immediately either way. That's what
 * lets this be an ordinary stateless prog/*.cpp program (kernel/prog.h:
 * no loop, no sleep, just "try once, report the result") instead of
 * needing its own blocking-call special case.
 *
 * Meant to run backgrounded from the boot script:
 *
 *   lora &
 *
 * lora_try_start() is itself rate-limited (at most one real
 * LoRa.begin() attempt every few seconds), so being re-invoked every
 * JOB_POLL_MS by the job scheduler costs nothing once the radio is up
 * (an idle bool check) or while it's still down (skipped attempts, not
 * extra ones). If task_poll_lora() ever sees an implausible packet and
 * clears radio_ready (lora.cpp), this job notices on its own next tick
 * and restarts the radio -- no separate "did it die" check needed,
 * same "is it ready yet" question either way. That's also why this is
 * a program the user can re-run by hand (`lora`) rather than something
 * that only ever runs once at boot: if the radio ever needs a kick
 * outside the automatic path, `lora &` again is the restart.
 */
#include "kernel/prog.h"
#include "lora.h"
#include <cstdio>

static int lora_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    bool up = lora_try_start();
    return snprintf(out, outlen, up ? "ready\n" : "starting\n");
}

static const program_t prog_lora = {"lora", lora_run};

void lora_register(void)
{
    prog_register(&prog_lora);
}
