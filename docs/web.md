# Web UI, preview files, and license

Sources live in `web/` (Vite 7, no framework). `npm run build` writes `build/temp/www/`. `scripts/pack_www.py` embeds that tree in the EBOOT. The PSP exports it to `www/` on the Memory Stick. See [Build](build.md).

Dependencies:

- `three` (MIT)
- `three-slicer` 0.4.1 (AGPL-3.0-or-later), a browser slicer derived from OrcaSlicer
- `three-slicer-viewer` (MIT)

`web/public/NOTICE-AGPL.txt` is copied into the built site. The repo license is the GNU Affero General Public License v3.0 (`LICENSE`). Because the slicer is inside the homebrew and is served to browsers, ship the corresponding source with any build you distribute, including this `web/` tree.

## What the page does

The library loads `GET /inbox.json` and draws FLPV thumbnails. Add model accepts `.stl`, `.gcode`, `.gco`, or `.g`.

An STL is shown in a cylindrical bed view (radius 130 mm, height 360 mm, the QQ-S Pro volume). Slice uses the baked machine `FLSun QQ-S Pro 0.4 nozzle`, filament `FLSun Generic PLA`, and a process preset from three-slicer (default `0.20mm Standard @FLSun QQSPro`). The first slice is without supports and produces a preview. If Supports is checked, a second slice produces the G-code and a supports preview. G-code over 4 MiB is rejected in the browser before upload.

A dropped G-code file is previewed as a toolpath, an FLPV is rendered, and both can be uploaded. The browser does not apply the 4 MiB cap on that path; the PSP returns 400 if the body is too large.

Uploads are `POST /upload` and `POST /preview` with a raw body. A 409 opens Overwrite / Save both / Cancel and retries with `mode=overwrite` or `mode=keep_both`. Companion previews then use the returned slug. Preferences are stored with `GET`/`POST /slicer.json` on the PSP (≤ 4 KiB).

`asciiName` keeps ASCII letters, digits, `.`, `_`, and `-`. Spaces become `_`. Other characters are dropped. An empty result becomes `model` or `upload`.

`scaleBinaryStl` returns the original bytes at 100%. Any other scale is applied to a binary STL (80-byte header, triangle count, 50 bytes per triangle). An ASCII STL at a scale other than 100% does not match that layout.

`slicer.slice` runs on the page that called it, twice when supports are enabled, so the tab can stall for the whole slice.

## FLPV

`FLPV` + little-endian u16 width + u16 height + RGB565 pixels. The page renders 240×200, which is 96,008 bytes. `testdata/smoke_stage4.flpv` is a file of that size. On the card the artifacts are `preview.flpv` and `preview_sup.flpv`. The PSP only blits them.
