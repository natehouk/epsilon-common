#!/usr/bin/env bash
# Copy epsilon-common's headers into a plugin repo (vendored, so each plugin builds on its own).
#   tools/sync.sh [-f] <path-to-plugin-repo> [PluginName]
# Writes <repo>/plugins/<Name>/common/*.hpp and a VERSION file naming the epsilon-common commit.
# -f: allow syncing from an epsilon-common checkout with uncommitted changes.
set -euo pipefail
force=0
if [ "${1:-}" = "-f" ]; then force=1; shift; fi
[ $# -ge 1 ] || { echo "usage: $0 [-f] <plugin-repo> [PluginName]" >&2; exit 2; }
here="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "$1" && pwd)"
if [ $# -ge 2 ]; then name="$2"; else
  dirs=("$repo"/plugins/*/); [ ${#dirs[@]} -eq 1 ] && [ -d "${dirs[0]}" ] || { echo "can't tell which plugin: pass the name" >&2; exit 2; }
  name="$(basename "${dirs[0]}")"
fi
dest="$repo/plugins/$name/common"
[ -d "$repo/plugins/$name" ] || { echo "no plugins/$name in $repo" >&2; exit 2; }
if [ $force -eq 0 ] && [ -n "$(git -C "$here" status --porcelain -- common)" ]; then
  echo "epsilon-common has uncommitted changes in common/ (use -f to sync anyway)" >&2; exit 1
fi
rev="$(git -C "$here" rev-parse --short HEAD 2>/dev/null || echo unknown)"
[ -n "$(git -C "$here" status --porcelain -- common)" ] && rev="$rev-dirty"
mkdir -p "$dest"
find "$dest" -maxdepth 1 -name '*.hpp' -delete
cp "$here"/common/*.hpp "$dest"/
{
  echo "epsilon-common $rev ($(date -u +%Y-%m-%d))"
  echo "https://github.com/natehouk/epsilon-common"
  echo "Vendored copy: do not edit here. Change epsilon-common, then run tools/sync.sh."
} > "$dest/VERSION"
echo "synced epsilon-common $rev -> plugins/$name/common ($(ls "$dest"/*.hpp | wc -l) headers)"
