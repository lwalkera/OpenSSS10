#!/usr/bin/env bash
# Build OpenBeken for this device's Wi-Fi module (BK7231N) with our patches.
#
# openbeken/OpenBK7231T_App is upstream OpenBeken as a submodule, pinned at
# the commit openbeken/patches/ is based on. This script applies the patches
# to its working tree (once; a rerun sees them applied), builds the
# OpenBK7231N target with the SDK's bundled GCC, and copies the images to
# build/openbeken/:
#
#   OpenBK7231N_<version>.rbl      OTA update (OpenBeken's OTA page or /api/ota)
#   OpenBK7231N_QIO_<version>.bin  full flash over UART, bootloader included
#   OpenBK7231N_UA_<version>.bin   UART flash of the application only
#   OpenBK7231N_<version>.axf/.map ELF and map, for debugging
#
# The first build fetches the BK7231N SDK (shallow, about 770 MB) and
# mbedTLS into the submodule; later builds reuse them.
#
#   tools/build_openbeken.sh            # or: make openbeken
#   OBK_VERSION=test1 tools/build_openbeken.sh
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SUB="$ROOT/openbeken/OpenBK7231T_App"
PATCHES=("$ROOT"/openbeken/patches/*.patch)
OUT="$ROOT/build/openbeken"

cd "$ROOT"
git submodule update --init openbeken/OpenBK7231T_App

# The patches only apply to the pinned commit
pinned=$(git rev-parse ":openbeken/OpenBK7231T_App")
head=$(git -C "$SUB" rev-parse HEAD)
if [ "$head" != "$pinned" ]; then
  echo "error: openbeken/OpenBK7231T_App is at ${head:0:8}, not the pinned ${pinned:0:8}" >&2
  echo "       run: git submodule update openbeken/OpenBK7231T_App" >&2
  exit 1
fi

# Apply the patches unless they already are (reverse-applying the whole
# series, last patch first, succeeds only when all of them are applied)
reversed=()
for ((i = ${#PATCHES[@]} - 1; i >= 0; i--)); do reversed+=("${PATCHES[i]}"); done
if git -C "$SUB" apply --reverse --check "${reversed[@]}" 2>/dev/null; then
  echo "patches already applied"
elif git -C "$SUB" apply --check "${PATCHES[@]}" 2>/dev/null; then
  git -C "$SUB" apply "${PATCHES[@]}"
  echo "applied ${#PATCHES[@]} patches"
else
  echo "error: the patches neither apply nor are fully applied in openbeken/OpenBK7231T_App" >&2
  echo "       to start over (this discards changes made there):" >&2
  echo "       git -C openbeken/OpenBK7231T_App checkout -- . && git -C openbeken/OpenBK7231T_App clean -fd src" >&2
  exit 1
fi

# The version shows up on OpenBeken's pages and in the file names
version=${OBK_VERSION:-sss10_$(git rev-parse --short=8 HEAD)$(git diff --quiet HEAD -- openbeken/patches || echo _dirty)}

make -C "$SUB" APP_VERSION="$version" APP_NAME=OpenBK7231N OpenBK7231N

mkdir -p "$OUT"
src="$SUB/output/$version"
for f in "OpenBK7231N_$version.rbl" "OpenBK7231N_QIO_$version.bin" "OpenBK7231N_UA_$version.bin" \
         "OpenBK7231N_$version.axf" "OpenBK7231N_$version.map"; do
  cp "$src/$f" "$OUT/"
done
echo
echo "OpenBeken $version:"
ls -l "$OUT"/*"$version"*
