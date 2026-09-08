# picogateway

An experimental, Plan 9-flavored rebuild of a working LoRa-to-ThingSpeak
field gateway (a Raspberry Pi Pico W: 3 room sensors, up to 3 remote
LoRa soil probes, a 5-button front panel, an OLED status/config menu,
an EEPROM for settings, and periodic cloud uploads), converted onto the
same cooperative-multitasking architecture as its sibling project,
[picoos](https://github.com/leonhiem/picoos) — itself rebuilt from
[babywarmer](https://github.com/leonhiem/babywarmer). The original
hand-rolled `gateway.cpp` (one giant blocking `while(1)`: OLED menu,
wifi connect, HTTP client, LoRa receive loop, all serial) is gone from
this file, one subsystem at a time, hardware-confirmed at every step —
it's not lost, it's in this repo's own git history (every commit
through the step this README describes).

No hardcoded control loop. No `ioctl()`. Everything is a file: `open`,
`close`, `read`, `write`. Each subsystem — LoRa, wifi, the OLED, the
ThingSpeak uploader — is a small, independently restartable piece, not
a step baked into `main()`.

## Philosophy

- **Everything is a file.** Hardware (`/dev/room/temp`, `/dev/lora/probe1/temp`,
  `/dev/config/wifi_ssid`, ...) and small programs (`bin/cat`,
  `bin/lora`, ...) are both just named, `open`able things — `ls` lists
  them together, in one namespace.
- **Bring-up is a job, not a step in `main()`.** `main()` only starts
  the "reliable" local hardware unconditionally (buttons, room
  sensors, the config EEPROM) — nothing with external state to lose.
  LoRa, wifi, and the OLED each get one non-blocking, rate-limited
  `_try_start()` function, called only from a `bin/` program
  (`lora &`/`wifi &`/`display &`) backgrounded from a boot script, not
  a blocking call in `main()`. If one of them ever drops (a bad LoRa
  packet, a dropped wifi link), it just goes back to "down" and the
  same background job's next tick notices and restarts it — no
  separate watchdog needed for that any more.
- **The device layer validates so callers don't have to.** `/dev/config/*`
  always null-terminates and drops non-printable bytes before a write
  ever reaches the EEPROM. The OLED menu's config editor (below) edits
  every field purely through `fs_open`/`fs_read`/`fs_write` on those
  devices — there's no hand-rolled array indexed by a menu constant
  anywhere in this codebase, so there's no equivalent of the original's
  `apis[-1]` off-by-one left to have.
- **Cooperative, not preemptive.** One core, one scheduler
  (`kernel/task.h`), tasks that run briefly and return. "Blocking"
  (an HTTP request, a button-driven menu, a job waiting to retry) is
  built as resumable state checked on each task's own tick, never a
  real blocking call.
- **Stay in Unix vocabulary, not Plan 9's own jargon** — `open`/
  `close`/`read`/`write`, not `Chan`.

## Building

Slackware-specific toolchain paths (adjust for your system):

```sh
export PICO_SDK_PATH=/home/leon/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=/home/leon/pico/pico-sdk/toolchain/gcc-arm-none-eabi-10.3-2021.10
export CMAKE_FIND_ROOT_PATH=$PICO_TOOLCHAIN_PATH
export CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
export CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
export CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY

mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

This also expects `../LoRa-pi-pico`, `../LoRa-pi-pico-src`, and
`../pico_dht` to exist as sibling directories next to this repo (see
`CMakeLists.txt`'s own `add_subdirectory()`/`PICO_LORA_PATH` lines) —
the LoRa radio driver and the DHT22 room-sensor library, vendored
separately rather than copied in.

Building produces several targets: `gateway` is the real firmware
(`gateway.uf2`); `demo_sched`/`demo_klog`/`demo_fs`/`demo_shell`/
`demo_room`/`demo_config`/`demo_lora` are the small, standalone proof
binaries each migration step was hardware-confirmed on before its code
became part of `gateway`, kept around rather than deleted. Flash
`build/gateway.uf2` the usual way (hold BOOTSEL, plug in USB-C, copy
the file onto the mass-storage device that appears). Console is the
same USB-C connection, standard serial terminal, 115200 baud (or just
USB CDC — no specific baud rate is enforced).

## Using the shell

Connect a serial terminal to the board. You'll get a `%` prompt (an
`rc`/Plan 9 nod):

```
picogateway
  ls            list /dev and bin/
  jobs, kill    manage background pipelines
  script, run   record and replay command sequences
  loop          like run, but repeats forever, Ctrl-C to stop
  watch         repeat a pipeline every <ms>, Ctrl-C to stop
running 'boot' automatically...
%
```

Grammar:

```
stage [| stage ...] [< path] [> path] [&]
stage := progname [arg...]
```

- `ls` — list every device and program.
- `cat <path>` — read a device once (or pass piped input through
  unchanged if no path given).
- `echo <text...> > <path>` — write to a device.
- `prog1 | prog2 | ...` — pipe a program's output into the next stage.
- `< path`, `> path` — read a device as the first stage's input /
  write the last stage's output to a device, instead of the terminal.
- `... &` — run the pipeline as a background job instead of once. This
  doesn't fork a process (there isn't one) — it registers the pipeline
  as a real periodic task, polled every `JOB_POLL_MS` (150ms).
- `jobs` — list running background jobs.
- `kill %<id>` — stop one.
- `sleep <ms>` — pause the shell (doesn't waste CPU; everything else
  keeps running).
- `script <name>` — capture the lines you type next into a named
  script, until a line containing just `.`. Prompt becomes `> ` while
  capturing.
- `run <name>` — replay a captured script, one line per shell tick.
  `sleep` inside a script really waits, and `&` inside a script
  backgrounds a job exactly like typing it directly.
- `loop <name>` — like `run`, but restarts from the top instead of
  stopping, forever until **Ctrl-C**.
- `scripts` — list captured script names.
- `watch <ms> <stage> [| stage ...] [< path]` — repeats a pipeline in
  the foreground every `<ms>`, printing every time, until **Ctrl-C**
  stops it.

`jobs`, `kill`, `sleep`, `script`, `run`, `loop`, `scripts`, and `watch`
are **shell builtins**, not entries in the device/program namespace —
they touch the shell's own state rather than being a text-in/text-out
program, so they don't appear in `ls`'s listing below. Scripts you
`script`/capture yourself are RAM-only — they don't survive a reboot.
One script, `boot`, is the exception: it's baked into the firmware
image itself (`BOOT_SCRIPT_TEXT` in `shell.cpp`), so it's back fresh
after every reboot with nothing to retype, and **it runs automatically**,
before the first prompt ever appears:

```
lora &
wifi &
display &
```

`thingspeak` isn't in there — unlike LoRa/wifi/the OLED, there's no
hardware to bring up for it, just a `/dev/wifi/status` check its own
always-on task makes every tick, so it's simply always running (see
[Status](#status-what-actually-runs-right-now)).

Backspace works. Unknown commands, bad paths, and rejected writes
print a short error instead of doing something silently wrong.

## Devices (`/dev/...`)

| Device | R/W | What it does |
|---|---|---|
| `/dev/buttons` | read | Names of buttons pressed since last read (e.g. `up down`), read-and-clear, **all buttons at once**. |
| `/dev/buttons/<name>` | read | One button only (`up`, `down`, `left`, `right`, `enter`), read-and-clear, independent of the others. |
| `/dev/room/temp` | read | Room temperature, °C (DHT22). Cached, refreshed every ~10s. |
| `/dev/room/hum` | read | Room humidity, % (DHT22). Same cache. |
| `/dev/room/light` | read | Room light level, % (ADC + LM358 opamp). Same cache. |
| `/dev/config/wifi_ssid` | read/write | Wifi SSID, EEPROM-backed. Non-printable bytes dropped on write; reported empty (not garbage) if the slot is ever corrupt. |
| `/dev/config/wifi_pass` | read/write | Wifi password, same EEPROM backing/validation. |
| `/dev/config/url` | read/write | ThingSpeak host (also the HTTP `Host:` header) — just a hostname, e.g. `api.thingspeak.com`, not a full URL. |
| `/dev/config/farm` | read/write | Farm name, shown on the OLED banner. |
| `/dev/config/api0` | read/write | ThingSpeak write-API key for the local room-sensor channel. |
| `/dev/config/api1`/`api2`/`api3` | read/write | ThingSpeak write-API key for LoRa probe 1/2/3's channel. |
| `/dev/lora/status` | read | `up`/`down` — is the LoRa radio actually running right now. |
| `/dev/lora/probe<1-3>/temp` | read | Probe's last-reported soil temperature, °C. `0.0` before its first packet ever. |
| `/dev/lora/probe<1-3>/soil` | read | Probe's last-reported soil moisture, %. |
| `/dev/lora/probe<1-3>/ph` | read | Probe's last-reported soil pH. |
| `/dev/lora/probe<1-3>/rssi` | read | Signal strength of the probe's last packet. |
| `/dev/lora/probe<1-3>/age` | read | Seconds since that probe's last packet, or `-1` if never heard from. |
| `/dev/lora/probe<1-3>/seen` | read | `1` once this probe has ever reported, else `0` — the explicit way to tell "never heard from" apart from "genuinely reported 0.0". |
| `/dev/wifi/status` | read | `down`/`connecting`/`up`. |
| `/dev/display/status` | read | `up`/`down` — has the OLED actually been brought up. |
| `/dev/display/mode` | read/write | `banner`/`menu` — which screen is showing. Write to force a switch (e.g. `echo banner > /dev/display/mode`) without waiting for a button press. |
| `/dev/thingspeak/status` | read | `idle`/`sending`/`ok`/`error` — outcome of the most recently attempted upload (room or LoRa, whichever ran last). |

## Programs (`bin/...`, run from the shell as bare names)

| Program | Usage | What it does |
|---|---|---|
| `cat` | `cat [path]` | Read a device, or pass piped input through unchanged if no path given. |
| `echo` | `echo <words...>` | Print its arguments, space-joined. |
| `ls` | `ls` | List every device and program. |
| `lora` | `lora` | One non-blocking attempt to bring up (or confirm) the LoRa radio. Meant to run as `lora &` from the boot script; safe to re-run by hand any time. |
| `wifi` | `wifi` | One non-blocking connect attempt/status check, using `/dev/config/wifi_ssid`+`wifi_pass`. Meant to run as `wifi &`; safe to re-run by hand. |
| `display` | `display` | One non-blocking attempt to bring up the OLED. Meant to run as `display &`; nothing draws to the panel before this has run once. |

There's no `bin/thingspeak` — see [Using the shell](#using-the-shell)
for why.

## Recipes

Poke at hardware and config directly:
```
cat /dev/room/temp
cat /dev/lora/probe1/soil
echo myssid > /dev/config/wifi_ssid
echo mypassword > /dev/config/wifi_pass
```

Force the OLED back to the status banner without touching a button:
```
echo banner > /dev/display/mode
```

Kick a subsystem by hand (same non-blocking call the boot script uses,
just run in the foreground so you see the result immediately):
```
wifi
lora
```

Watch a live value once a second, Ctrl-C to stop:
```
watch 1000 cat /dev/room/temp
```

Check whether the last ThingSpeak upload actually made it out:
```
cat /dev/thingspeak/status
```

Manage what's running:
```
jobs
kill %1
```

Pause without wasting CPU (everything else keeps running):
```
sleep 3000
```

Record and replay a sequence of commands (RAM-only, lost on reboot):
```
script demo
> echo banner > /dev/display/mode
> sleep 2000
> echo menu > /dev/display/mode
> .
run demo
```

## Status: what actually runs right now

`main()` starts buttons, room sensors, and the config EEPROM
unconditionally — none of them have external state to lose, so none of
them need to be restartable, they just come up and stay up.

LoRa, wifi, and the OLED display each follow the same shape: a
non-blocking, rate-limited `_try_start()`/`_try_connect()` function
(`lora.cpp`/`wifi.cpp`/`display.cpp`), called only from a `bin/`
program backgrounded off the boot script (`lora &`/`wifi &`/
`display &`), plus an always-on kernel task registered unconditionally
that no-ops until that subsystem is actually up. A dropped LoRa link,
a bad packet, or a lost wifi association all just fall back to "down" —
the same background job's next tick notices and restarts things on its
own, no separate watchdog required for any of it.

The OLED (`display.cpp`) is one task, not two — unlike picoos's ST7735
display (a data-sourcing job plus a separate always-on flush task),
the menu's cursor position and which screen is showing don't exist
anywhere except *as* display state, so navigation and rendering stay
together. It dispatches between a live status banner (farm name,
wifi/LoRa status) and an interactive menu: **Room sensors** / **LoRa
probes** / **Wifi status** (read-only, five lines max — a title row
plus four content rows, this panel's whole visible height) and
**Config**, which can edit all 8 EEPROM fields (wifi SSID/password,
server URL, farm name, 4 API keys) through the same char-by-char
editor the original had (LEFT/RIGHT moves the cursor, UP/DOWN cycles
the character, one more DOWN past space cuts the string there) — but
routed entirely through `fs_open`/`fs_read`/`fs_write` on
`/dev/config/*`, which is what retires the original's `apis[-1]`
off-by-one: there's no array indexed by a menu constant left to have
one in. The cut point (a bare NUL, which used to render identically to
a trailing space) now draws as a visible `_`, with a one-line hint
("DOWN here = cut") that only appears while the cursor actually sits
on a space. `LEFT` is "back" in every list screen — otherwise unused
there — so no menu is a dead end.

`thingspeak.cpp` uploads to ThingSpeak on a fixed schedule, gated on
`/dev/wifi/status` reading `up` (skipped quietly otherwise, no retry
storm): the room sensors every 60s to the channel configured at
`/dev/config/api0`, and one LoRa probe's cached values — round-robin
over whichever of probe 1/2/3 have actually ever reported — every 60s,
offset 30s from the room upload. The HTTP client itself is a small
non-blocking GET built on lwIP's `tcp_pcb` callbacks and
`dns_gethostbyname()` (the original's own primitives, just no longer
behind a blocking `sleep_ms()` loop), with a `tcp_poll()` backstop that
fails a stuck request out after a few seconds rather than ever hanging
the scheduler — which is also why the original's watchdog stays
disabled: the thing it existed to recover from doesn't exist here any
more.

## Architecture, briefly

- `kernel/task.h` — the cooperative scheduler. `task_register(name, fn, period_ms)`, `task_run()`.
- `kernel/fs.h` — the device namespace. `device_t{name, open, close, read, write}`, one flat table.
- `kernel/prog.h` — the program registry, same shape as `kernel/fs.h` but for text-in/text-out filters instead of hardware.
- `kernel/klog.h` — serialized cross-task logging; tasks call `klog()`, one dedicated task drains it with `klog_flush()`.
- `jobs.h`/`jobs.cpp` — pipeline execution (`|`/`<`/`>`) and background job control (`&`/`jobs`/`kill`), built entirely on top of `kernel/task.h`.
- `shell.cpp` — `task_shell`, the interactive `%` prompt, plus `script`/`run`/`loop`/`watch` and the boot-time auto-run of `BOOT_SCRIPT_TEXT`.
- `buttons.h`/`room.h`/`eeprom.h`/`lora.h`/`wifi.h`/`display.h`/`thingspeak.h` — one owner module per subsystem (hardware/state + the non-blocking bring-up/step function); each has a matching `dev/*.cpp` turning its state into `/dev/*` devices.
- `dev/*.cpp`, `prog/*.cpp` — one file per device group/program, each self-contained.
- `ssd1306.h`/`ssd1306.cpp` — vendored SSD1306 OLED driver (MIT-licensed), the only file `display.cpp` touches directly for the actual panel I/O.
- `../LoRa-pi-pico`, `../LoRa-pi-pico-src` — the vendored LoRa radio library (sibling directory, not copied in).
- `../pico_dht` — the vendored DHT22 library (sibling directory).

Known trade-offs and deliberate non-fixes live in the source comments
where they're relevant — e.g. `jobs.h`'s one shared `JOB_POLL_MS` for
every background job, or `gateway.cpp`'s own note on exactly why the
watchdog stays disabled.

## License

This project's own code is [MIT licensed](LICENSE). Everything it
builds on is permissive too, so nothing here needs anything other than
MIT:

- `ssd1306.h`/`ssd1306.cpp` are vendored from David Schramm's
  [pico-ssd1306](https://github.com/daschr/pico-ssd1306) (MIT) — the
  original copyright notice is kept in both files.
- [`LoRa-pi-pico`](https://github.com/akshayabali/LoRa-pi-pico) (MIT,
  Akshaya Bali) and [`pico_dht`](https://github.com/vmilea/pico_dht)
  (MIT) are sibling repos linked via `add_subdirectory()`, not copied
  in — see [Building](#building).
- The Raspberry Pi Pico SDK, lwIP, and the cyw43 wifi driver are all
  BSD-3-Clause-licensed.
