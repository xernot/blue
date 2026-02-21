# blue

An interactive Bluetooth device manager for the Linux terminal.

`blue` is a lightweight TUI application that scans, lists, connects, and manages Bluetooth devices through `bluetoothctl` — no libraries beyond libc, no ncurses, no dbus bindings. Just a C program and a terminal.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                                                                              │
│  blue — Bluetooth Device Manager          trurl  99%  cpu  6%  mem 40%  dsk 11%
│                                                                              │
├─────────────────────────────────────────────────────┬────────────────────────┤
│  Device               Status        Battery         │                        │
│  ─────────────────────────────────────────────────  │        . . .           │
│ ▸ xirs keyboard       Connected     80% ████████░░  │                        │
│   JBL Charge 5        Connected      —              │     (          )       │
│   xirs mouse          Connected     75% ███████░░░  │    ((          ))      │
│   mouse office        Paired         —              │   (((    .     )))     │
│   keyboard office     Paired         —              │    ((          ))      │
│   JBL WAVE FLEX       Paired         —              │     (          )       │
│  ─────────────────────────────────────────────────  │                        │
│   66-87-7D-0E-2D-18   Discovered     —              │        . . .           │
│   73-89-76-CB-AB-DD   Discovered     —              │                        │
│                                                     │   xirs bluetooth       │
│                                                     │       handler          │
│                                                     │                        │
├─────────────────────────────────────────────────────┴────────────────────────┤
│  s Scan  c Connect  d Disconnect  p Pair  t Trust  x Remove                  │
│  r Refresh  q Quit  ↑↓ Navigate                                             │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Features

- **Interactive TUI** with box-drawing frame, color-coded status, and battery bars
- **Non-interactive CLI** subcommands for scripting (`blue list`, `blue connect <addr>`, ...)
- **Live system stats** in the header — hostname, battery, CPU, memory, and disk usage (updates every second)
- **Device grouping** — connected first, then paired, then discovered, with a visual separator
- **Confirmation prompts** for destructive actions (device removal)
- **User-friendly names** — reads BlueZ `Alias` field so your custom device names show up
- **Battery readout** from both BlueZ and upower (fallback), rendered as colored block bars
- **Terminal resize** handling (responds to SIGWINCH)
- **Zero dependencies** beyond libc — no ncurses, no dbus, no external libraries
- **Vim-style navigation** (j/k) alongside arrow keys

## Requirements

- **Linux** with BlueZ installed
- **bluetoothctl** (part of the `bluez-utils` or `bluez` package)
- A terminal with **UTF-8** and **ANSI color** support
- **GCC** or any C11-compatible compiler

Optional:
- **upower** — used as a battery fallback for devices that don't report battery through BlueZ

## Build

```bash
make
```

That's it. No `./configure`, no `cmake`, no dependency fetching. The Makefile compiles with `-Wall -Wextra -pedantic -std=c11`.

```bash
make clean          # remove build artifacts
make CC=clang       # use a different compiler
```

## Usage

### Interactive mode

```bash
./blue
```

Launches the full-screen TUI. Devices are listed with their connection status and battery level. The header shows live system information.

### Keybindings

| Key | Action |
|-----|--------|
| `↑` / `k` | Move selection up |
| `↓` / `j` | Move selection down |
| `s` | Toggle Bluetooth scanning |
| `c` | Connect to selected device |
| `d` | Disconnect selected device |
| `p` | Pair with selected device |
| `t` | Trust selected device |
| `x` | Remove device (asks for confirmation) |
| `r` | Refresh device list |
| `q` | Quit |

### CLI subcommands

```bash
./blue list                     # print all known devices
./blue scan                     # scan for 5 seconds, print results
./blue connect D6:21:42:4F:64:A1
./blue disconnect D6:21:42:4F:64:A1
./blue pair D6:21:42:4F:64:A1
./blue trust D6:21:42:4F:64:A1
./blue remove D6:21:42:4F:64:A1
./blue help
```

All action subcommands exit with `0` on success, `1` on failure — suitable for scripting.

## Architecture

```
main.c        Entry point, CLI parsing, event loop, signal handling
bt.c / bt.h   Bluetooth backend (bluetoothctl + upower subprocesses)
ui.c / ui.h   TUI rendering (raw ANSI escape codes, termios raw mode)
sysinfo.c/h   Live system stats (reads /proc, /sys, statvfs)
device.h      Shared Device struct
Makefile      Build system
```

