# Configuration

Copy `flsun.ini.example` to `ms0:/PSP/GAME/FLSUNRemote/flsun.ini`. The tracked example uses placeholder addresses only. A real `flsun.ini` stays out of git.

| Key | Meaning | Example / default |
|---|---|---|
| `printer_ip` | MKS module address | `192.168.1.50` (placeholder, not discovery) |
| `local_ip` | Optional PSP address shown on Dash | empty; leave empty when the address changes |
| `http_port` | PSP HTTP server | `8081` |
| `mks_port` | MKS G-code TCP port | `8080` |
| `pla_hotend` | Tune hotend preset, °C | `200` (clamped 0–280) |
| `pla_bed` | Tune bed preset, °C | `60` (clamped 0–110) |
| `speed_pct` | `M220` preset | `100` (clamped 10–200) |
| `flow_pct` | `M221` preset | `100` (clamped 10–200) |
| `log` | `off` or `net` | `off` |
| `log_port` | TCP log port when `log=net` | `9999` |

`cfg_load` applies defaults, then the file. Setters mark the config dirty only when the value changes. Tune saves on Apply and when leaving the screen, after a successful queue. Dash Square calls `cfg_save_force`. A failed write keeps the previous file. The worker reads the printer address through `cfg_copy_printer_connection`.

Compiled-in limits (`src/core/config.h`):

- G-code upload 4 MiB
- FLPV preview 200 KiB
- `slicer.json` 4 KiB
- Printer upload HTTP port 80
- Screen 480×272

`slicer.json` sits next to `flsun.ini`. The browser writes it; it is not part of `flsun.ini`.

`log=off` does not serve `/logs`. `log=net` serves `http://<psp>:<http_port>/logs` and accepts a raw TCP stream on `log_port`. DEBUG builds (`make DEBUG=1`) also append `log.txt` on the card.

Dash Left/Right changes the last octet of `printer_ip` (1–254) and marks the file dirty. Save with Square to keep it.
