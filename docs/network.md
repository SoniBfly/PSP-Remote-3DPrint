# Printer link

The UI never opens a printer socket itself. It calls `printer_request_*`. `net/worker` runs the command in `src/net/printer_mks.c`.

| Port | Use |
|---|---|
| `mks_port` (default 8080) | One TCP connection per G-code command |
| 80 | `POST /upload?X-Filename=<name>` of G-code to the printer |
| `http_port` (default 8081) | PSP server; see [HTTP](http.md) |

Commands sent to the printer: `M104` / `M140` (heat), `M105` (temperatures), `M220` / `M221` (speed / flow), `G28` (home), `M20` (SD list), `M23` (select), `M24` (start or resume), `M25` (pause), `M27` (SD print status), `M30` (delete).

`M105` and `M30` use 2.5 s. `M20` uses 6 s. `M23`, `M24`, `M25`, and `M27` use 2 s. `G28` uses 8 s. The worker's `timeout_ms` covers the whole operation, including the post-upload `M20` poll. Connect, send, and receive use deadlines; a steady trickle of bytes does not reset the budget. Empty replies are failures.

`M23` and `M24` succeed only on a line-leading `ok` token (any case) or the exact text `File selected`. A match inside an echoed name such as `book.gcode` is not an acknowledgement. A line that is an error, failure, or unknown command overrides a later `ok`. Other firmware wordings still need a check on real hardware; the host test only locks this parser.

Printing a card model reads `models/<slug>/model.gcode` (up to 4 MiB), uploads it to the printer, and waits until `M20` lists the name before `M23` / `M24`. Moving the cursor does not send `M23`. Tile size comes from the merged file list (local G-code size, or unknown for many SD-only names).

`net_refresh_local_ip` UDP-connects to the printer and reads `getsockname`, which yields the LAN address when a stack would otherwise report `127.0.0.1`. A non-empty `local_ip` overrides that result.

Heater-off queues `M104 S0` and `M140 S0`. It does not stop motion and it does not replace the printer's own emergency stop.
