#ifndef CONFIG_H
#define CONFIG_H

/* ── Printer (SNMP) ───────────────────────────────
 * Auto-discovered via mDNS (avahi-browse), then
 * queried via snmpget/snmpwalk.
 * Requires: sudo apt install avahi-utils snmp       */
#define PRINTER_SNMP_COMMUNITY "public"

/* How often to re-query the printer (milliseconds).
 * SNMP queries are cheap but no need to hammer it.  */
#define PRINTER_REFRESH_MS  60000

/* Maximum number of supply entries to track
 * (toners, imaging unit, belts, etc.)               */
#define PRINTER_MAX_SUPPLIES 16

/* ── Bluetooth refresh ────────────────────────────
 * How often to re-query bluetoothctl (milliseconds) */
#define BT_REFRESH_MS       5000

/* ── Network refresh ──────────────────────────────
 * How often to re-query wifi status (milliseconds)  */
#define NETWORK_REFRESH_MS  5000

/* ── Speedtest ────────────────────────────────────
 * How often to auto-run speedtest (milliseconds).
 * Runs in background, does not block UI.
 * 600000 = 10 minutes                              */
#define SPEEDTEST_INTERVAL_MS 600000

/* Speedtest command name (e.g. "speedtest", "speedtest.sh",
 * "speedtest-cli"). Must support --simple flag.     */
#define SPEEDTEST_CMD "speedtest.sh"

/* Expected speedtest duration in seconds.
 * Used for the status bar progress bar.             */
#define SPEEDTEST_EXPECTED_SEC 20

/* Spinner frames for speedtest progress indicator   */
#define SPINNER_FRAMES "\xe2\x8a\x99", "\xe2\x8a\x98", "\xe2\x8a\x95", "\xe2\x8a\x98"
#define SPINNER_FRAME_COUNT 4

/* How often to update the speedtest progress bar
 * in the status bar (milliseconds).                 */
#define SPEEDTEST_PROGRESS_MS 500

/* Cache directory for speedtest results (relative to $HOME).
 * Per-SSID files: speedtest_<ssid> and speedtest_<ssid>_history.
 * Results persist across restarts and are separated by network. */
#define SPEEDTEST_CACHE_DIR  ".cache/blue"

/* Maximum number of historical results shown in the sparkline graph.
 * Determines sparkline width in the Network pane.                    */
#define SPEEDTEST_HISTORY_MAX 16

/* ── System info refresh ──────────────────────────
 * How often to refresh cpu/mem/disk (milliseconds)  */
#define SYSINFO_REFRESH_MS  1000

/* ── Health refresh ───────────────────────────────
 * How often to refresh disk I/O, services, uptime.
 * Kernel version is static so only read once.       */
#define HEALTH_REFRESH_MS   2000

/* ── Thermal / fan sensors ────────────────────────
 * Maximum number of fan speed entries to display.
 * Scanned from /sys/class/hwmon/                   */
#define HEALTH_MAX_FANS 4

/* Preferred thermal zone type for CPU temperature.
 * Falls back to first available zone if not found.  */
#define THERMAL_ZONE_PREFERRED "x86_pkg_temp"
#define THERMAL_ZONE_FALLBACK  "acpitz"

/* ── UI poll interval ─────────────────────────────
 * Main loop sleep between iterations (microseconds) */
#define UI_POLL_INTERVAL_US 50000

/* ── Host display ────────────────────────────────
 * Domain suffix appended to hostname in the header
 * (e.g. "trurl" becomes "@trurl.xir.at")           */
#define HOST_DOMAIN_SUFFIX ".xir.at"

#endif
