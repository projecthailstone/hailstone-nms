#!/usr/bin/env bash
# Makes a release of this plugin as a Hailstone add-on (the client's docs/addons.md): builds the library for each
# platform, packs it with its ui/ (the client's tools/make_addon.py), signs hailstone-addon.txt, and with --publish
# uploads everything as a GitHub release of RELEASE_REPO, the repository players paste on the Add-ons screen.
#
#   tools/release.sh 1.0.0              build and pack into release/v1.0.0/, nothing uploaded
#   tools/release.sh 1.0.0 --publish    the same, then gh release create v1.0.0 on RELEASE_REPO
#
# Needs a hailstone-client checkout ($HAILSTONE_CLIENT, default ../hailstone-client) for sdk/, tools/make_addon.py
# and a built hailstone_sign ($HAILSTONE_SIGN, default $HAILSTONE_CLIENT/build/hailstone_sign); the add-on key's
# secret half ($ADDON_KEY, default ~/.config/hailstone-release/addon-key.secret: keep it off GitHub, and keep using
# the same one, since a player's client refuses an update signed with another key); for linux-x64, Docker and the
# hailstone-linux-build:24.04 image (Ubuntu 24.04, so the library loads on its glibc and libstdc++); for windows-x64,
# the client's clang-cl + xwin toolchain (its docs/ci.md "Windows builds from Linux"). $JOBS caps the parallel jobs.
set -euo pipefail

# ---- this plugin
NAME=nms
CAPS=read,commands,actions,move,chat,events,ui,packets,extend,login # docs/nms.md "What it needs"
RELEASE_REPO=projecthailstone/hailstone-nms
DESCRIPTION="The NMS server's client features: personal loot, the vault, waypoints, pet commands. Off on other servers."
AUTHOR="Project Hailstone"
MIN_CLIENT=0.2.0-alpha # raise it when the plugin needs a newer SDK (sdk/nmsc_plugin.h) than this client has

# ---- the rest is the same for every plugin
version=${1:?usage: tools/release.sh <version> [--publish]}
version=${version#v}
publish=${2:-}
here=$(cd "$(dirname "$0")/.." && pwd)
client=$(cd "${HAILSTONE_CLIENT:-$here/../hailstone-client}" && pwd)
sign=${HAILSTONE_SIGN:-$client/build/hailstone_sign}
key=${ADDON_KEY:-$HOME/.config/hailstone-release/addon-key.secret}
jobs=${JOBS:-16}
image=${LINUX_IMAGE:-hailstone-linux-build:24.04}
out=$here/release/v$version
tag=v$version

[ -f "$client/sdk/nmsc_plugin.h" ] || { echo "no hailstone-client at $client (set HAILSTONE_CLIENT)" >&2; exit 1; }
[ -x "$sign" ] || { echo "no hailstone_sign at $sign: build the client, or set HAILSTONE_SIGN" >&2; exit 1; }
[ -f "$key" ] || { echo "no add-on key at $key: '$sign keygen $key' once, or set ADDON_KEY" >&2; exit 1; }
if [ -n "$(git -C "$here" status --porcelain)" ]; then
  echo "the working tree has changes: commit them first, so the release matches a commit" >&2
  exit 1
fi

# linux-x64, in the Ubuntu 24.04 image; the build folder belongs to the caller, not root.
rm -rf "$here/build-linux" "$here/build-windows" "$out"
docker run --rm --cpus "$jobs" -u "$(id -u):$(id -g)" -v "$here:/src" -v "$client:/client:ro" "$image" sh -c "
  cmake -S /src -B /src/build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DHAILSTONE_CLIENT_DIR=/client &&
  cmake --build /src/build-linux -j $jobs &&
  strip --strip-unneeded /src/build-linux/$NAME/$NAME.so"

# windows-x64, cross-built with clang-cl.
cmake -S "$here" -B "$here/build-windows" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHAILSTONE_CLIENT_DIR="$client" \
      -DCMAKE_TOOLCHAIN_FILE="$client/cmake/toolchains/clang-cl-xwin.cmake"
cmake --build "$here/build-windows" -j "$jobs"

python3 "$client/tools/make_addon.py" --name "$NAME" --kind plugin --version "$version" --caps "$CAPS" \
    --platform "linux-x64=$here/build-linux/$NAME" --platform "windows-x64=$here/build-windows/$NAME" \
    --author "$AUTHOR" --description "$DESCRIPTION" --min-client "$MIN_CLIENT" \
    --notes "https://github.com/$RELEASE_REPO/releases/tag/$tag" --out "$out"
"$sign" sign "$key" "$out/hailstone-addon.txt"
"$sign" verify "$("$sign" public "$key")" "$out/hailstone-addon.txt"
ls -l "$out"

if [ "$publish" = "--publish" ]; then
  # The tag goes on the commit built (this repository is RELEASE_REPO), so push it first.
  gh release create "$tag" -R "$RELEASE_REPO" --target "$(git -C "$here" rev-parse HEAD)" --title "NMS plugin $version" \
     --notes "Install from Hailstone's Add-ons screen: paste https://github.com/$RELEASE_REPO." "$out"/*
fi
