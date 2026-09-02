/**
 * kernel/klog.h — serialized logging across independent tasks
 *
 * Tasks call klog() instead of printf(). Each call formats a complete
 * line and queues it; nothing is written to stdio at call time. One
 * dedicated task calls klog_flush() to drain the queue and do the actual
 * printf(), so output from independent tasks is always whole lines,
 * never interleaved mid-line.
 *
 * Safe without locks because the scheduler (kernel/task.h) is cooperative
 * and single-core: only one task function runs at a time and none of
 * them yield mid-function, so pushes from different tasks can never race
 * with each other or with the drain in klog_flush(). This stops being
 * true if picoos ever gains preemption or uses core1 — see task.h.
 */
#pragma once

/* Sized to comfortably exceed the widest existing debug line
 * (picoos.cpp's per-second telemetry row is ~90 chars). */
#define KLOG_MAX_MSG   128
#define KLOG_QUEUE_LEN 16

void klog(const char *tag, const char *fmt, ...);

/* Call from exactly one task. Prints and removes every message
 * currently queued; does nothing if the queue is empty. */
void klog_flush(void);
