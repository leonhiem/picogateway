/**
 * shell.cpp — task_shell: a tiny Plan 9/rc-flavored interactive shell
 *
 * Step 8: ports script/run/loop/watch and boot-time auto-run from
 * ../picoos's current shell.cpp (its post-step-4 history) -- the
 * pipe/redirect/background-job core here was already that shape since
 * step 4; this step catches picogateway's shell up to what step 9+
 * actually needs: a "boot" script, baked into the image, that
 * task_shell() runs automatically on its first tick, before the
 * prompt ever appears. That's the mechanism the LoRa/wifi subsystems
 * (steps 10/11) will use to start themselves as backgroundable,
 * restartable bin/ jobs instead of gateway.cpp's old inline, blocking
 * startup code -- see BOOT_SCRIPT_TEXT below, still empty until those
 * programs exist.
 *
 * Grammar: stage [| stage ...] [< path] [> path] [&]
 *   stage := progname [arg...]
 * `jobs`, `kill %<id>`, `sleep <ms>`, and `script`/`run`/`scripts`/
 * `loop`/`watch` are true shell builtins (they touch the shell's own
 * state, not a text-in/text-out stream); everything else is a program
 * (kernel/prog.h) run through the same pipeline machinery (jobs.h),
 * foreground or backgrounded.
 *
 * `sleep` doesn't block -- nothing in this kernel ever does. It just
 * remembers a wake time; task_shell() checks it at the top of every
 * one of its own ticks and returns immediately if not yet time, same
 * as any other task "waiting" for its next scheduled run. Everything
 * else (jobs, sensor polling, ...) keeps running normally meanwhile.
 *
 * `script <name>` / `run <name>` -- named, storable sequences of shell
 * lines. `script <name>` captures typed lines (prompt becomes "> ")
 * until a lone "." ends it. `run <name>` replays them through the
 * same dispatch() a typed line goes through, one line per tick, driven
 * by the same resumable `sleeping` flag `sleep` uses -- a `sleep`
 * inside a script really waits before the next line runs, same as if
 * typed by hand. `loop <name>` is `run` that restarts from the top
 * instead of stopping, forever until Ctrl-C (ASCII ETX, 0x03).
 *
 * `watch <ms> <stage> [| stage ...] [< path]` repeats a pipeline in
 * the foreground every <ms>, printing every time, until Ctrl-C.
 *
 * A "boot" script comes pre-loaded into scripts[0] -- baked into the
 * firmware image itself (BOOT_SCRIPT_TEXT below), not written to any
 * storage, so it's there after every reboot without needing EEPROM
 * (the real /dev/config/* EEPROM is for gateway settings, not shell
 * scripts). task_shell() auto-runs it on its very first tick, before
 * the prompt ever appears -- same "unattended at every power-up"
 * choice picoos made for its own boot script, named explicitly here
 * too rather than left as a silent default.
 */
#include "kernel/fs.h"
#include "jobs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define LINE_MAX    96
#define MAX_TOKENS  20

#define SCRIPT_MAX       4
#define SCRIPT_NAME_MAX  16
#define SCRIPT_TEXT_MAX  512 // room to grow once bin/lora, bin/wifi exist
                              // and BOOT_SCRIPT_TEXT actually starts them
                              // (steps 10/11) -- picoos needed 1024 for a
                              // much bigger boot recipe; bump this the
                              // same way if/when it gets close.

typedef struct {
    bool used;
    char name[SCRIPT_NAME_MAX];
    char text[SCRIPT_TEXT_MAX]; // lines joined by '\n', nul-terminated
} script_t;

// Empty for now -- nothing needs starting yet at this step (buttons,
// room sensors, and config are registered unconditionally in main(),
// not through here; see the file header). Steps 10/11 add
// "lora &\n" / "wifi &\n" style lines once those bin/ programs exist,
// making LoRa/wifi bring-up start unattended at boot while staying
// killable/restartable through the normal job table -- the whole
// point of routing them through a script instead of main() calling
// their init functions directly.
#define BOOT_SCRIPT_TEXT ""

static script_t scripts[SCRIPT_MAX] = {
    { true, "boot", BOOT_SCRIPT_TEXT },
};

static bool sleeping = false;
static absolute_time_t wake_time;

static bool capturing = false;
static char capture_name[SCRIPT_NAME_MAX];
static char capture_buf[SCRIPT_TEXT_MAX];
static int  capture_len = 0;

static bool running = false;      // a script is being replayed, one line per tick
static char run_buf[SCRIPT_TEXT_MAX];
static int  run_pos = 0;          // offset of the next unread line in run_buf
static bool looping = false;      // `loop` instead of `run`: restart run_buf from
                                   // the top when it ends, instead of stopping
