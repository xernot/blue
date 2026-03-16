# blue

An interactive device management TUI for Linux. Manages Bluetooth devices, monitors WiFi with real speed tests, tracks network printer health, and shows system diagnostics — all in a full-height 4-pane terminal dashboard.

No libraries beyond libc, no ncurses, no dbus bindings. Just C and a terminal.

```
┌────────────────────────────────────────────────────────────────────────────────────────────────┐
│  blue — Device Management                                            trurl  Ubuntu 24.04.4 LTS │
├─────────────────────────────┬──────────────────────┬────────────────────────┬──────────────────┤
│  Bluetooth                  │  Network ●           │  Printer ●            │  System          │
│  Device    Status  Battery  │                      │  HP Color Laser MFP   │                  │
│  ────────────────────────── │  SSID   home         │  IP 192.168.68.60     │  trurl 99%⚡     │
│ ▸ xirs keyboard  Connected │  IP     192.168.68.62│                       │  Kernel 6.17.0.. │
│   xirs mouse     Connected │  Link   5187 Mbps    │  C ░░░░░░░░░░   0%   │  Uptime 2d 5h 49m│
│   JBL Charge 5   Paired    │  Signal -33 dBm      │  M ░░░░░░░░░░   0%   │  CPU      3%     │
│  ────────────────────────── │  Iface  wlp0s20f3    │  Y ██░░░░░░░░  22%   │  Memory  42%     │
│   66-87-7D-0E..  Discovered│                      │  K █░░░░░░░░░  10%   │  Disk    67%     │
│                             │  Speedtest           │                       │                  │
│                             │  ↓  209 Mbps         │  Health               │  Disk I/O        │
│                             │  ↑  47 Mbps          │  Imaging Unit    44%  │  Read    1.2 MB/s│
│                             │  Ping   21 ms  14:05 │  Fuser Life      55%  │  Write   0.5 MB/s│
│                             │                      │  Waste Toner    100%  │                  │
│                             │  ⊙ Testing...        │                       │  Temp    52°C    │
│                             │                      │                       │  Fan 1  3073 RPM │
│                             │                      │                       │  Fan 2  3073 RPM │
│                             │                      │                       │                  │
│                             │                      │                       │  Services        │
│                             │                      │                       │  ✓ All OK        │
├─────────────────────────────┴──────────────────────┴────────────────────────┴──────────────────┤
│  Speedtest ████████████░░░░░░░░ 62%  12s                                                      │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│  s Scan  S Speedtest  c Connect  d Disconnect  p Pair  t Trust  x Remove  r Refresh  q Quit   │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## Features

**Bluetooth** — Scan, connect, disconnect, pair, trust, and remove devices. Live battery levels with colored bar indicators. Vim-style navigation (j/k) alongside arrow keys. Confirmation prompts for destructive actions.

**Network** — WiFi SSID, IP, link speed, and signal strength (color-coded by dBm). Real internet speed test (download, upload, ping) runs in background — never blocks the UI. Auto-reruns every 10 minutes, or trigger manually with `S`.

**Speedtest** — Results cached to disk (`~/.cache/blue/speedtest`) and loaded on startup. Progress bar in the status bar shows elapsed time during a run. Spinner indicator in the network pane while testing.

**Printer** — Auto-discovers printers on the network via mDNS. CMYK toner levels with colored bars. Health section with imaging unit, fuser, and waste toner. Works on any network without configuration.

**System** — Hostname and OS in the header bar. System pane shows laptop battery with charging indicator, kernel version, uptime, live CPU/memory/disk usage, disk I/O throughput (read/write MB/s), CPU temperature, fan speeds, and failed systemd services.

## Install

### Dependencies

```bash
# Bluetooth
sudo apt install bluez

# Network info
sudo apt install iw iproute2

# Printer discovery & monitoring
sudo apt install avahi-utils snmp

# Speed test
sudo apt install speedtest-cli

