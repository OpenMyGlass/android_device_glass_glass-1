<!-- SPDX-License-Identifier: Apache-2.0 -->

> **Not implemented: the owner decided against a GPS HAL for the on-board receiver** (see `docs/hals.md`, "No GPS HAL"). This file is kept only as a record of the chip facts. It was written by a separate reverse-engineering agent (clean room: facts only, no code from the stock binaries) and copied here unchanged apart from this note.

# GPS HAL specification and feasibility (Glass Explorer Edition, XE24)

Clean-room facts for an open `gps.glass_1` HAL. Sources:

- **kernel**: GPL source, branch `glass-omap-xrx13b-stable-3.4.113` of OpenMyGlass/kernel-omap, `file:line`.
- **conf**: stock `/system/etc/csr_gps_config.xml`, `/system/etc/gps.conf`, `/system/etc/glass_gpsclear.sh`,
  ramdisk `init.omap4430.rc`.
- **blob**: stock `/system/lib/hw/gps.omap4.so` (1217632 B, sha256 `3f68b133…a493`), the CSR "SLL 2.4.0" location
  library built for Glass; addresses are link-time addresses of exported symbols or data.

No code from the blob is reproduced. Vendor credentials present in the stock config are deliberately not copied.

## 1. Verdict

**An open NMEA-only HAL is feasible.** The receiver is a CSR SiRFstarIV **GSD4e** (ROM part) that comes up
speaking **NMEA 0183 at 4800 baud**; the stock library itself detects NMEA on start and only then switches the chip
to the binary OSP protocol. The switch command and the NMEA input commands are the publicly documented SiRF NMEA
`$PSRF` messages. An open HAL can keep the chip in NMEA mode and parse standard sentences.

What is lost compared with stock: the ROM patch (`NVM2`, proprietary, applied over OSP), CSR's SGEE extended
ephemeris (server and format proprietary), SUPL A-GPS and OSP raw measurements. Expect a slower cold TTFF; hot and
warm starts work with the chip's battery-backed state.

## 2. Hardware

| Item | Value | Source |
|---|---|---|
| Chip | CSR SiRFstarIV GSD4e ("Elton"), ROM version string family `GSD4e_4.1.2` | kernel `drivers/char/gps_elton.c:1-5, 245`; blob strings `GSD4e`, `GSD4e_4.1.2-P1 R`, `GSD4e_4.1.2-P1_RPATCH` |
| UART | OMAP UART4 = `/dev/ttyO3`, TX/RX only in the pad list (no RTS/CTS pads muxed) | kernel `arch/arm/mach-omap2/board-notle.c:1143-1155, 1205-1229`; rc `init.omap4430.rc:39-41` (owner `system`, mode 0660) |
| ON_OFF GPIO | 139 (EVT2+), requested output-low at boot | `board-notle.h:75-76`; `board-notle.c:2047-2058` |
| RESET_N GPIO | 140 (EVT2+), requested output-high (not in reset) at boot | `board-notle.h:77-78`; `board-notle.c:2060-2068` |
| WAKEUP/AWAKE GPIO | 138 (EVT2+), input; high while the receiver is running | `board-notle.h:79-80`; `board-notle.c:2070-2078`; `gps_elton.c` suspend message |
| Reference clock (stock config) | 16.369 MHz TCXO, external LNA on | conf `csr_gps_config.xml` (`RefClkFrequency`, `LnaType`) |

Kernel userspace interface (`drivers/char/gps_elton.c`, class `gps`, device `gps_elton`):

| sysfs file | Read | Write |
|---|---|---|
| `/sys/class/gps/gps_elton/onoff` | ON_OFF GPIO level | "0" or "1" sets the level (anything else `-EINVAL`) |
| `/sys/class/gps/gps_elton/reset` | RESET_N level | "0" (assert reset) or "1" (release) |
| `/sys/class/gps/gps_elton/awake` | AWAKE GPIO level | — |
| `/sys/class/gps/gps_elton/functional` | 1 only on board version V1.5 proto (the newest supported board id) | — |

