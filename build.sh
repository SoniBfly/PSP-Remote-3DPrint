#!/usr/bin/env bash
# Build FLSUN Remote EBOOT.PBP → build/dist/EBOOT.PBP (intermediates in build/temp/)
set -euo pipefail
cd "$(dirname "$0")"

if [[ -z "${PSPDEV:-}" ]]; then
  if [[ -d "${HOME}/pspdev" ]]; then
    export PSPDEV="${HOME}/pspdev"
  else
    echo "PSPDEV is not set and ~/pspdev is missing." >&2
    echo "Install pspdev (see README.md) then: export PSPDEV=\$HOME/pspdev" >&2
    exit 1
  fi
fi
export PATH="${PSPDEV}/bin:${PATH}"

if ! command -v psp-gcc >/dev/null; then
  echo "psp-gcc not found in PATH (PSPDEV=${PSPDEV})" >&2
  exit 1
fi

# Optional: user Homebrew libs (needed if pspdev was rewritten for ~/.homebrew)
if [[ -x "${HOME}/.homebrew/bin/brew" ]]; then
  # shellcheck disable=SC1090
  eval "$("${HOME}/.homebrew/bin/brew" shellenv)"
fi

# Load/Slice SPA (three-slicer WASM) → build/temp/www/ → embedded in EBOOT
if [[ -f web/package.json ]]; then
  if [[ ! -d web/node_modules/three-slicer ]]; then
    (cd web && npm install --no-fund --no-audit)
  fi
  (cd web && npm run build)
fi

python3 scripts/pack_www.py
make dist
ls -la build/dist/EBOOT.PBP
echo "OK: build/dist/EBOOT.PBP (www UI embedded; checks web files each launch; exports only changed files)"
