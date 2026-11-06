# Testing and sample files

## Host tests

```sh
python3 scripts/run_host_tests.py
HOST_SANITIZE=1 python3 scripts/run_host_tests.py
```

`scripts/run_host_tests.py` compiles the C modules with the headers in `tests/host` (POSIX stand-ins for the PSP SDK). `HOST_SANITIZE=1` adds AddressSanitizer and UndefinedBehaviorSanitizer.

The process covers:

- config load, concurrent connection snapshots, and a failed ini write that keeps the previous config
- model storage conflicts, replace/rollback, and locking
- printer snapshots and the result FIFO
- worker scheduling, copied arguments, repeat, and stop
- socket deadlines on a fake clock
- log concurrency, the export cursor, and `log=off`
- MKS `ok` / `File selected` parsing, including names that merely contain those letters
- web-bundle checks against a small `FLWW` fixture
- splash geometry

A passing run prints `All host tests passed.` The same script states that PSP, PPSSPP, and printer behavior still need a runtime check. The host build does not link pspdev and does not draw on a PSP framebuffer.

`cd web && npm ci && npm run build` checks that the site bundles. It does not execute the slicer. A successful Vite build still warns that `node:module` is externalized for the browser and that some chunks are larger than 6 MB. `make dist` needs PSPDEV and is a separate check.

Not covered here: cold or corrupt `www/` on a Memory Stick, card-full errors, acknowledgement text from a particular printer firmware, browsing during a real upload, HTTP and MKS at the same time, the offline-printer timeout on the UI, both Confirm cancel paths on hardware, and whether the linked bundle fits a PSP-1000.

## Sample files

`testdata/` is synthetic. It has no machine addresses, accounts, or logs. Names describe which smoke check the file was made for.

| File | What it contains |
|---|---|
| `smoke_stage1.gcode` | `G28`, then heaters off |
| `smoke_stage2.gcode` | `G28`, heaters off, short `G0` moves at Z30, then `G28` and `M84` |
| `smoke_stage3.gcode` | Same idea as the short file, padded with comments. The header notes that very small uploads can get `err:0` from MKS Wi-Fi and still be missing from `M20` |
| `smoke_stage4.gcode` | 40 mm square and a diagonal, with extrusion, heaters commanded off, nozzle at Z0.2 |
| `smoke_stage4.flpv` | 96,008-byte RGB565 preview (240×200 plus an 8-byte header) |
| `smoke_stage42.stl` | Small binary STL for the browser slicer |

Every G-code file starts with `G28`. A delta printer will home. Keep the bed clear. `smoke_stage4.gcode` extrudes after `M104 S0` and moves to Z0.2. Treat it as preview data, not as a job to print, unless cold-extrusion protection is known to be on.

The PSP client waits for the uploaded name to appear in `M20` before it selects the file. `smoke_stage3.gcode` is padded so a module that ignores multi-kilobyte-or-smaller uploads still has a file large enough to list.