All four files are mode 0666 in the driver (`gps_elton.c:109-114`). The driver does nothing on suspend/resume except a
log line if the receiver is still awake (`gps_elton.c:116-128`).

## 3. Stock HAL declaration (API versions)

| Item | Value | Where |
|---|---|---|
| `hw_module_t` | tag HWMT, `module_api_version` 0x0001, `hal_api_version` 0, id `"gps"`, name `"csr_gnss"`, author `"CSR"` | blob symbol `HMI` @0x127054 |
| `GpsInterface` | `size` = 40 (KitKat-era layout: init, start, stop, cleanup, inject_time, inject_location, delete_aiding_data, set_position_mode, get_extension) | blob `.data` @0x124fc4, returned by `csr_gnss_get_gps_interface` @0x23720 |
| `get_extension("agps")` | `AGpsInterface`, size 24 (v1: init, data_conn_open, data_conn_closed, data_conn_failed, set_server) | blob @0x20e04 → @0x124fac |
| `get_extension("agps_ril")` | `AGpsRilInterface`, size 28 (v1) | → @0x124ff4 |
| `get_extension("gps-debug")` | `GpsDebugInterface`, size 8 | → @0x124fec |
| `get_extension("gps-ni")` | name recognised but **returns NULL** | blob @0x20e04 |
| `get_extension("gps-xtra")` and others | NULL with log "interface '%s' not supported" | same |

So the `XTRA_SERVER_1` line in stock `gps.conf` is not used by this HAL (no XTRA interface). `gps.conf` also sets
`NTP_SERVER=north-america.pool.ntp.org` (framework NTP time injection).

## 4. How the stock library drives the chip (behaviour)

### 4.1 Power control (blob `CsrSllPortControlGiveOnOffPulse` @0x37c84, `CsrSllPortControlGiveResetPulse` @0x37d04,
`CsrSllPortControlSetOnOff` @0x26c80, `CsrSllPortControlSetReset` @0x26d64)

- Config: `OnOffControl` = GPIO via `/sys/class/gps/gps_elton/onoff`, `ResetControl` = GPIO via
  `/sys/class/gps/gps_elton/reset`, `AutoHighLowPulse` = No (conf).
- ON_OFF pulse: write "1", wait 100 ms, write "0" (2-byte writes, value + NUL). The GSD4e toggles between
  hibernate and full power on each pulse.
- Reset pulse: write "0" to `reset`, wait 100 ms, write "1", wait 200 ms.
- The library uses the reset pulse only when no valid protocol is found during discovery.

### 4.2 Discovery and protocol switch (blob `CsrSllPtaStartChipDiscovery` @0x38e70, `CsrSllPtaEngProtoSwitchOsp`
@0x3a1f4, `CsrSllPtaEngProtoSwitchNmea` @0x3a390, baud table `enginePtaUartBaudRates` @0x1275f0)

1. Open `/dev/ttyO3` at the configured baud 115200 (conf `UartBaudRate`), give an ON_OFF pulse and wait (1 s timer)
   for OSP packets.
2. If no OSP: retry with the next baud of the table 115200, 921600, 460800, 230400 and with flow control toggled.
3. Still no OSP: reopen the UART at **4800 baud, no flow control**, and look for NMEA.
4. If NMEA is seen: send `$PSRF100,0,115200,8,1,0*04` (SiRF NMEA input "set serial port": protocol 0 = SiRF binary,
   115200 8N1), wait 100 ms, reopen the UART at 115200 and rediscover (now OSP).
5. If neither: reset pulse and start again (counted retries).

This is the evidence that the chip's power-on protocol is NMEA at 4800 baud.

### 4.3 After discovery (OSP, stock only)

