#!/bin/sh
# sync-jdbackend.sh - copy Kasen's audio backends into jdbackend/.
#
# lv2host, lv2guihost and the apps built on this stack (rktuner) run on the
# same audio backends as Kasen (https://github.com/rations/kasen): its Media
# Kit node, its JACK add-on and the loader that finds the add-on. The files
# are copied byte for byte and never edited here: fix them in Kasen, then run
# this again. VST3-renku keeps its copy the same way.
#
# usage: tools/sync-jdbackend.sh [path to a Kasen checkout]
#        (default: ../jackDAW-haiku next to this repo)
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-"$ROOT/../jackDAW-haiku"}
DEST="$ROOT/jdbackend"

[ -f "$SRC/src/backend/mediakit/mediakit_backend.cpp" ] ||
	{ echo "!! no Kasen checkout at $SRC" >&2; exit 1; }

COMMIT=$(git -C "$SRC" rev-parse HEAD)
DIRTY=$(git -C "$SRC" status --porcelain -- src/backend/mediakit src/backend/jack \
	src/engine/audio_backend.h src/engine/audio_backend.c \
	src/engine/rt_ringbuffer.h src/engine/rt_ringbuffer.c)
[ -z "$DIRTY" ] || { echo "!! uncommitted backend changes in $SRC; commit them first" >&2; exit 1; }

mkdir -p "$DEST/backend/mediakit" "$DEST/backend/jack" "$DEST/engine"
cp "$SRC/src/backend/mediakit/mediakit_backend.cpp" "$DEST/backend/mediakit/"
cp "$SRC/src/backend/jack/jack_backend.c" "$DEST/backend/jack/"
cp "$SRC/src/engine/audio_backend.h" "$SRC/src/engine/audio_backend.c" \
	"$SRC/src/engine/rt_ringbuffer.h" "$SRC/src/engine/rt_ringbuffer.c" "$DEST/engine/"
cp "$SRC/LICENSE" "$DEST/LICENSE"

cat > "$DEST/PROVENANCE" <<EOT
These files are copied unchanged from Kasen, MIT licence (see LICENSE here),
by tools/sync-jdbackend.sh. Do not edit them in this repository: change them
in Kasen and sync again.

Source:  https://github.com/rations/kasen
Commit:  $COMMIT
Files:   src/backend/mediakit/mediakit_backend.cpp -> backend/mediakit/
         src/backend/jack/jack_backend.c           -> backend/jack/
         src/engine/audio_backend.h                -> engine/
         src/engine/audio_backend.c                -> engine/
         src/engine/rt_ringbuffer.h                -> engine/
         src/engine/rt_ringbuffer.c                -> engine/
EOT
echo ">> synced jdbackend from $SRC at $COMMIT"
