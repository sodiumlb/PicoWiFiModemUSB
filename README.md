# Pico WiFi USB modem

[![Latest release](https://img.shields.io/github/v/release/benedictemarty/PicoWiFiModemUSB?label=release)](https://github.com/benedictemarty/PicoWiFiModemUSB/releases/latest)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](#license)
[![Platform](https://img.shields.io/badge/platform-Pico%20W%20(RP2040)-c51a4a)](https://www.raspberrypi.com/products/raspberry-pi-pico/)

A Raspberry Pi **Pico W** based USB CDC ⇄ WiFi modem with Hayes **AT**-style
commands and a LED indicator — now with **TLS/SSL termination inside the dongle**.

The host (e.g. an Oric 8-bit computer via [LOCI](https://github.com/sodiumlb/loci-hardware))
talks plain serial over USB; the Pico W handles the network — including the full
HTTPS handshake, decryption and optional certificate verification. No extra or
custom hardware is required: a bare Pico W is enough.

> **About this fork** — adds TLS/SSL termination (mbedTLS) and the related AT
> commands on top of [sodiumlb/PicoWiFiModemUSB](https://github.com/sodiumlb/PicoWiFiModemUSB),
> itself based on the [Pico WiFi modem](https://github.com/mecparts/PicoWiFiModem).

## Contents

- [Project lineage](#project-lineage)
- [Features](#features)
- [Installation](#installation)
- [First-time setup](#first-time-setup)
- [TLS / HTTPS](#tls--https)
- [Command reference](#command-reference)
- [Building from source](#building-from-source)
- [Notes](#notes)
- [References](#references)
- [Acknowledgements](#acknowledgements)
- [License](#license)

## Project lineage

This firmware stands on the shoulders of a long line of retro WiFi modems.
Direct ancestry (this fork is at the bottom):

1. [jsalin/esp8266_modem](https://github.com/jsalin/esp8266_modem) — Jussi Salin's original ESP8266 virtual modem.
2. [mecparts/RetroWiFiModem](https://github.com/mecparts/RetroWiFiModem) → [mecparts/PicoWiFiModem](https://github.com/mecparts/PicoWiFiModem) — Paul Rickards / mecparts lineage, ported to the Pico.
3. [sodiumlb/PicoWiFiModemUSB](https://github.com/sodiumlb/PicoWiFiModemUSB) — **direct parent**: native USB-CDC variant for the Pico W, designed to pair with [LOCI](https://github.com/sodiumlb/loci-hardware).
4. **this fork** ([benedictemarty/PicoWiFiModemUSB](https://github.com/benedictemarty/PicoWiFiModemUSB)) — adds TLS/SSL termination (mbedTLS) and the `ATGET https`, `AT$CA` and `AT$CV` commands.

Other influences (ESP8266 virtual modems by Stardot and Roland Juno, WiFi232,
etc.) are listed under [References](#references).

## Features

- Native **USB CDC** serial interface (no RS-232 hardware needed).
- Hayes **AT** command set (dial, answer, speed dial, Telnet, status…).
- **WiFi** station mode (2.4 GHz) with credentials stored in flash.
- **TLS-terminated HTTPS**: `ATGET https://…` performs the handshake and returns
  the decrypted page.
- **Certificate verification** (`AT$CV1`) against a user-provided CA
  (`AT$CA=`) — refused unless a CA is present, so it never fails open.
- On-board flash storage via **LittleFS** (settings + CA), in the area not used
  by the firmware.

## Installation

A pre-built firmware is attached to each
[release](https://github.com/benedictemarty/PicoWiFiModemUSB/releases/latest).

1. Unplug the Pico W, then plug it back in **while holding the BOOTSEL button**.
   It appears as a USB drive named `RPI-RP2`.
2. Copy `wifi_modem-vX.Y.Z.uf2` onto that drive.
3. The Pico reboots automatically and enumerates as a USB serial port
   (`/dev/ttyACM*` on Linux, a COM port on Windows).

Connect with any serial terminal (`picocom -b 115200 /dev/ttyACM0`,
`minicom`, PuTTY…). USB CDC ignores the actual baud rate, but the AT-level
`AT$SB` speed must match your terminal for UART-style setups.

## First-time setup

Default serial configuration: **9600 bps, 8 data bits, no parity, 1 stop bit**.

```
AT$SSID=your WiFi network name     set the network to join at power-up
AT$PASS=your WiFi network password set the password (case sensitive)
ATC1                               connect now
AT&W                               save settings to NVRAM
```

Optional:

```
AT$SB=speed   default serial speed        AT$SU=dps   data bits / parity / stop bits
ATNETn        Telnet protocol mode        AT&Kn       RTS/CTS flow control
AT&Dn         DTR handling
```

After `AT&W`, the modem auto-connects on power-up and is ready to dial out with
`ATDT` or fetch pages with `ATGET`.

## TLS / HTTPS

HTTPS is **terminated in the dongle**: the Pico W performs the TLS 1.2 handshake
with mbedTLS, then streams the decrypted response over the serial link.

Fetch a page over TLS:

```
ATGEThttps://example.com
```

### Certificate verification

By default verification is **off** (`AT$CV0`, insecure: any server certificate is
accepted). To verify the server certificate against a trusted root:

```
AT$CA=                 then paste a CA certificate in PEM form,
-----BEGIN CERTIFICATE-----
…                      finish with a line containing only a single '.'
-----END CERTIFICATE-----
.
AT$CV1                 enable verification (returns ERROR if no CA is stored)
AT&W                   persist
```

With a CA loaded and `AT$CV1` enabled, a connection whose certificate does not
chain to that CA (expired, self-signed, unknown root, wrong hostname) is
**refused**. `AT$CA?` reports the stored CA size; `AT$CA-` deletes it.

> Note: there is no on-board clock, so certificate *expiry dates* are not
> checked; trust is established via the CA chain and hostname (SNI).

## Command reference

Multiple AT commands can be typed on a single line. Spaces between commands are
allowed, but not within commands (e.g. `ATS0=1 X1 Q0` is fine; `ATS 0=  1` is
not). Commands taking a string argument (e.g. `AT$SSID=`, `AT$TTY=`) consume
*everything* that follows, so no command may follow them.

Command | Details
------- | -------
+++     | Online escape code: one second pause, three `+`, another one second pause — returns to local command mode.
A/      | Repeats the last command entered. Do not type AT or press Enter.
AT      | The attention prefix preceding all commands except A/ and +++.
AT?     | Displays a help cheatsheet.
ATA     | Force the modem to answer an incoming connection.
ATC?<br>ATC*n* | Query/change WiFi connection status. 0 = not connected, 1 = connected. `ATC0` disconnects, `ATC1` connects.
ATDS*n* | Calls the host in speed dial slot *n* (0-9).
ATDT<i>[+=-]host[:port]</i> | Establish a TCP connection to host/IP (default port 23, Telnet). Speed-dial by alias or 7 identical digits. A leading `+`/`=`/`-` overrides the ATNET setting for the call (**+** fake Telnet, **=** real Telnet, **-** no Telnet). Pressing a key before connect aborts.
ATE?<br>ATE*n* | Command-mode echo. E0 off, E1 on.
ATGET*http&#58;//host[/page]*<br>ATGET*https&#58;//host[/page]* | Fetch and display a web page, then close. **https** is TLS-terminated by the modem (see [TLS / HTTPS](#tls--https)).
ATH | Hang up the current connection.
ATI | Display network status: build date, WiFi/call state, SSID, RSSI, IP, bytes transferred.
ATNET?<br>ATNET*n* | Query/change Telnet mode. 0 = off, 1 = real Telnet, 2 = fake Telnet. See note on real vs fake below.
ATO | Return online (use with +++ to toggle command/online modes).
ATQ?<br>ATQ*n* | Result codes. Q0 display, Q1 suppress (quiet).
ATRD<br>ATRT | Display current UTC date/time from NIST (`YY-MM-DD HH:MM:SS`). Requires WiFi and no active call.
ATS0?<br>ATS0=*n* | Rings before auto-answer. `S0=0` = don't answer.
ATS2?<br>ATS2=*n* | ASCII code of the online escape character (default 43 = `+`). 128-255 disables the escape.
ATV?<br>ATV*n* | Result codes as words (V1) or numbers (V0).
ATX?<br>ATX*n* | Result code verbosity. X0 basic (CONNECT, NO CARRIER), X1 extended (with speed / connect time).
ATZ | Reset the modem.
AT&D?<br>AT&D*n* | DTR-inactive handling: &D0 ignore, &D1 command mode, &D2 end call, &D3 reset.
AT&F | Reset NVRAM and current settings to defaults (SSID, password, speed dials included).
AT&K?<br>AT&K*n* | Flow control: &K0 off, &K1 hardware (CTS/RTS).
AT&R?<br>AT&R=*server pwd* | Query/change the password required for incoming connections (3 tries in 60 s).
AT&V*n* | Display current (&V0) or stored (&V1) settings.
AT&W | Save current settings to NVRAM.
AT&Z*n*?<br>AT&Z*n*=*host[:port],alias* | Store up to 10 speed-dial entries. Example: `AT&Z2=particlesbbs.dyndns.org:6400,particles` → dial with `ATDS2`, `ATDTparticles` or `ATDT2222222`.
AT$AE?<br>AT$AE=*startup AT cmd* | Query/change the command line executed at startup.
AT$AYT | Send a Telnet "Are You There?" if connected to a Telnet remote.
AT$BM?<br>AT$BM=*server busy msg* | Query/change the message returned to an incoming connection while busy.
AT$CA?<br>AT$CA=<br>AT$CA- | TLS CA management. `?` reports the stored CA size (bytes), `=` uploads a CA in PEM form (end with a line containing only `.`), `-` deletes it. See [TLS / HTTPS](#tls--https).
AT$CV?<br>AT$CV*n* | TLS certificate verification. 0 = off (insecure, accept any cert), 1 = on. `AT$CV1` returns **ERROR** unless a CA is stored.
AT$MDNS?<br>AT$MDNS=*mDNS name* | Query/change the mDNS network name. With a non-zero TCP port set, reach it via `telnet mdnsname.local port`.
AT$PASS?<br>AT$PASS=*WiFi pwd* | Query/change the WiFi password (case sensitive, max 64 chars). Set empty to clear.
AT$SB?<br>AT$SB=*n* | Query/change baud rate: 110, 300, 450, 600, 710, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 76800, 115200. Must match your terminal.
AT$SP?<br>AT$SP=*n* | TCP server port to listen on (0 = disabled).
AT$SSID?<br>AT$SSID=*ssid* | Query/change the SSID (case sensitive, max 32 chars). Set empty to clear.
AT$SU?<br>AT$SU=*dps* | Data bits (5-8), parity (N/O/E), stop bits (1-2). Default 8N1.
AT$TTL?<br>AT$TTL=*telnet location* | Telnet SEND-LOCATION value (default "Computer Room").
AT$TTS?<br>AT$TTS=*WxH* | Telnet NAWS window size (default 80x24).
AT$TTY?<br>AT$TTY=*terminal type* | Telnet TERMINAL-TYPE value (default "ansi").
AT$W?<br>AT$W=*n* | Startup wait. $W=0 no wait, $W=1 wait for Enter at startup.

**Real vs fake Telnet** — with *real* Telnet a CR sent by the modem is followed
by a NUL; some BBS implementations don't strip that NUL (use *fake* Telnet for
those, e.g. Particles!). Inbound, real Telnet strips a NUL following a CR; fake
Telnet passes it through.

## Building from source

The Pico SDK and dependencies are vendored under `src/`. With the ARM toolchain
installed:

```
cmake -S src -B src/build
cmake --build src/build -j
```

The firmware is produced at `src/build/wifi_modem.uf2`. A Wokwi test variant
(UART console instead of USB CDC) can be built with `-DWOKWI=1`.

See [CHANGELOG.md](CHANGELOG.md) for version history.

## Notes

### Linux, Telnet, Zmodem and downloading binary files

If you `sz` a binary file from a Linux box over a Telnet connection and the link
drops at a reproducible point, it is the **Linux telnet daemon**, not the modem:
it chokes on blocks containing many `0xFF` bytes. Xmodem and Ymodem (128-byte
blocks) work; Zmodem does not. Modem-to-modem transfers are unaffected.

## References

- [Retro WiFi Modem](https://github.com/mecparts/RetroWiFiModem)
- [WiFi232 — An Internet Hayes Modem for your Retro Computer](http://biosrhythm.com/?page_id=1453)
- [WiFi232's Evil Clone](https://forum.vcfed.org/index.php?threads/wifi232s-evil-clone.1070412/)
- [Jussi Salin's virtual modem for ESP8266](https://github.com/jsalin/esp8266_modem)
- [Stardot's ESP8266 based virtual modem](https://github.com/stardot/esp8266_modem)
- [Roland Juno's ESP8266 based virtual modem](https://github.com/RolandJuno/esp8266_modem)

## Acknowledgements

- Jussi Salin for releasing the original ESP8266 virtual modem code.
- Paul Rickards for the hardware inspiration.
- The Stardot contributors.
- [sodiumlb](https://github.com/sodiumlb) for the USB-CDC Pico W port and LOCI.
- And Dennis C. Hayes, for a command set simple and elegant enough to outlive
  the modems it was made for.

## License

GNU General Public License v3.0 — see the source headers. This is a derivative
work of the projects listed above, distributed under the same terms.
