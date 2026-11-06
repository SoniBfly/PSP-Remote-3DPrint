# Architecture

PSP homebrew remote for an MKS Wi-Fi FDM printer, plus a browser slicer served by the PSP. The install directory on the Memory Stick is `ms0:/PSP/GAME/FLSUNRemote/`. The XMB title is `PSP Remote 3DPrint`; the module name is `FLSUN Remote`.

## Layout

| Path | Role |
|---|---|
| `src/core/main.c` | Startup, screen registration, pad loop |
| `src/core/screen.*` | Screen table and `screen_go` (`leave`, then `enter`) |
| `src/core/config.*` | `flsun.ini` and compiled-in limits |
| `src/core/model_storage.*` | `models/<slug>/` on the Memory Stick |
| `src/core/printer.*` | Printer snapshot, merged file list, queued commands |
| `src/core/session.*` | UI selection, confirm state, status line |
| `src/core/log.*` | Ring log, optional card file, optional TCP/HTTP log |
| `src/net/socket.*` | Connect, send, and receive with deadlines |
| `src/net/printer_mks.*` | MKS TCP commands and SD upload |
| `src/net/httpd.*` | PSP HTTP server |
| `src/net/worker.*` | Printer-command queue and worker thread |
| `src/net/net.*` | WLAN init and LAN address |
| `src/net/www_bundle.*` | Embedded site unpack |
| `src/ui/base/*` | Framebuffer, text, widgets, progress |
| `src/ui/screens/*` | Models, Tune, Dash, Preview, Confirm |
| `src/ui/preview.*` | FLPV blit |
| `src/ui/splash.*` | Startup artwork |
| `web/` | Vite site and slicer |

Screens talk to the printer through `core/printer` and draw through `ui/base`. `model_storage` does not open sockets. The HTTP server and the MKS worker are the writers that touch the card and the printer.

## Threads

Three threads do the work:

1. The UI thread reads the pad, polls for published results, and draws. `httpd_poll` on this thread does not accept clients.
2. `net/worker` runs one queue of 12 slots. It copies each request, talks to the printer, and publishes temperatures, the file list, and per-command results. The UI keeps drawing while a command is in flight.
3. The HTTP thread accepts browser clients. File reads return an owned buffer; the storage lock is not held for the socket send.

Shutdown stops network waits and joins the worker and HTTP thread before the network stack comes down.

`printer_request_*` returns as soon as the command is queued. A busy printer rejects conflicting refresh, print, delete, home, and apply requests. Navigation stays available. Heater-off can be queued; it is not an emergency stop and it is not an immediate acknowledgement.

## Startup

`main` shows the splash before config, model storage, and the web bundle. `www_bundle_ensure` checks the embedded `FLWW` archive before it changes files on the card. Every launch compares each installed file's size and bytes. Only files that differ are exported, through a `.pending` file and replace. The progress bar counts completed checks, then bytes actually written. A www error stays on screen until Cross (continue) or START (exit).

The worker copies the printer address under the config lock, so a later UI edit does not rewrite the string a socket is using.

## Memory Stick

`ms0:/PSP/GAME/FLSUNRemote/` holds `EBOOT.PBP`, `flsun.ini`, `slicer.json`, `models/<slug>/`, `www/`, and a legacy `inbox/` used only as a migration source. A DEBUG build can also write `log.txt`. An optional `ui_autodump.flag` enables framebuffer BMP dumps in DEBUG builds.

One model is one directory:

`models/<slug>/{model.gcode, model.stl, preview.flpv, preview_sup.flpv, thumb.png, meta.ini}`

Slugs are at most 57 bytes so a `.gcode` name still fits a printer file entry. Spaces become `_`. Non-ASCII bytes become `_hh`. Dot segments, `..`, separators, and control characters are rejected. Writes use `.pending`, then replace the target. If rename cannot overwrite, the previous file is kept via `.replace_backup`.

`PSP_HEAP_SIZE_KB(-16384)` leaves 16 MB outside the heap. The web bundle is linked into the ELF (`WWW_BUNDLE_BYTES` in `src/net/www_bundle_meta.h`). The Makefile does not set `PSP_LARGE_MEMORY`. Host tests do not measure PSP-1000 or PSP-2000 memory.

## Frame loop

Input, then result polling, then `httpd_poll` and `log_poll`, then the current screen's input and draw. START leaves the game from the main loop. R on Models or Dash opens Tune.

## Logging

Runtime mode is `log=off` or `log=net` in `flsun.ini`. `log=net` listens on `log_port` (default 9999) and serves `/logs` plus `/api/logs`. `log=off` does not write `last_m105.txt` or `last_m20.txt` and does not serve the log viewer. Card logging (`log.txt`) exists only in builds made with `make DEBUG=1`.
