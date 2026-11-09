#!/bin/sh
# make-hpkg.sh — build the LV2 packages on a running RenkuOS (x86_64):
#   lv2_renku       the LV2 spec bundles, the lilv tools and the hkamp example
#                   plug-in (no JACK)
#   lv2_renku_jack  lv2jackhost, the JACK LV2 host
# Requires jack (libjack) to be installed first (lv2jackhost links -ljack).
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
VERSION=0.4.0
REVISION=1
STAGE_PKG="$HERE/stage"
STAGE_JACK="$HERE/stage-jack"

# The system this is built on is the oldest the packages may run on (as
# haikuporter does): the version of the installed haiku package.
HAIKU_PKG=$(ls /boot/system/packages/haiku-r*-x86_64.hpkg | head -n 1)
HAIKU_MIN=$(basename "$HAIKU_PKG" | sed -e 's/^haiku-//' -e 's/-[0-9]*-x86_64\.hpkg$//')

pkgman install -y meson ninja gcc make pkgconfig || true

cd "$ROOT"
rm -rf build "$STAGE_PKG" "$STAGE_JACK"
make

mkdir -p "$STAGE_PKG/bin" "$STAGE_PKG/add-ons/media/LV2" "$STAGE_JACK/bin"

cp build/bin/lv2jackhost "$STAGE_JACK/bin/"
for tool in lv2ls lv2info lv2apply; do
    cp "build/stage/bin/$tool" "$STAGE_PKG/bin/"
done

# LV2 spec bundles (lilv needs them on LV2_PATH) + the example plugin.
for b in build/stage/lib/lv2/*.lv2; do
    [ -d "$b" ] || continue
    cp -r "$b" "$STAGE_PKG/add-ons/media/LV2/"
done
cp -r build/hkamp.lv2 "$STAGE_PKG/add-ons/media/LV2/"

for pkg in lv2_renku lv2_renku_jack; do
    stage="$STAGE_PKG"
    [ "$pkg" = lv2_renku_jack ] && stage="$STAGE_JACK"
    sed -e "s/@VERSION@/$VERSION/g" -e "s/@REVISION@/$REVISION/g" \
        -e "s/@HAIKU_MIN@/$HAIKU_MIN/g" \
        "$HERE/$pkg.PackageInfo" > "$stage/.PackageInfo"
    OUT="$HERE/$pkg-$VERSION-$REVISION-x86_64.hpkg"
    package create -C "$stage" "$OUT"
    echo ">> built $OUT"
done