Software version poll, tracker configuration (ref clock 16369000 Hz, LNA, IO pin config words, UART
115200 with flow control, backup LDO on), optional **ROM patch download** of `/system/vendor/firmware/NVM2`
(18060 B, sha256 `41211a7cb452522adc8494c7184acd18157ab4ee54fe285733f516c2b5f6866f`; conf `ApplyPatch` Yes,
`PathPatchNVM`), SGEE extended ephemeris (conf `Sif`/`OperationMode` Sgee, `FileFormat` FF4, host storage), SUPL 2.0
(conf `SuplServerInfoIpAddress` supl.google.com, port 7276, not secure, MS-Based and MS-Assisted), NTP and
local time aiding, NVM state in `/data/gps/nvm` (rc `init.omap4430.rc:284-285` creates `/data/gps` and
`/data/gps/nvm`, owner `system`, 0770). Stop sends the OSP "software commanded off" message
(`SIRF_MSG_SSB_SW_COMMANDED_OFF`, blob string). Logs in `/data/gps/*.txt`, removed at boot by
`glass_gpsclear.sh` (rc service `glass_gpsclear`).

None of this is needed for an NMEA HAL.

## 5. Open NMEA HAL design (recommended)

Interfaces: legacy `hw_module_t`/`GpsInterface` (size 40 on KitKat; on 6.0.1 the same struct plus
`GpsCallbacks` with `set_system_info_cb` is optional — build with `sizeof(GpsInterface)` from the headers in use).
Expose no extensions at first (`get_extension` returns NULL), or `"gps-debug"` only. Module name `gps.glass_1`.

Sequence:

1. `init`: open `/dev/ttyO3` raw (no echo, no canonical mode, 8N1, no flow control), **4800 baud**; check
   `/sys/class/gps/gps_elton/functional` (expect 1) and the `onoff`/`reset`/`awake` files.
2. `start`: if `awake` reads 0, give an ON_OFF pulse (write "1", 100 ms, write "0"); wait up to ~2 s for `awake` = 1
   and for NMEA bytes. If the chip answers OSP instead (it may keep the stock 115200 binary setting in battery-backed
   RAM), switch it back with the public OSP/SiRF-binary message ID 129 "Switch to NMEA protocol" at 115200 baud
   (documented in the SiRF Binary/OSP reference manuals), then reopen at the chosen NMEA baud. If nothing is seen at
   4800, also try 115200 NMEA. As last resort: reset pulse ("0", 100 ms, "1", 200 ms) and retry.
3. Optionally raise the NMEA rate with `$PSRF100,1,<baud>,8,1,0*CS` (protocol 1 = NMEA), e.g. 38400 or 115200,
   then reopen the UART. Select sentences with `$PSRF103` (GGA, GSA, GSV, RMC at 1 Hz; others off) — both commands
   are in the public SiRF NMEA reference manual.
4. Parse GGA (fix, altitude, HDOP), RMC (time, date, speed, course), GSA (used PRNs → `used_in_fix_mask`), GSV
   (`GpsSvStatus`: PRN, SNR, elevation, azimuth). Report `GpsLocation` with flags LAT_LONG, ALTITUDE, SPEED,
   BEARING, ACCURACY (accuracy estimated from HDOP), and pass each sentence to `nmea_cb` with a CLOCK_REALTIME
   millisecond timestamp. `set_capabilities_cb` with SCHEDULING only.
5. `inject_time` / `inject_location`: optional `$PSRF101` (initialise with position, clock drift, GPS week/TOW,
   reset type warm/hot) — public SiRF NMEA input message; skip in the first version.
6. `delete_aiding_data`: map to `$PSRF101` reset types (cold/warm) if implemented.
7. `stop`: if `awake` is 1, give an ON_OFF pulse to hibernate; confirm `awake` = 0. `cleanup`: close the UART.

Never write the bootloader or other partitions; nothing here touches voltages (the LDO/backup settings remain the
chip defaults).

## 6. Open questions that need a device

1. Confirm NMEA at 4800 baud after a cold power-up, and what the chip speaks after a stock session (OSP at 115200
   retained in battery-backed RAM?).
2. Whether UART4 really has no hardware flow control on this board (stock config enables flow control; the pad
   table lists only TX/RX).
3. TTFF and sensitivity without the `NVM2` ROM patch.
4. Whether `functional` reads 1 on retail XE units.
