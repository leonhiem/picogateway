/**
 * shell.cpp — task_shell: a tiny Plan 9/rc-flavored interactive shell
 *
 * Trimmed down from ../picoos's current shell.cpp: the pipe/redirect/
 * background-job core (its original shape from picoos commit 2ece80a),
 * without script/run/loop/watch -- those were later, separate steps in
 * picoos's own history and are good candidates for a later step here
 * too, once there's more than one device worth watching or scripting.
 *
 * Polls stdio non-blocking (getchar_timeout_us(0)), so it fits the
 * cooperative model with no scheduler changes. Echoes what's typed and
 * dispatches complete lines.
 *
 * Grammar: stage [| stage ...] [< path] [> path] [&]
 *   stage := progname [arg...]
 * `jobs` and `kill %<id>` are true shell builtins (they touch the
 * shell's own job table, not a text-in/text-out stream); everything
 * else is a program (kernel/prog.h) run through the pipeline machinery
 * (jobs.h), foreground or backgrounded.
 */
#include "kernel/fs.h"
#include "jobs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define LINE_MAX    96
#define MAX_TOKENS  20

static void print_prompt(void)
{
    printf("%% ");
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

void task_shell(void)
{
    static char line[LINE_MAX];
    static int  line_len = 0;
    static bool banner_shown = false;

    if (!banner_shown) {
        printf("\npicogateway -- type 'ls' to see /dev and bin/, 'jobs'/'kill' to manage background pipelines\n");
        print_prompt();
        banner_shown = true;
    }

    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[line_len] = '\0';
            dispatch(line);
            line_len = 0;
            print_prompt();
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
