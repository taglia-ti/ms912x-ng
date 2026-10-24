#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# Linux and Android exercise the same shared conversion fixture and workers.
exec "$repo_root/tools/test_rgbx.sh" "$@"
