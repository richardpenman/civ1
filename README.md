# Civilization (1991) — reconstructed source

A C reconstruction of the DOS game's `CIV.EXE` (see `NOTES.md` for how it was recovered and
verified), with an SDL2 front end that uses the original game files.

## Build

    make            # civ (the game), libciv.so + civdump (engine, tests), shot (screenshots)

The original game is not part of this repository.  Put its files (`CIV.EXE`, `ISOUND.CVL`,
the `*.PIC`, `*.PAL`, `*.TXT` files, `FONTS.CV`, ...) in `~/Dos/Civ1` (or point
`$CIV_ORIG`, or `make ORIG=dir`, at them): the first `make` derives
the rule tables, the initial game state and the sounds from them (`src/rules_data.c`,
`src/fresh_save.c`, `src/sdl/sound_data.c`, with python3 and the `unicorn` module; also the
disassembly in `build/asm/` that the emulator-based tools use), and the game reads its
pictures, fonts and texts from there at run time.

Needs a C99 compiler and SDL2 (the Makefile also uses a local `.toolchain/` if present).
On macOS: `xcode-select --install` and `brew install sdl2`, then `make civ` (SDL2 is found
with `sdl2-config`).  Copy the project without `.toolchain/`, which holds Linux binaries.

## Play

    ./civ [--data ~/Dos/Civ1] [--saves save] [--scale 3]

`--data` is the directory with the original files (`*.PIC`, `*.PAL`, `*.TXT`, `FONTS.CV`;
default `$CIV_ORIG`, else `~/Dos/Civ1`); games are saved to `--saves` (default `save/`), and
the saved games in the data directory can be loaded too.  The `.SVE`/`.MAP` files are the original formats.

Keys: numeric keypad / arrows move (Home, PgUp, End, PgDn diagonals); `b` build city,
`f` fortify, `s` sentry, `w` wait, space skip, `r` road, `i` irrigate, `m` mine,
`p` clean pollution, `u` unload, `h` home city, `g` GoTo, Shift-`D` disband, Shift-`P` pillage,
`c` centre the view, Enter on a city opens it.  Esc opens the GAME menu (tax rates, find city,
options, save, revolution, quit); F1–F10 are the advisors and world reports; the mouse works
on the menu bar, the map (click a neighbouring square to move there, a city to open it) and
the minimap.  In the city screen: click squares to move workers, click specialists to change
them, `c`/CHANGE, `b`/BUY, `v` to view the city, click an improvement to sell it.

Sound is the original PC-speaker music and effects (switch it off with Sound in the
GAME menu's Options).

## Test

    make test       # differential tests against the original EXE run in an emulator
                    # (needs python3 with unicorn)
    make smoke      # the SDL front end plays scripted games without a window

GitHub runs only a compile check (`.github/workflows/build.yml`, Linux and macOS): without
the original's files it builds with empty stand-ins for the derived data (`make stub-data`,
`ci/stubs/`), which compiles but does not play.  Run the tests locally before pushing.
