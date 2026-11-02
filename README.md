# Hailstone: the NMS plugin

The [Hailstone](https://github.com/projecthailstone/hailstone-client) plugin for the NMS server. It does what the NMS
patch does to the stock RoF2 client (personal loot, the vault, waypoints, pet commands, the server's checks) and
switches on only when that server announces itself. What it does is in [docs/nms.md](docs/nms.md).

## Installing

On Hailstone's login screen, click **Add-ons**, paste `https://github.com/projecthailstone/hailstone-nms` and press
Look, then Install. The client keeps it up to date. A server can also ship it to its players itself (docs/nms.md,
"Installing it").

## Building

Inside the client's tree, with the client's tests:

```sh
cd hailstone-client
git clone https://github.com/projecthailstone/hailstone-nms.git plugins/nms
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
build/nmsc_tests          # the client's tests and this plugin's
```

The client's `.gitignore` keeps `plugins/nms/` out of its own repository. With the folder there, the client builds the
plugin into `build/hailstone/plugins/nms/`, adds its tests (`tests/nms_*_test.cpp`) to `nmsc_tests` and its decoders
(`tests/fuzz_decoders_nms.inc`) to the fuzz lists. Without it, the client builds as before. The Project Triune
plugin's tests also load this plugin when both are checked out.

On its own, against a client checkout's `sdk/` (no tests):

```sh
cmake -S . -B build -G Ninja -DHAILSTONE_CLIENT_DIR=../hailstone-client
cmake --build build       # build/nms/: the library and ui/
```

## Releasing

```sh
tools/release.sh 1.0.0             # builds linux-x64 and windows-x64, packs and signs into release/v1.0.0/
tools/release.sh 1.0.0 --publish   # the same, then a GitHub release of this repository
```

The script's header lists what it needs: a client checkout with `hailstone_sign` built, the add-on key, Docker with
the `hailstone-linux-build:24.04` image, and the clang-cl + xwin toolchain. Keep signing with the same key: a
player's client pins the first key it sees and refuses an update signed with another.

The add-on manager reads releases from public repositories only. While this repository is private, the Add-ons link
does not work. A player then copies a package's folder into `plugins/nms/` by hand, or the server ships the plugin.

The client's rules (its `AGENTS.md` and `CONTRIBUTING.md`) apply here too.