# Battery (usually pre-installed)
sudo apt install upower
```

### Build

```bash
git clone https://github.com/xernot/blue.git
cd blue
make
```

That's it. No `./configure`, no `cmake`, no dependency fetching. Compiles with `-Wall -Wextra -pedantic -std=c11`.

```bash
make clean          # remove build artifacts
make CC=clang       # use a different compiler
```

### Run

```bash
./blue              # interactive TUI (minimum 100 columns, full terminal height)
```

## CLI Subcommands

```bash
./blue list                     # print all known Bluetooth devices
./blue scan                     # scan for 5 seconds, print results
./blue connect D6:21:42:4F:64:A1
./blue disconnect D6:21:42:4F:64:A1
./blue pair D6:21:42:4F:64:A1
./blue trust D6:21:42:4F:64:A1
./blue remove D6:21:42:4F:64:A1
./blue help
```

All action subcommands exit with `0` on success, `1` on failure — suitable for scripting.

## Keybindings

| Key | Action |
|-----|--------|
| `↑` / `k` | Move selection up |
| `↓` / `j` | Move selection down |
| `s` | Toggle Bluetooth scanning |
| `S` | Start speed test |
| `c` | Connect to selected device |
| `d` | Disconnect selected device |
| `p` | Pair with selected device |
| `t` | Trust selected device |
| `x` | Remove device (asks y/n) |
| `r` | Refresh device list |
| `q` | Quit (asks y/n) |

## Configuration

All tunables live in [`config.h`](config.h):

| Constant | Default | Description |
|----------|---------|-------------|
| `PRINTER_SNMP_COMMUNITY` | `"public"` | SNMP community string |
| `PRINTER_REFRESH_MS` | `60000` | Printer re-query interval |
| `PRINTER_MAX_SUPPLIES` | `16` | Max supply entries to track |
| `BT_REFRESH_MS` | `5000` | Bluetooth device list refresh |
| `NETWORK_REFRESH_MS` | `5000` | WiFi status refresh |
| `SPEEDTEST_INTERVAL_MS` | `600000` | Speed test auto-run (10 min) |
| `SPEEDTEST_EXPECTED_SEC` | `20` | Expected test duration (progress bar) |
| `SPEEDTEST_PROGRESS_MS` | `500` | Progress bar update interval |
| `SYSINFO_REFRESH_MS` | `1000` | CPU/mem/disk refresh |
| `HEALTH_MAX_FANS` | `4` | Max fan speed entries to track |
| `THERMAL_ZONE_PREFERRED` | `"x86_pkg_temp"` | Preferred thermal zone for CPU temp |
| `THERMAL_ZONE_FALLBACK` | `"acpitz"` | Fallback thermal zone |
| `SPEEDTEST_CMD` | `"speedtest.sh"` | Speed test command name |
| `HEALTH_REFRESH_MS` | `2000` | Disk I/O / temp / fans / services refresh |
| `UI_POLL_INTERVAL_US` | `50000` | Main loop poll (50ms) |

Speed test results are cached to `~/.cache/blue/speedtest` and loaded on startup.

## Architecture

```
main.c        — entry point, CLI parsing, event loop, key handling, timer management
bt.c/h        — Bluetooth backend (bluetoothctl + upower subprocesses)
ui.c/h        — TUI rendering (raw ANSI escape codes, 4-pane layout, full height, buffered output)
printer.c/h   — Printer status via SNMP (auto-discovered via avahi mDNS)
network.c/h   — WiFi status (iw + ip subprocesses)
speedtest.c/h — Background speed test (fork + speedtest --simple, disk cache)
sysinfo.c/h   — System stats (/proc, /sys, statvfs)
health.c/h    — System health (OS, kernel, uptime, disk I/O, CPU temp, fan speed, failed services)
device.h      — shared Device struct
config.h      — all configurable constants
```

### Design decisions

**No ncurses.** Raw ANSI escape codes and `termios` directly. Zero dependency on terminal libraries.

**Full terminal height.** The TUI fills the entire terminal. Pane body rows expand dynamically.

**Buffered rendering.** Entire frame is buffered (64KB) and flushed atomically. Per-line erase (`\033[2K`) instead of screen clear (`\033[2J`) prevents visible flicker.

**No libdbus / libsnmp / libnl.** All external data comes via subprocess calls (`popen`, `fork`+`exec`). This avoids heavy C library dependencies entirely.

**Single-threaded event loop.** Non-blocking stdin with 50ms poll. Independent timers drive each data source at different intervals.

**Non-blocking speed test.** The `speedtest.sh --simple` command takes ~20 seconds. It runs in a `fork()`ed child process with a pipe — the UI stays fully responsive. Results are cached to disk so they persist across restarts. Progress bar switches to a spinner when exceeding the expected duration.

**Printer auto-discovery.** `avahi-browse _ipp._tcp` finds printers via mDNS/Bonjour. The discovered IP is cached; if the printer goes offline, re-discovery happens on the next refresh cycle. No hardcoded IPs.

**Disk I/O throughput.** Computed by reading `/proc/diskstats` and calculating sector deltas between reads. Detects nvme and sd devices automatically.

**CPU temperature & fans.** Temperature from `/sys/class/thermal/` (prefers `x86_pkg_temp`, falls back to `acpitz`). Fan speeds from `/sys/class/hwmon/*/fan*_input` (virtual ACPI fans filtered out).

**Platform stubs.** All Linux-specific code lives behind `#ifdef __linux__`. Other platforms get compilable stubs that return "not supported".

### Data sources

| Data | Source |
|------|--------|
| Bluetooth devices | `bluetoothctl devices` + `bluetoothctl info <addr>` |
| Device battery | BlueZ Battery Service + `upower -i` fallback |
| WiFi info | `iw dev <iface> link` + `ip -4 addr show` |
| Speed test | `speedtest.sh --simple` (forked background process) |
| Speed test cache | `~/.cache/blue/speedtest` |
| Printer discovery | `avahi-browse -rpt _ipp._tcp` (mDNS) |
| Printer supplies | `snmpget`/`snmpwalk` via Printer MIB OIDs |
| OS name | `/etc/os-release` (PRETTY_NAME) |
| Kernel version | `uname()` syscall |
| Uptime | `/proc/uptime` |
| CPU usage | `/proc/stat` (delta between reads) |
| Memory usage | `/proc/meminfo` (MemTotal - MemAvailable) |
| Disk usage | `statvfs("/")` |
| Disk I/O | `/proc/diskstats` (sector delta) |
| CPU temperature | `/sys/class/thermal/thermal_zone*/temp` |
| Fan speed | `/sys/class/hwmon/*/fan*_input` |
| Battery | `/sys/class/power_supply/BAT0/capacity` |
| Failed services | `systemctl --failed --no-legend --no-pager` |

## Known Limitations

- **Charging state not available for all BT devices.** The Bluetooth Battery Service only exposes percentage — no charging field. Some devices report charging through upower, but many (e.g. Logitech mice) do not.

- **Battery unavailable for some devices.** Devices without Bluetooth Battery Service (e.g. JBL speakers) show `—` instead of a bar.

- **Printer requires SNMP.** The printer must respond to SNMP v1 queries. Most network printers do, but some may have SNMP disabled.

- **Speed test requires internet.** The `speedtest` command connects to Ookla servers. If offline, it simply shows no result.

- **Speed test phase detection not possible.** `speedtest --simple` outputs all results at once when done, not incrementally per phase. The progress bar is time-based, not phase-based.

- **Minimum terminal width: 100 columns.** The 4-pane layout needs room. Narrower terminals will be clamped to 100.

- **Linux only.** All backends require Linux-specific tools and paths. Other platforms compile but return stubs.

## License

This project is unlicensed. Do whatever you want with it.
