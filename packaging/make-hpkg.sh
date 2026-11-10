#!/bin/sh
# make-hpkg.sh — build the lv2_renku package on a running RenkuOS (x86_64):
# the LV2 spec bundles, lv2host and lv2guihost with their JACK add-on, the
# lilv tools and the hkamp example plug-in. JACK's headers are needed only
# to build the add-on (see the Makefile); the package does not require jack.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
VERSION=0.4.0
REVISION=1
STAGE_PKG="$HERE/stage"

# The system this is built on is the oldest the packages may run on (as
# haikuporter does): the version of the installed haiku package.
HAIKU_PKG=$(ls /boot/system/packages/haiku-r*-x86_64.hpkg | head -n 1)
HAIKU_MIN=$(basename "$HAIKU_PKG" | sed -e 's/^haiku-//' -e 's/-[0-9]*-x86_64\.hpkg$//')

pkgman install -y meson ninja gcc make pkgconfig || true

cd "$ROOT"
rm -rf build "$STAGE_PKG"
make

[ -f build/bin/backends/jack.so ] ||
	{ echo "!! no JACK add-on was built (JACK headers not found)" >&2; exit 1; }

mkdir -p "$STAGE_PKG/bin" "$STAGE_PKG/add-ons/media/LV2" "$STAGE_PKG/add-ons/lv2host"

cp build/bin/lv2host build/bin/lv2guihost "$STAGE_PKG/bin/"
# Both hosts look for the add-on in add-ons/lv2host/.
cp build/bin/backends/jack.so "$STAGE_PKG/add-ons/lv2host/"
for tool in lv2ls lv2info lv2apply; do
    cp "build/stage/bin/$tool" "$STAGE_PKG/bin/"
done

# LV2 spec bundles (lilv needs them on LV2_PATH) + the example plugin.
for b in build/stage/lib/lv2/*.lv2; do
    [ -d "$b" ] || continue
    cp -r "$b" "$STAGE_PKG/add-ons/media/LV2/"
done
cp -r build/hkamp.lv2 "$STAGE_PKG/add-ons/media/LV2/"

sed -e "s/@VERSION@/$VERSION/g" -e "s/@REVISION@/$REVISION/g" \
    -e "s/@HAIKU_MIN@/$HAIKU_MIN/g" \
    "$HERE/lv2_renku.PackageInfo" > "$STAGE_PKG/.PackageInfo"
OUT="$HERE/lv2_renku-$VERSION-$REVISION-x86_64.hpkg"
package create -C "$STAGE_PKG" "$OUT"
echo ">> built $OUT"
