#ifndef CONFIG_H
#define CONFIG_H

/* ── ANSI color escape codes ─────────────────────
 * Used throughout the TUI for styling text.       */
#define C_RESET "\033[0m"      /* Reset all attributes */
#define C_BOLD "\033[1m"       /* Bold text */
#define C_DIM "\033[2m"        /* Dim/faint text */
#define C_GREEN "\033[32m"     /* Green foreground */
#define C_RED "\033[31m"       /* Red foreground */
#define C_YELLOW "\033[33m"    /* Yellow foreground */
#define C_CYAN "\033[36m"      /* Cyan foreground */
#define C_BGREEN "\033[1;32m"  /* Bold green */
#define C_BYELLOW "\033[1;33m" /* Bold yellow */
#define C_BCYAN "\033[1;36m"   /* Bold cyan */

/* ── Box-drawing characters (UTF-8) ──────────────
 * Used to render pane borders and dividers.       */
#define B_H "\xe2\x94\x80"  /* ─  horizontal */
#define B_V "\xe2\x94\x82"  /* │  vertical */
#define B_TL "\xe2\x94\x8c" /* ┌  top-left corner */
#define B_TR "\xe2\x94\x90" /* ┐  top-right corner */
#define B_BL "\xe2\x94\x94" /* └  bottom-left corner */
#define B_BR "\xe2\x94\x98" /* ┘  bottom-right corner */
#define B_ML "\xe2\x94\x9c" /* ├  middle-left junction */
#define B_MR "\xe2\x94\xa4" /* ┤  middle-right junction */
#define B_TM "\xe2\x94\xac" /* ┬  top-middle junction */
#define B_BM "\xe2\x94\xb4" /* ┴  bottom-middle junction */

/* ── Network pane row indices ────────────────────
 * Offsets within the network pane body.           */
#define NET_HDR 0        /* Pane header ("Network ●") */
#define NET_SSID 2       /* WiFi SSID */
#define NET_IP 3         /* IPv4 address */
#define NET_SPEED 4      /* Link speed in Mbps */
#define NET_SIGNAL 5     /* Signal strength in dBm */
#define NET_IFACE 6      /* Network interface name */
#define NET_ST_HDR 9     /* Speedtest section header */
#define NET_DL 10        /* Download speed */
#define NET_UL 11        /* Upload speed */
#define NET_PING 12      /* Ping latency */
#define NET_ST_TIME 13   /* Last speedtest date/time */
#define NET_ST_RUN 14    /* Speedtest running indicator */
#define NET_GRAPH_HDR 16 /* History graph header */
#define NET_GRAPH_TOP 17 /* Top row of braille graph */
#define NET_GRAPH_BOT 18 /* Bottom row of braille graph */

/* ── Printer pane row indices ────────────────────
 * Offsets within the printer pane body.           */
#define PR_HDR 0      /* Pane header ("Printer ●") */
#define PR_MODEL 1    /* Printer model name */
#define PR_IP 2       /* Printer IP address */
#define PR_TONER 4    /* First toner row (4 toner rows: 4-7) */
#define PR_HEALTH 9   /* Health section header */
#define PR_HSUPPLY 10 /* First health supply row */
#define PR_BK_LINE 14 /* Divider line before backup section */
#define PR_BK_HDR 15  /* "Last Backup" header */
#define PR_BK_DATE 16 /* Backup date/time */
#define PR_BK_AGE 17  /* Backup age ("3d 2h ago") */

/* ── System health pane row indices ──────────────
 * Offsets within the system pane body.            */
#define SH_HDR 0     /* Pane header ("System") */
#define SH_HOST 2    /* Hostname + battery */
#define SH_KERNEL 3  /* Kernel version */
#define SH_UPTIME 4  /* System uptime */
#define SH_CPU 5     /* CPU usage percentage */
#define SH_MEM 6     /* Memory usage percentage */
#define SH_DISK 7    /* Disk usage percentage */
#define SH_DISKIO 9  /* "Disk I/O" section header */
#define SH_DREAD 10  /* Disk read throughput */
#define SH_DWRITE 11 /* Disk write throughput */
#define SH_TEMP 13   /* CPU temperature */
#define SH_FAN 14    /* First fan speed row */

/* Minimum pane body rows (must fit sparkline graph + backup) */
#define MIN_PANE_ROWS 19

/* Vertical levels per braille character pair (2 text rows × 4 dots) */
#define GRAPH_DOT_ROWS 8

/* ── Pane widths ─────────────────────────────────
 * Outer widths of the right three panes.
 * BT pane fills the remaining terminal width.     */
