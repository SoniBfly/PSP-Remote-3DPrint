# Build

The PSP firmware needs the official PSPDEV toolchain and GNU Make. The browser UI needs a Node.js release that Vite 7 supports, plus Python 3 for `scripts/pack_www.py`.

```sh
export PSPDEV="$HOME/pspdev"          # or the path you unpacked
export PATH="$PSPDEV/bin:$PATH"
psp-gcc --version

cd web && npm ci && npm run build && cd ..
make dist                 # release → build/dist/EBOOT.PBP
make DEBUG=1 dist         # card logging → build/dist/EBOOT.PBP
```

Both targets write `build/dist/EBOOT.PBP`. Build release and DEBUG separately if you need both binaries. `DEBUG=1` adds `-DFLSUN_DEBUG` (card `log.txt` and the optional UI dump flag).

`./build.sh` looks for `PSPDEV` or `~/pspdev`, installs web dependencies when `three-slicer` is missing (`npm install`), runs `npm run build`, packs the site, and runs `make dist`. Prefer `npm ci` when `package-lock.json` changed, or when moving between operating systems. `node_modules` is not portable.

Windows does not host pspdev directly. Use WSL and keep the tree on the Linux filesystem. `Makefile` and `build.sh` use LF line endings.

## Products

| Path | Contents |
|---|---|
| `build/temp/www/` | Vite output |
| `build/temp/www.pak` | Packed site |
| `build/temp/obj/` | C objects |
| `build/temp/EBOOT.PBP` | Linked homebrew |
| `build/dist/EBOOT.PBP` | Copy made by `make dist` |
| `src/net/www_bundle_meta.h` | Generated bundle version, file count, and byte count |

`src/net/www_bundle_meta.h` is tracked so `scripts/run_host_tests.py` can read `WWW_BUNDLE_VERSION` before a web build. `scripts/pack_www.py` rewrites it. Do not edit it by hand.

`make clean-all` removes objects, the packed site, `build/dist/EBOOT.PBP`, and the generated meta header. It leaves `build/temp/old/` alone if that directory exists locally. `build/` is gitignored.

Install `build/dist/EBOOT.PBP` as `ms0:/PSP/GAME/FLSUNRemote/EBOOT.PBP` and copy `flsun.ini.example` beside it. The XMB icon is `assets/icon0.png`.

## Web bundle (FLWW)

`scripts/pack_www.py` reads `build/temp/www/` and writes little-endian records:

- magic `FLWW`
- `version` u32, FNV-1a over each relative path, size, and payload
- `file_count` u32
- `total_bytes` u32
- repeated `path_len` u16, path, `size` u32, bytes

Paths reject `..`. `.DS_Store` is skipped. `index.html` is required. `psp-objcopy` turns `www.pak` into `build/temp/www_pak.o` with the symbol names `_binary_www_pak_start` and `_binary_www_pak_end`.

On the PSP, startup validates that archive, compares it with `www/` on the card, and exports only changed files. See [Architecture](architecture.md).

## Host tests

```sh
python3 scripts/run_host_tests.py
HOST_SANITIZE=1 python3 scripts/run_host_tests.py
```

Details and sample G-code: [Testing](testing.md).
