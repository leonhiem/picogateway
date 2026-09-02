/**
 * kernel/fs.h — everything is a file
 *
 * A minimal Plan 9-flavored device namespace: a flat table of named
 * devices, each speaking open()/close()/read()/write() over plain text.
 * No ioctl(). Reading a device produces a fresh line of text on every
 * call (there's no seek/stream position — doesn't make sense for a live
 * sensor); writing sends it a text command it parses itself.
 *
 * Deliberately global/flat rather than per-task namespaces (Plan 9's
 * mount/bind machinery) — that's solving a multi-process isolation
 * problem this single-address-space microcontroller doesn't have yet.
 * A real stretch goal if picoos ever grows actual isolated processes,
 * not something to build speculatively now.
 */
#pragma once

#define FS_MAX_DEVICES 64 // 2026-08-16: bumped from 16 after it silently
                          // dropped /dev/setpoint -- registered 17th, past
                          // the old cap, with no warning at all. See fs.cpp.
                          // 2026-08-19: bumped from 32 -- dev/pid.cpp's six
                          // devices landed exactly on that cap (32/32 used,
                          // zero headroom), caught only by grepping the
                          // registration count before it became the same
                          // bug again.
                          // 2026-08-23: bumped from 48 -- /dev/tft/*'s seven
                          // devices would have landed at 47/48, one spare;
                          // given real headroom this time instead of just
                          // enough, same lesson as every prior bump here.
#define FS_NAME_MAX    32

typedef struct {
    const char *name;                          // e.g. "/dev/lamp"
    int  (*open)(void);                        // most devices: no-op, return 0
    void (*close)(void);                       // most devices: no-op
    int  (*read)(char *buf, int len);           // returns bytes written to buf, or -1
    int  (*write)(const char *buf, int len);    // returns bytes consumed, or -1
} device_t;

// Register a device. Call before fs_open() can find it (typically from
// main(), once per device, at startup).
void fs_register(const device_t *dev);

// Look up a device by exact path and open it. Returns a small fd (>= 0)
// on success, -1 if no such device is registered or its open() fails.
int fs_open(const char *path);

void fs_close(int fd);

// Both return the number of bytes produced/consumed, or -1 on a bad fd.
int fs_read(int fd, char *buf, int len);
int fs_write(int fd, const char *buf, int len);

// For `ls`: iterate registered devices by index, 0..fs_count()-1.
// Returns the device's path, or NULL past the end.
int fs_count(void);
const char *fs_name(int index);