static int  run_slot = -1;        // which scripts[] entry run_buf was last loaded
                                   // from -- needed to refresh it on a loop restart
                                   // (run_step()/dispatch() mutate run_buf in place
                                   // as they go, so replaying the same bytes a
                                   // second time without reloading from the
                                   // pristine scripts[] copy first reads back
                                   // garbage -- every line has shrunk to its
                                   // first word)

static bool watching = false;
static pipeline_t watch_pipeline;
static uint32_t watch_interval_ms;
static absolute_time_t watch_next;

static void print_prompt(void)
{
    printf(capturing ? "> " : "%% ");
}

static int tokenize(char *line, char *tokens[MAX_TOKENS])
{
    int n = 0;
    char *tok = strtok(line, " \t");
    while (tok && n < MAX_TOKENS) {
        tokens[n++] = tok;
        tok = strtok(0, " \t");
    }
    return n;
}

static bool parse_pipeline(char **tok, int ntok, pipeline_t *p, bool *background)
{
    p->nstages = 1;
    p->stages[0].argc = 0;
    p->has_redirect_out = false;
    p->has_redirect_in = false;
    *background = false;

    pipeline_stage_t *stage = &p->stages[0];

    for (int i = 0; i < ntok; i++) {
        const char *t = tok[i];
        if (strcmp(t, "|") == 0) {
            if (p->nstages >= JOB_MAX_STAGES) { printf("too many pipeline stages\n"); return false; }
            stage = &p->stages[p->nstages++];
            stage->argc = 0;
        } else if (strcmp(t, ">") == 0) {
            if (++i >= ntok) { printf("> needs a path\n"); return false; }
            strncpy(p->redirect_out, tok[i], JOB_TOK_MAX - 1);
            p->redirect_out[JOB_TOK_MAX - 1] = '\0';
            p->has_redirect_out = true;
        } else if (strcmp(t, "<") == 0) {
            if (++i >= ntok) { printf("< needs a path\n"); return false; }
            strncpy(p->redirect_in, tok[i], JOB_TOK_MAX - 1);
            p->redirect_in[JOB_TOK_MAX - 1] = '\0';
            p->has_redirect_in = true;
        } else if (strcmp(t, "&") == 0) {
            *background = true;
        } else {
            if (stage->argc >= JOB_MAX_ARGS) { printf("too many args in one stage\n"); return false; }
            strncpy(stage->argv[stage->argc], t, JOB_TOK_MAX - 1);
            stage->argv[stage->argc][JOB_TOK_MAX - 1] = '\0';
            stage->argc++;
        }
    }
    return true;
}

// Finds a used script slot by name, or -1. Shared by capture_line()'s
// "am I redefining an existing script" check, the `run`/`loop`
// builtins, and task_shell()'s own boot-time auto-run lookup.
static int find_script(const char *name)
{
    for (int i = 0; i < SCRIPT_MAX; i++) {
        if (scripts[i].used && strcmp(scripts[i].name, name) == 0) return i;
    }
    return -1;
}

// Appends one typed line to the script currently being captured, or
// finalizes it on a lone ".". Not called while a script is running --
// run replays through dispatch(), never through capture.
static void capture_line(const char *line)
{
    if (strcmp(line, ".") == 0) {
        capture_buf[capture_len] = '\0';
        int slot = find_script(capture_name);
        if (slot < 0) {
            for (int i = 0; i < SCRIPT_MAX; i++) {
                if (!scripts[i].used) { slot = i; break; }
            }
        }
        if (slot < 0) {
            printf("script: table full (max %d), '%s' not saved\n", SCRIPT_MAX, capture_name);
        } else {
            strncpy(scripts[slot].name, capture_name, SCRIPT_NAME_MAX - 1);
            scripts[slot].name[SCRIPT_NAME_MAX - 1] = '\0';
            strncpy(scripts[slot].text, capture_buf, SCRIPT_TEXT_MAX - 1);
            scripts[slot].text[SCRIPT_TEXT_MAX - 1] = '\0';
            scripts[slot].used = true;
            printf("script '%s' saved (%d bytes)\n", capture_name, capture_len);
        }
        capturing = false;
        return;
    }

    int llen = strlen(line);
    if (capture_len + llen + 1 >= SCRIPT_TEXT_MAX) { // +1 for the '\n' this line adds
        printf("script: buffer full, line dropped\n");
        return;
    }
    memcpy(capture_buf + capture_len, line, llen);
    capture_len += llen;
    capture_buf[capture_len++] = '\n';
}

