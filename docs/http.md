# HTTP server and model storage

The PSP listens on `http_port` (default 8081). Implementation: `src/net/httpd.c`. There is no authentication. Responses include `Access-Control-Allow-Origin: *`. Use a trusted LAN only; do not publish this port on the internet.

The on-card warehouse is `models/<slug>/`, implemented by `src/core/model_storage.c`. `/inbox.json` and `/inbox/<name>` remain as compatibility paths for the web UI. Deletes of a whole model directory are done on the PSP, not over HTTP.

## Routes

| Request | Result | Limit |
|---|---|---|
| `GET /`, `GET /index.html` | Site from `www/`, or a small fallback uploader | |
| `GET /<path>` | File under `www/` | No `..` or `%`; file size ≤ 12 MB |
| `GET /inbox.json`, `GET /models.json` | Model list, preview flags, G-code size | At most 32 models |
| `GET /inbox/<name>`, `GET /models/<name>` | `model.gcode`, `preview.flpv`, or `preview_sup.flpv` | Name maps to a slug and a file kind |
| `GET /slicer.json`, `POST /slicer.json` | Slicer preferences on the card | POST body ≤ 4 KiB and must start with `{` |
| `POST /upload?name=` | `models/<slug>/model.gcode` | ≤ 4 MiB |
| `POST /preview?name=` | `preview.flpv` or `preview_sup.flpv` | ≤ 200 KiB; `FLPV` magic checked by the writer path used for previews |
| `GET /logs`, `GET /api/logs?since=` | Log viewer and JSON | 404 when `log=off` |

Upload and preview bodies are raw bytes. The file name is the `name` query (or `Content-Disposition` when the query is absent).

If `models/<slug>/` already contains that artifact and `mode` is omitted, the server returns **409** `exists: choose overwrite or keep_both`. `mode=overwrite` replaces the file. `mode=keep_both` picks a free `<slug>_N`. The response text includes `slug=<actual>` so the browser can store companion previews on the same model. A short or failed send is not reported as success.

## Names

`model_storage_sanitize_slug` does not strip an extension on a second pass. `resolve_legacy_name` strips one known extension. Unsupported directories are skipped. See [Architecture](architecture.md) for the directory layout and the pending-file replace.
