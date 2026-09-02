#include "klog.h"
#include <cstdio>
#include <cstdarg>

typedef struct {
    char text[KLOG_MAX_MSG];
} klog_msg_t;

static klog_msg_t queue[KLOG_QUEUE_LEN];
static int head = 0;   // next slot to write
static int tail = 0;   // next slot to read
static int count = 0;  // messages currently queued
static int dropped = 0;

void klog(const char *tag, const char *fmt, ...)
{
    if (count >= KLOG_QUEUE_LEN) {
        dropped++;   // queue full: drop rather than block the caller
        return;
    }

    klog_msg_t *slot = &queue[head];
    int n = snprintf(slot->text, KLOG_MAX_MSG, "[%s] ", tag);
    if (n < 0) n = 0;
    if (n > KLOG_MAX_MSG) n = KLOG_MAX_MSG;

    va_list args;
    va_start(args, fmt);
    vsnprintf(slot->text + n, KLOG_MAX_MSG - n, fmt, args);
    va_end(args);

    head = (head + 1) % KLOG_QUEUE_LEN;
    count++;
}

void klog_flush(void)
{
    while (count > 0) {
        printf("%s\n", queue[tail].text);
        tail = (tail + 1) % KLOG_QUEUE_LEN;
        count--;
    }
    if (dropped > 0) {
        printf("[klog] dropped %d message(s), queue was full\n", dropped);
        dropped = 0;
    }
}