#define PANE_SYS_WIDTH 24 /* System health pane width */
#define PANE_PR_WIDTH 26  /* Printer pane width */
#define PANE_NET_WIDTH 24 /* Network pane width */

/* ── Terminal dimensions ─────────────────────────
 * Minimum sizes to render properly; defaults
 * used when ioctl fails.                          */
#define MIN_TERM_COLS 100    /* Minimum terminal columns */
#define MIN_TERM_ROWS 16     /* Minimum terminal rows */
#define DEFAULT_TERM_COLS 80 /* Default columns when ioctl fails */
#define DEFAULT_TERM_ROWS 24 /* Default rows when ioctl fails */

/* ── Buffer sizes ────────────────────────────────
 * Shared buffer sizes for command strings,
 * line reads, and path construction.              */
#define CMD_BUF 1024        /* Command string buffer */
#define LINE_BUF 512        /* Single-line read buffer */
#define PATH_BUF 512        /* File path buffer */
#define MAX_UPOWER_PATHS 64 /* Max cached upower device paths */
#define SECTOR_BYTES 512    /* Disk sector size for I/O calculation */
#define FRAME_BUF_SIZE                                                         \
  (64 * 1024) /* Frame buffer for flicker-free rendering                       \
               */

/* ── SNMP OIDs (Printer MIB) ────────────────────
 * Standard OIDs for printer supply monitoring.    */
#define OID_DESCR "1.3.6.1.2.1.1.1.0" /* sysDescr (model info) */
#define OID_SUPPLY_NAME                                                        \
  "1.3.6.1.2.1.43.11.1.1.6" /* prtMarkerSuppliesDescription */
#define OID_SUPPLY_MAX                                                         \
  "1.3.6.1.2.1.43.11.1.1.8" /* prtMarkerSuppliesMaxCapacity */
#define OID_SUPPLY_LEVEL                                                       \
  "1.3.6.1.2.1.43.11.1.1.9" /* prtMarkerSuppliesLevel                          \
                             */

/* ── System paths ────────────────────────────────
 * Paths to sysfs/procfs entries read by the TUI.  */
#define PATH_BAT0_CAPACITY "/sys/class/power_supply/BAT0/capacity"
#define PATH_BAT1_CAPACITY "/sys/class/power_supply/BAT1/capacity"
#define PATH_AC_ONLINE "/sys/class/power_supply/AC/online"
#define PATH_PROC_STAT "/proc/stat"
#define PATH_PROC_MEMINFO "/proc/meminfo"
#define PATH_PROC_UPTIME "/proc/uptime"
#define PATH_PROC_DISKSTATS "/proc/diskstats"
#define PATH_OS_RELEASE "/etc/os-release"
#define PATH_SYS_THERMAL "/sys/class/thermal"
#define PATH_SYS_HWMON "/sys/class/hwmon"
#define PATH_SYS_NET "/sys/class/net"
#define DCONF_BACKUP_KEY "/org/gnome/deja-dup/last-backup"

/* ── Command templates ───────────────────────────
 * Shell commands run via popen(). Format strings
 * use %s for addresses, interfaces, OIDs, etc.    */
#define CMD_BTCTL_VERSION "bluetoothctl --version 2>/dev/null"
#define CMD_BTCTL_DEVICES "bluetoothctl devices 2>/dev/null"
#define CMD_BTCTL_SCAN_ON "timeout -k 1 1 bluetoothctl scan on >/dev/null 2>&1"
#define CMD_BTCTL_SCAN_OFF                                                     \
  "timeout -k 1 1 bluetoothctl scan off >/dev/null 2>&1"
#define CMD_BTCTL_ACTION "bluetoothctl %s %s 2>&1"
#define CMD_BTCTL_INFO "bluetoothctl info %s 2>/dev/null"
#define CMD_UPOWER_ENUM "upower -e 2>/dev/null"
#define CMD_UPOWER_INFO "upower -i '%s' 2>/dev/null"
#define CMD_IW_LINK "iw dev %s link 2>/dev/null"
#define CMD_IP_ADDR "ip -4 addr show %s 2>/dev/null"
#define CMD_AVAHI_BROWSE                                                       \
  "timeout 3 avahi-browse -rpt _ipp._tcp 2>/dev/null | grep '^='"
#define CMD_SNMPGET "snmpget -v 1 -c %s -Oqv %s %s 2>/dev/null"
#define CMD_SNMPWALK "snmpwalk -v 1 -c %s -Oqv %s %s 2>/dev/null"
#define CMD_SYSTEMCTL_FAILED                                                   \
  "systemctl --failed --no-legend --no-pager 2>/dev/null"
#define CMD_DCONF_BACKUP "dconf read %s 2>/dev/null"