### Design decisions

**No ncurses.** The TUI uses raw ANSI escape codes and `termios` directly. This eliminates the ncurses dependency entirely — `blue` compiles with nothing but a C compiler and libc.

**No libdbus.** Bluetooth operations go through `bluetoothctl` and `upower` as subprocesses via `popen()`. This avoids the complexity of the D-Bus C API and its transitive dependencies.

**Single-threaded event loop.** Non-blocking stdin (`O_NONBLOCK` + `fcntl`) with a 50ms poll interval. Two independent timers drive updates:
- **1 second** — system stats (CPU, memory, disk, battery)
- **5 seconds** — Bluetooth device list refresh

**Platform stubs.** All Linux-specific code (`bluetoothctl`, `/proc`, `/sys`) lives behind `#ifdef __linux__`. Other platforms get compilable stubs that return "not supported" — the project builds everywhere, even if Bluetooth only works on Linux.

### How it reads device info

1. `bluetoothctl devices` — enumerates all known devices
2. `bluetoothctl info <addr>` — reads `Alias`, `Paired`, `Connected`, `Trusted`, `Blocked`, `Battery Percentage` for each device
3. `upower -e` + `upower -i <path>` — battery fallback for devices that don't expose battery through BlueZ (cached per refresh cycle to avoid redundant subprocess calls)

### System stats sources

| Stat | Source |
|------|--------|
| Hostname | `gethostname()` |
| Battery | `/sys/class/power_supply/BAT0/capacity` |
| CPU | `/proc/stat` (delta between reads) |
| Memory | `/proc/meminfo` (`MemTotal` - `MemAvailable`) |
| Disk | `statvfs("/")` |

## TUI layout

The interface is split into sections within a box-drawing frame:

- **Header** — application title on the left, live system stats on the right (hostname, battery %, CPU %, memory %, disk %)
- **Device list** (left panel) — grouped and sorted: connected devices first, then paired, then discovered. A thin separator line divides known devices from newly discovered ones. Selected device is highlighted with `▸`
- **Art panel** (right panel) — Bluetooth signal art and branding, separated by a vertical divider with `┬`/`┴` junctions
- **Status bar** — feedback messages ("Connecting to...", "Remove cancelled") and confirmation prompts
- **Keybindings** — always-visible reference at the bottom

Colors indicate status at a glance:
- **Green** — connected / healthy
- **Yellow** — paired / moderate
- **Red** — low battery / high resource usage
- **Dim** — discovered / inactive

## Files

| File | Description |
|------|-------------|
| `main.c` | Entry point, argument parsing, interactive event loop, device sorting, signal handling |
| `bt.c` | Bluetooth backend — device enumeration, connect/disconnect/pair/trust/remove via `bluetoothctl` |
| `bt.h` | Bluetooth API (9 functions) |
| `ui.c` | Full-screen TUI — raw mode, ANSI rendering, box-drawing frame, battery bars, ASCII art panel |
| `ui.h` | UI API and key code constants |
| `sysinfo.c` | System info reader — CPU, memory, disk, battery from procfs/sysfs |
| `sysinfo.h` | SysInfo struct |
| `device.h` | Device struct shared across modules |
| `Makefile` | Build system (make / make clean) |
| `battery_status.c` | Legacy standalone battery utility (not part of main build) |

## Known Limitations

- **Charging state not available for all devices.** The Bluetooth Battery Service (UUID 0x180F) only exposes battery percentage — there is no field for charging state. Some devices (like phones or headphones) report charging through upower, but many (notably Logitech mice) do not. Their firmware simply doesn't send that data over Bluetooth. Even vendor tools like Logitech Options+ detect charging via proprietary USB HID, not Bluetooth. For these devices, `blue` cannot show the ⚡ charging indicator.

- **Battery percentage unavailable for some devices.** Devices that don't implement the Bluetooth Battery Service (e.g. JBL speakers) will show `—` instead of a battery bar. This is a device firmware limitation.

- **Linux only.** The Bluetooth backend requires `bluetoothctl` (BlueZ). Other platforms compile but all BT functions return "not supported".

## License

This project is unlicensed. Do whatever you want with it.