static void dispatch(char *line)
{
    char *tok[MAX_TOKENS];
    int ntok = tokenize(line, tok);
    if (ntok == 0) return; // empty line

    if (strcmp(tok[0], "jobs") == 0) {
        job_list();
        return;
    }
    if (strcmp(tok[0], "kill") == 0) {
        if (ntok < 2) { printf("usage: kill %%<jobid>\n"); return; }
        const char *arg = tok[1];
        if (*arg == '%') arg++;
        if (!job_kill(atoi(arg))) printf("kill: no such job\n");
        return;
    }
    if (strcmp(tok[0], "sleep") == 0) {
        if (ntok < 2) { printf("usage: sleep <ms>\n"); return; }
        int ms = atoi(tok[1]);
        if (ms < 0) ms = 0;
        wake_time = make_timeout_time_ms(ms);
        sleeping = true;
        return; // no prompt yet -- task_shell() prints one once awake
    }
    if (strcmp(tok[0], "script") == 0) {
        if (ntok < 2) { printf("usage: script <name>\n"); return; }
        strncpy(capture_name, tok[1], SCRIPT_NAME_MAX - 1);
        capture_name[SCRIPT_NAME_MAX - 1] = '\0';
        capture_len = 0;
        capturing = true;
        printf("capturing '%s' -- end with a line containing just '.'\n", capture_name);
        return; // prompt becomes "> " until capture ends
    }
    if (strcmp(tok[0], "scripts") == 0) {
        bool any = false;
        for (int i = 0; i < SCRIPT_MAX; i++) {
            if (scripts[i].used) { printf("%s\n", scripts[i].name); any = true; }
        }
        if (!any) printf("no scripts\n");
        return;
    }
    if (strcmp(tok[0], "run") == 0) {
        if (ntok < 2) { printf("usage: run <name>\n"); return; }
        if (running) { printf("run: already running a script\n"); return; }
        int slot = find_script(tok[1]);
        if (slot < 0) { printf("run: no such script '%s'\n", tok[1]); return; }
        strncpy(run_buf, scripts[slot].text, SCRIPT_TEXT_MAX - 1);
        run_buf[SCRIPT_TEXT_MAX - 1] = '\0';
        run_pos = 0;
        run_slot = slot;
        running = true;
        return; // task_shell() drives it from here, one line per tick -- see run_step()
    }
    if (strcmp(tok[0], "loop") == 0) {
        if (ntok < 2) { printf("usage: loop <name>\n"); return; }
        if (running) { printf("loop: already running a script\n"); return; }
        int slot = find_script(tok[1]);
        if (slot < 0) { printf("loop: no such script '%s'\n", tok[1]); return; }
        strncpy(run_buf, scripts[slot].text, SCRIPT_TEXT_MAX - 1);
        run_buf[SCRIPT_TEXT_MAX - 1] = '\0';
        run_pos = 0;
        run_slot = slot;
        running = true;
        looping = true;
        printf("looping '%s' -- Ctrl-C to stop\n", tok[1]);
        return; // task_shell() drives it from here -- see run_step() and the
                 // `if (running)` block's own looping/Ctrl-C handling
    }
    if (strcmp(tok[0], "watch") == 0) {
        if (ntok < 3) { printf("usage: watch <ms> <stage> [| stage ...] [< path]\n"); return; }
        int ms = atoi(tok[1]);
        if (ms < 50) ms = 50; // sane floor -- also protects against a typo like "watch heater"
        bool background;
        if (!parse_pipeline(tok + 2, ntok - 2, &watch_pipeline, &background)) return;
        if (watch_pipeline.stages[0].argc == 0) return;
        if (background) printf("watch: '&' ignored -- watch already repeats on its own\n");
        watch_interval_ms = (uint32_t)ms;
        watch_next = get_absolute_time(); // fire the first line immediately
        watching = true;
        printf("watching every %dms -- Ctrl-C to stop\n", ms);
        return; // no normal prompt -- watch prints its own lines until stopped
    }
    if (tok[0][0] == '\0') return;
    if (strcmp(tok[0], "|") == 0 || strcmp(tok[0], ">") == 0) {
        printf("empty pipeline\n");
        return;
    }

    pipeline_t p;
    bool background;
    if (!parse_pipeline(tok, ntok, &p, &background)) return;
    if (p.stages[0].argc == 0) return; // e.g. line was just "&"

    if (background) {
        int id = job_start(&p);
        if (id < 0) printf("no free job slots (max %d)\n", MAX_JOBS);
        else printf("[%d]\n", id);
    } else {
        pipeline_run_once(&p, true); // foreground: always show the result
    }
}

