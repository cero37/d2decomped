# Sanctuary Pocket: D2Decomp on the New 3DS

![Sanctuary Pocket](cia/art/preview/banner_3x.png)

Sanctuary Pocket is the [D2Decomp](../../README.md) engine, a reimplementation of Diablo II: Lord of
Destruction, running natively on the **New Nintendo 3DS / New 2DS XL**. The world fills the top screen
at the console's own 400×240, and the whole interface (HUD, belt, map, inventory, skills, stores and
menus) lives on the bottom touch screen, playable with the buttons alone or with touch alone.

It is unofficial and unfinished. It is not affiliated with or endorsed by Blizzard Entertainment or
Nintendo, and it contains **no game data**: you supply your own Diablo II + Lord of Destruction files.

## Install

1. **Get the CIA.** Scan the QR code on the
   [latest 3ds build](https://github.com/ratherDashing/d2decomped/releases/tag/3ds-nightly) with FBI
   (Remote Install > Scan QR Code), or download `sanctuary-pocket.cia` from there and install it with
   FBI. `sanctuary-pocket.3dsx` is for the Homebrew Launcher, but the HOME Menu CIA gets the New 3DS's
   larger memory mode, which the game needs.
2. **Copy your game files** to the SD card, into `/3ds/d2d/data/`:

   | File | From |
   |---|---|
   | `D2DATA.MPQ`, `D2CHAR.MPQ`, `D2SFX.MPQ`, `D2SPEECH.MPQ` | Diablo II |
   | `D2Exp.mpq`, `D2xTalk.mpq` | Lord of Destruction |
   | `LODPatch_114d.exe` | Blizzard's 1.14d patch installer: only if your files are older than 1.14d (from the CDs) |

   The music archives aren't needed. If a file is missing, the game lists what it can't find
   instead of starting.
3. **Sound** needs the 3DS's DSP firmware at `/3ds/dspfirm.cdc` (dump it once with DSP1). Without it
   the game runs silent.

Saves go to `/3ds/d2d/save/`, and the game's log to `/3ds/d2d/d2d.log` (the two runs before it are
`d2d-prev.log` and `d2d-prev2.log`, so a crash's log is still there after a restart).

## Controls

| Control | Does |
|---|---|
| Circle Pad | Walk (pushed most of the way: run) |
| A | Attack, talk, open, pick up: the prompt on the top screen says what |
| B, Y, X, L, R, ZR | Your skills |
| Hold ZL | A second set of skills |
| Tap ZL | Swap weapons |
| D-pad | Drink from the belt (Up, Down, Left, Right = its four columns) |
| START | Character, items, skills, map, quests, options |
| SELECT | Put skills on the buttons |
| Touch | Panels, items, stores and menus |
| Tap a name on the map | Walk there: an exit (on into the next area), a person or a waypoint; the Circle Pad stops it. Tap elsewhere on the map to zoom the view |

## Feedback

This port is tested by people playing it. Please
[open an issue](https://github.com/ratherDashing/d2decomped/issues/new/choose) for anything broken,
confusing, ugly or slow, and attach `/3ds/d2d/d2d.log` (and `d2d-prev.log` after a crash). Say
which build you ran: the log's first lines give its commit.

## Build it yourself

Everything builds in the `devkitpro/devkitarm` Docker image:

```sh
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD/..:$PWD/.." -w "$PWD" \
    devkitpro/devkitarm sh platform/3ds/deps.sh   # SDL3, StormLib, CLI11, pinned and patched, into ../d2d-3ds-deps
sh platform/3ds/build.sh                          # build-3ds/d2d.3dsx and d2d.elf (runs the same image)
sh platform/3ds/make-cia.sh                       # build-3ds/d2d.cia (needs makerom and bannertool)
```

The [3ds workflow](../../.github/workflows/3ds.yml) does the same on every push to the `3ds`
branch and publishes the CIA with its QR code.

The HOME Menu icon, banner and jingle are drawn and synthesised by
[`cia/art/make_assets.py`](cia/art/make_assets.py) from nothing but shapes, sine waves and the
Linux Libertine typeface. No picture, sound or text from any game goes into them.

## How it's put together

- `src/main_3ds.cpp`: start-up, the memory layout (96 MiB heap, the rest linear), SD paths.
- `src/present_3ds.cpp`: both screens drawn straight to the framebuffers, memory and frame-time logging.
- `src/al_ndsp.cpp`: OpenAL on the 3DS's DSP.
- `src/threads_3ds.cpp`, `src/memory_3ds.cpp`: thread stacks and cores; malloc falling back to linear memory.
- `src/logs_3ds.cpp`: the rotating logs, and an optional upload to a test server (`/3ds/d2d/logurl.txt`).
- The handheld interface itself is in the engine (`apps/d2d`, behind `D2D_HANDHELD`): the bottom-screen
  HUD and panels, pad steering, the touch pages, and the memory limits (levels let go, capped caches).