/* ── Printer (SNMP) ───────────────────────────────
 * Auto-discovered via mDNS (avahi-browse), then
 * queried via snmpget/snmpwalk.
 * Requires: sudo apt install avahi-utils snmp       */
#define PRINTER_SNMP_COMMUNITY "public"

/* Maximum number of printers to discover via mDNS.
 * Cycle through them with the P key.                */
#define PRINTER_MAX_DISCOVERED 8

/* How often to re-query the printer (milliseconds).
 * SNMP queries are cheap but no need to hammer it.  */
#define PRINTER_REFRESH_MS 60000

/* Maximum number of supply entries to track
 * (toners, imaging unit, belts, etc.)               */
#define PRINTER_MAX_SUPPLIES 16

/* ── Bluetooth refresh ────────────────────────────
 * How often to re-query bluetoothctl (milliseconds) */
#define BT_REFRESH_MS 5000

/* How often to refresh BT device list during active scan.
 * Faster than normal to pick up newly discovered devices. */
#define BT_SCAN_REFRESH_MS 2000

/* ── Network refresh ──────────────────────────────
 * How often to re-query wifi status (milliseconds)  */
#define NETWORK_REFRESH_MS 5000

/* ── Speedtest ────────────────────────────────────
 * Speedtest is user-triggered only (manual run via keypress).
 * Runs in background, does not block UI.            */

/* Speedtest command name. Ookla official CLI outputs JSON
 * with bandwidth in bytes/sec.                       */
#define SPEEDTEST_CMD "speedtest-ookla"

/* Arguments for JSON output with live progress lines */
#define SPEEDTEST_ARG_FORMAT "--format=json"
#define SPEEDTEST_ARG_PROGRESS "--progress=yes"

/* Speedtest progress weights (percent of overall bar).
 * Ping phase gets 5%, download 55%, upload 40%.      */
#define ST_PHASE_PING_WEIGHT 5
#define ST_PHASE_DL_WEIGHT 55
#define ST_PHASE_UL_WEIGHT 40

/* Spinner frames for speedtest progress indicator   */
#define SPINNER_FRAMES                                                         \
  "\xe2\x8a\x99", "\xe2\x8a\x98", "\xe2\x8a\x95", "\xe2\x8a\x98"
#define SPINNER_FRAME_COUNT 4

/* How often to update the speedtest progress bar
 * in the status bar (milliseconds).                 */
#define SPEEDTEST_PROGRESS_MS 500

/* Cache directory for speedtest results (relative to $HOME).
 * Per-SSID files: speedtest_<ssid> and speedtest_<ssid>_history.
 * Results persist across restarts and are separated by network. */
#define SPEEDTEST_CACHE_DIR ".cache/blue"

/* Maximum number of historical results shown in the sparkline graph.
 * Determines sparkline width in the Network pane.                    */
#define SPEEDTEST_HISTORY_MAX 16

/* ── System info refresh ──────────────────────────
 * How often to refresh cpu/mem/disk (milliseconds)  */
#define SYSINFO_REFRESH_MS 1000

/* ── Health refresh ───────────────────────────────
 * How often to refresh disk I/O, services, uptime.
 * Kernel version is static so only read once.       */
#define HEALTH_REFRESH_MS 2000

/* ── Thermal / fan sensors ────────────────────────
 * Maximum number of fan speed entries to display.
 * Scanned from /sys/class/hwmon/                   */
#define HEALTH_MAX_FANS 4

/* Preferred thermal zone type for CPU temperature.
 * Falls back to first available zone if not found.  */
#define THERMAL_ZONE_PREFERRED "x86_pkg_temp"
#define THERMAL_ZONE_FALLBACK "acpitz"

/* ── Startup step labels ─────────────────────────
 * Shown on the splash screen while each subsystem
 * initialises. Order must match startup_init().     */
#define STARTUP_STEPS                                                          \
  {"Bluetooth", "Devices",       "System info", "Printer",                     \
   "Network",   "System health", "Speedtest"}
#define STARTUP_STEP_COUNT 7

/* ── UI poll interval ─────────────────────────────
 * Main loop sleep between iterations (microseconds) */
/* Terminal window title prefix (set via OSC escape on startup)
 * Final title: "BLUE - <hostname>"                              */
#define TERMINAL_TITLE_PREFIX "BLUE"

#define UI_POLL_INTERVAL_US 50000

/* ── Host display ────────────────────────────────
 * Domain suffix appended to hostname in the header
 * (e.g. "trurl" becomes "@trurl.xir.at")           */
#define HOST_DOMAIN_SUFFIX ".xir.at"

#endif