// Runs exactly one line of the in-progress script, advancing run_pos
// past it, and clears `running` once the script is out of lines.
// Called at most once per task_shell() tick -- if the line dispatches
// to `sleep`, this simply isn't called again until that sleep's wake
// time passes, so a script's sleep behaves exactly like a typed one.
static void run_step(void)
{
    if (run_buf[run_pos] == '\0') { running = false; return; }

    char *ln = run_buf + run_pos;
    char *nl = strchr(ln, '\n');
    if (nl) { *nl = '\0'; run_pos = (int)(nl - run_buf) + 1; }
    else     { run_pos += strlen(ln); } // last line (shouldn't lack a trailing '\n', but safe)

    printf("%% %s\n", ln);
    dispatch(ln);
}

void task_shell(void)
{
    static char line[LINE_MAX];
    static int  line_len = 0;
    static bool banner_shown = false;

    if (!banner_shown) {
        printf("\npicogateway\n");
        printf("  ls            list /dev and bin/\n");
        printf("  jobs, kill    manage background pipelines\n");
        printf("  script, run   record and replay command sequences\n");
        printf("  loop          like run, but repeats forever, Ctrl-C to stop\n");
        printf("  watch         repeat a pipeline every <ms>, Ctrl-C to stop\n");
        banner_shown = true;

        // Auto-run 'boot' -- same "unattended at every power-up" choice
        // picoos made for its own boot script. scripts[0] is always
        // seeded with "boot" (BOOT_SCRIPT_TEXT above), so find_script()
        // only fails here if something someday changes that.
        int slot = find_script("boot");
        if (slot >= 0) {
            printf("running 'boot' automatically...\n");
            strncpy(run_buf, scripts[slot].text, SCRIPT_TEXT_MAX - 1);
            run_buf[SCRIPT_TEXT_MAX - 1] = '\0';
            run_pos = 0;
            running = true; // picked up by the `if (running)` block below,
                             // same tick -- first line runs immediately
        } else {
            print_prompt();
        }
    }

    if (sleeping) {
        if (absolute_time_diff_us(get_absolute_time(), wake_time) > 0) return; // not yet
        sleeping = false;
        if (!running) print_prompt(); // a script's own sleep resumes silently, no interactive prompt
        // fall through: either process input that arrived while asleep,
        // or (if running) let the block below continue the script now
    }

    if (running) {
        run_step();
        if (!running) {
            if (looping) {
                // Script reached its end -- restart it instead of stopping.
                // run_buf must be reloaded from the pristine scripts[]
                // text, not just rewound -- see run_slot's comment above.
                strncpy(run_buf, scripts[run_slot].text, SCRIPT_TEXT_MAX - 1);
                run_buf[SCRIPT_TEXT_MAX - 1] = '\0';
                run_pos = 0;
                running = true;
            } else if (!sleeping) {
                print_prompt(); // script just finished, and didn't end mid-sleep
            }
        }
        if (looping) {
            // A plain `run` never reads stdin while replaying -- but a
            // loop never stops on its own, so it needs its own Ctrl-C
            // poll, same convention `watching` below uses.
            int c;
            while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
                if (c == 3) { // Ctrl-C
                    looping = false;
                    running = false;
                    printf("(loop stopped)\n");
                    print_prompt();
                    break;
                }
                // anything else typed while looping is discarded, on purpose
            }
        }
        return; // script lines advance one per tick; don't also read keyboard input this tick
    }

    if (watching) {
        if (absolute_time_diff_us(get_absolute_time(), watch_next) <= 0) {
            pipeline_run_once(&watch_pipeline, true); // foreground: always prints
            watch_next = make_timeout_time_ms(watch_interval_ms);
        }
        int c;
        while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            if (c == 3) { // Ctrl-C
                watching = false;
                printf("(watch stopped)\n");
                print_prompt();
                break;
            }
            // anything else typed while watching is discarded, on purpose
        }
        return; // watching owns this tick; don't also run the normal command loop
    }

    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[line_len] = '\0';
            if (capturing) capture_line(line);
            else dispatch(line);
            line_len = 0;
            if (!sleeping && !running && !watching) print_prompt();
        } else if (c == 8 || c == 127) { // backspace / DEL
            if (line_len > 0) {
                line_len--;
                printf("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7f && line_len < LINE_MAX - 1) {
            // Printable ASCII only -- silently drop anything else (e.g.
            // stray bytes from USB CDC enumeration/terminal noise right
            // at connect time).
            line[line_len++] = (char)c;
            putchar(c);
        }
    }
}
