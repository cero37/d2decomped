#!/bin/sh
# Packs build-3ds/d2d.elf as an installable CIA (New 3DS, 124 MB extended memory mode).
# Needs makerom and bannertool in ~/3ds-tools/bin; they run in a container.
# icon.png, banner.png and banner.wav come from cia/art/make_assets.py.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
tools=${TOOLS:-$HOME/3ds-tools/bin}
out=$root/build-3ds
docker run --rm -u "$(id -u):$(id -g)" -v "$root:$root" -v "$tools:/tools:ro" -w "$root/platform/3ds/cia" ubuntu:24.04 sh -c "
  /tools/bannertool makebanner -i banner.png -a banner.wav -o $out/banner.bnr >/dev/null &&
  /tools/bannertool makesmdh -s 'Sanctuary Pocket' -l 'An unofficial Diablo II engine port — bring your own game files' -p 'd2decomped contributors' -i icon.png -o $out/icon.icn >/dev/null &&
  /tools/makerom -f cia -o $out/d2d.cia -elf $out/d2d.elf -rsf d2d.rsf -target t -exefslogo -icon $out/icon.icn -banner $out/banner.bnr -major 0 -minor 0 -micro 1"
ls -la "$out/d2d.cia"
