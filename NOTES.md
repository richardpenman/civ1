# Civilization (DOS, 1991) — source reconstruction notes

Goal: a modern portable C + SDL port that is behaviourally faithful to `orig/CIV.EXE`
and loads the original assets from `orig/`. Work proceeds in milestones.

## Layout

- `orig/` — the shipped game (untouched).
- `tools/` — Python analysis tools (run with `python3 -I`).
  - `unexepack.py` — undoes Microsoft EXEPACK. `python3 -I tools/unexepack.py orig/CIV.EXE build/CIV_UNPACKED.EXE`
  - `pic.py` — decodes `.PIC`/`.PAL` to PNG. `python3 -I tools/pic.py orig/KING00.PIC out.png`
  - `extract_rules.py` — generates `src/rules_data.c` from the EXE's data segment.
  - `disasm.py` — writes `build/asm/seg_XXXX.asm` (root) and `ovl_NN.asm` (overlays) with far calls,
    overlay thunks and globals named from `tools/symbols.map`. Rerun after editing the map.
  - `symbols.map` — the growing symbol database (`f_SSSS_OOOO`/`o_NN_OOOO` functions, `g_XXXX` globals,
    `h_SSSS_OOOO` far data).
  - `lift.py` — turns one function of a listing into goto-style pseudo-C (register tracking, call
    arguments, MSC idioms, struct folding, jump tables). A reading aid: `python3 -I tools/lift.py
    build/asm/seg_0d07.asm city_process`.
  - `callees.py` — the non-UI functions a function depends on, in porting order.
  - `oracle.py` — runs original functions in a Unicorn x86 emulator on a loaded save (overlays,
    DOS file calls, keyboard and timer are emulated; graphics, dialogs and movies are stubbed).
- `tests/run.py` — differential tests: every ported function is run in the emulator and in the C
  port (`libciv.so`) from the same save and RNG seed, and the complete saved state, the map page
  and the AI's transient tables must come out byte-identical.
- `src/` — the reconstructed C sources. `make` builds them, `make test` checks every
  `orig/*.SVE` loads and re-encodes byte-for-byte; `./civdump X.SVE X.MAP -m` prints a summary and map.
  `src/sdl/` is the SDL front end (`./civ`); `ui_headless.c` stands in for it in `libciv.so`.
  `./shot X.SVE X.MAP out.bmp` renders a save's main screen without a window.
- Front-end tests: `CIV_HEADLESS=1 CIV_KEYS="ENTER b SHOT:x.bmp ..." ./civ` plays a key
  script without a window (`M:x,y` clicks, `F1`.., `ESC`, `SPACE`, `SHOT:` saves the screen).
- `.toolchain/` — gcc 12 and SDL 2.26 unpacked from Debian packages (`apt-get download` + `dpkg -x`),
  because this machine has no compiler and no root. The Makefile uses it automatically.
- `build/` — scratch output (unpacked EXE, disassembly, PNGs); safe to delete.

## CIV.EXE facts

- Microsoft C 5.x ("MS Run-Time Library - Copyright (c) 1988"), large model, MS LINK overlays,
  then EXEPACKed. Unpacked: 199,776-byte root image, 2,517 relocations, plus 126,401 bytes of
  appended overlay data (~23 overlays, entered through `int 3Fh` thunks — 147 in the root).
- Roughly 350 `push bp; mov bp,sp` functions in the root and ~150 in overlays, plus hand-written asm.
- Real entry `2058:2E83`. DGROUP = segment `0x2A1B` (file offset `0x2A1B0` in the root image).
- 31 root code segments (from relocation targets) plus DGROUP. Overlays are 23 small MZ images
  appended at file offset `0x2B800`; all are linked at segment `0x2388` and loaded into an empty
  9,648-byte buffer there. Calls into overlays are inline `int 3Fh; db overlay; dw offset`.
- Graphics, sound and misc helpers are separate driver binaries loaded at runtime:
  `EGRAPHIC/MGRAPHIC/TGRAPHIC.EXE` (EGA/MCGA/Tandy), `MISC.EXE`, `*SOUND.CVL`.

### DGROUP tables (offsets from DS)

| Offset | Contents | Record |
|-------:|----------|--------|
| `0x00AC` | government names (6) | NUL-separated strings |
| `0x00E5` | trade goods (8) | strings |
| `0x0239` | leader titles per government (6) | strings |
| `0x0282` | terrain + specials (24) | 19 B: name[12], move, defense×2, food, shields, trade, ?, ega colour |
| `0x04DA` | advances (72) | 22 B: name[20], prereq1, prereq2 (signed byte; -1 none, 127 never) |
| `0x0B9A` | buildings & wonders (46) | 30 B: name[24], cost/10, upkeep, prereq (words) |
| `0x112A` | unit types (28) | 34 B: name[12], 11 words (see `src/rules.h`) |
| `0x14D8` | civilizations (16) | 58 B: 5 words, leader[16], plural[16], adjective[16] |

### PIC / PAL format

A file is a series of chunks: `tag[2] length[u16] data[length]`.

- `E0` — 256-byte EGA dither map (`first, last, map[]`).
- `M0` — VGA palette: `first, last`, then 6-bit RGB triples for `first..last`.
- `X0` — 8-bit image to end of file: `width u16, height u16, maxbits u8`, then an LZW stream
  whose output is RLE-decoded (`0x90 n`: n=0 → literal 0x90, else repeat previous byte n-1 more times).
  (`X1` would be the same with 4-bit packed pixels; none seen yet.)

The LZW is **not** textbook: it mirrors the routine at root `0x1219–0x13B3`. It adds a dictionary
entry for *every* code (including the first after a reset, using the stale previous code), widens
when the next code exceeds the mask, and resets when the width would exceed `maxbits` (≤ 11).
All 106 `.PIC` files in `orig/` decode to 320×200.

### Save files

`.SVE` (37,856 bytes) is a raw dump of 80 globals in a fixed order with no header; the order and
sizes come from the serializer `o_11_09e4`, and `src/save.c` reproduces it. Layout highlights:
`0x0000` turn, `0x0002` human civ, `0x0006` world seed, `0x0008` year, `0x000A` difficulty,
`0x0010` leader/civ/adjective names, `0x0138` treasury, `0x04E8` advance bitsets,
`0x1508` cities (128 × 28 B), `0x2308` unit-type table (the game saves its own copy),
`0x26C0` units (8 civs × 128 × 12 B), `0x6970` city names (256 × 13 B).

`.MAP` is a 320×200 `X0` picture: the off-screen page that holds the world. See the layer table in
`src/game.h`. Special resources and huts are not stored; both come from a hash of the square's
4×4 block and the world seed (`has_special_resource`, `has_hut` in `src/map.c`).

## Status

- [x] M1: unpacker, image decoder, rule tables → `src/rules_data.c`, `src/rules.h`
- [x] M2: segment-aware disassembler + symbol map; `.SVE`/`.MAP` formats; C game state,
      picture decoder, save loader/encoder (round-trips all 11 shipped saves), map queries
- [x] M3: core rules in C (map, units, movement/combat, cities, production, science, governments)
      Done and verified against the original: square yields, unit creation/deletion/stacks,
      visibility and first contact, war/peace between AIs, advance trading, the whole city turn
      (`city_process`, 324 cities), production choice, caravans, research, civilization
      destruction and rebirth, a civ's whole city phase (`civ_process_cities`, 74 civs), unit
      movement, combat, city founding/capture and diplomats for a whole civ's unit phase
      (`civ_move_units` with scripted orders, 126 civ turns), and the computer players' own
      unit orders (`ai_unit_command` incl. barbarians, 62 civ turns per seed).
      AI strategy and government changes (`ai_strategy`, 296 cases), AI diplomacy (296), the
      end-of-turn events (barbarians, AI wonders, pollution/warming, ranking, historians,
      disasters) and whole turns (`game_turn_loop`, the human's civilization sitting out).
      World generation (random and Earth, 12 worlds), new-game setup (start positions and
      their balancing), spaceships (1872 parts), and the diplomacy meeting with the human
      (624 meetings, dialog answers scripted).
- [x] M4: SDL front end (`src/sdl/`, `./civ`): the original's assets (SP257/TER257/SP299 sprite
      sheets, SP256 palette, FONTS.CV), the map view ported from the VGA path of the square
      drawing (`f_1b05_03b1`: terrain blending, coast quarters, rivers, roads/railroads,
      improvements, cities, the fog edge; units `f_1b05_0e1b`/`0fa5`; the dissolve order of
      `f_1b05_0004`), minimap, status and unit panels (`f_0237_107a`, `f_0402_4010`), the human's
      orders (keys, mouse, GoTo, menus GAME/ORDERS/ADVISORS/WORLD/CIVILOPEDIA), dialogs fed by the
      text sections of KING/HELP/ERROR/BLURB, the city screen (workers, specialists, change,
      buy and sell with the original's prices), save/load (`.SVE` + `.MAP` written in the
      original format), new games (world parameters, difficulty, rivals, tribe, names).
      The human's turn gained the original's end-of-turn wait and the settler/first-move
      hints (`o_04_00af`, which re-centres the view and so takes a random number).
- [x] M5: end-game and the screens with game effects: the newspaper (`o_21_0000`: 3 or 19
      random numbers) and the city view (`o_19_0000`: re-seeds the generator from the city
      name and lays the city out with it) are in the engine (`newspaper`, `city_view_layout`
      in view.c) and verified against the original (tests `newspapers`, `city_views`); the
      palace screen's rules (`o_17_0000`, `palace_improve`); the city view drawn from
      CITYPIX/WONDERS/HILL/POP; leader portraits (KINGnn) for diplomacy; replay chronicle,
      final score, Hall of Fame (`save/FAME.TXT`), the closing picture.  (AI and diplomacy
      were finished in M3.)

## Porting conventions

- Behaviour must match the original exactly, including its bugs; each reproduced bug carries a
  `sic`/"the original ..." comment. Where the original reads memory that does not exist in the
  port (indexing past an array), the port uses the constant data the EXE has there when that is
  knowable (e.g. `civ_types[-1]`), otherwise 0, with a comment.
- 16-bit arithmetic is reproduced with `int16_t` casts where products can overflow.
- User interface is reached only through `ui.h`; `ui_headless.c` answers dialogs with the first
  option, as the oracle's stubbed dialog engine does.
- Functions not ported yet are in `src/todo.c` and abort if reached.
- Display code that consumes random numbers is part of the game state: redrawing the map view
  (`view_center`, f_1b05_0004) draws `rnd(256)`, and whether it is redrawn depends on the view
  position (`view_follow`, f_0860_1679), so both live in `src/view.c`, not behind `ui.h`.
  The newspaper (overlay 21) and city view (overlay 19) also draw random numbers; the oracle
  stubs them for now, so the port does not model them yet.
- Where the original reads uninitialised memory, the port picks a definition and the oracle
  is patched to do the same (`UNDEFINED_PATCHES` in `tests/run.py`).  So far: the transport
  code of `ai_unit_command` reads `city[l12]` with `l12` never set; the port uses the nearest
  city.  Where the original never terminates (a stack ring broken by `unit_unlink`'s handling
  of ships leaving a shared square makes the transport cargo walk loop for ever), the port
  bails out after 1000 steps and the test skips that case.
- Reads past the end of tables use `dgroup_init` (the EXE's initialised DGROUP, generated into
  `rules_data.c`) via `dgroup_word()`, e.g. unit-type fields of a deleted unit (type 255/-1).
- `tools/dsfield.py 0xf5b8 ...` names the saved field at a DGROUP offset.
- Some of the original's random numbers depend on the player's timing: animations that run
  until a key is pressed draw random numbers (the spaceship screen's stars, the talking
  leader in diplomacy dialogs).  The engine provides these as calls the front end makes per
  frame (`spaceship_stars_step`); their count cannot match a given playthrough of the
  original, only the logic can.
- The city view (overlay 19) re-seeds the random generator from the sum of the city name's
  characters and lays the city out with it; the newspaper (overlay 21) draws its crowd, name
  and headline at random.  Both are modelled in the engine (view.c); the oracle runs the
  original entry points up to their last random number and jumps to their epilogue
  (`Emu.ovl_jump`), skipping only drawing.  The palace screen (`o_17_0000`) is run in the
  oracle as "no section chosen" (straight to its final `redraw_map`), which is what the
  headless UI answers.
- The diplomacy meeting answers dialogs through `ui_envoy_*`; tests script the answers
  (`headless_script`) on both sides.
- Oracle details: the C runtime's stdio heap is not set up (startup code is skipped), so
  `load_text_section` is emulated in Python; overlay returns through root code are redirected
  through reload thunks found on the BP chain, as the real overlay manager must.
- The world generator works on screen page 1 and copies it to the map page; between, it
  builds `land_links` from page 2, which in the original holds whatever the intro animation
  drew.  The port builds the world in the map page directly, so the links come from the new
  world (the saved games' links look like this too).
- Redrawing the map (`redraw_map`) recentres the view and so draws a random number; every
  engine path that redraws calls `redraw_map()` (view.c), never a pure UI hook.  The human's
  own command handlers (in the original's civ_move_units) also redraw: the front end must do
  that through `redraw_map()`/`view_center()` too.
- Tests compare the random number state as well as the saved state.
- Screens that end with `redraw_map` in the original (so draw a random number): the score
  screen (`o_20_0c98` with its display, also inside the game-over sequence), the conquest
  replay (`o_09_0dde`), the intelligence report once a rival's details were opened
  (`o_14_0d3d`), the palace (`o_17_0000`), the revolution news.  Other advisors restore the
  saved screen without a redraw.  The engine or the front end calls `redraw_map()` there.
- Drawing a map square has a game effect: the original's square drawing (`f_1b05_11c6`, and
  `f_1b05_03b1` for visible cities in the view) calls `unit_at()`, which sets the global
  `stack_owner` (and clears stale unit markers in the map page).  Code after a redraw reads
  `stack_owner` (e.g. `unit_reveal` after a first-contact meeting redrew the map), so the
  engine performs these lookups wherever the original draws: `view_center` (in its dissolve
  order), `square_redraw` (11c6), `terrain_redraw` (03b1), `redraw_if_seen` (f_0402_3e8c),
  `redraw_unit_square` (f_0402_3ec3), `reveal_redraw`, `combat_display`, the city screen's
  map (`city_process` mode 1, in its own temporary view) and the human's blinking unit.
  The UI hooks only draw.
- The original autosaves (turn % 50 == 0) before its end-of-game check, so the autosave of
  turn 650 is already in the final year: loaded, it plays on for ever.  The port does the same.
- The oracle stubs pure displays: the other entry points of overlays 19 and 21, the wonder
  view `o_08_062a` and autosave `o_11_0368`.
- Stack rings broken by the original's `unit_unlink` (see above) also make the martial-law
  count in `city_happiness`, a diplomat walk in `ai_unit_command` and the civil-war unit
  transfer loop for ever in the original; the port stops each after 1000 steps, so games
  that hang the original (CIVIL0, CIVIL5-9 after a few turns) play on.
- Far segment 2786 holds, besides saved blocks, the AI's per-4x4-block table at 0x1498
  (`ai_block_flags`, rebuilt by ai_strategy).  `unk_far_2786_0000` / `_0fe8` are the power
  and war histories (8 civs x 150 samples, one every 4 turns) written by `civ_ranking`.

Front end, beyond the main screens: the palace drawing (o_17_07ec), Civilopedia articles
with the icon-page pictures (o_08_062a, also used as the wonder view), the cabinet of a new
government (o_05_1b8b), the discovery screen, the story of the world while it is made
(STORY.TXT over BIRTHn), the founding walk (o_22_0e87), the conquest march (o_19_166c), the
power graph (o_12_0567) and sound: `tools/extract_sound.py` runs the original PC-speaker
driver ISOUND.CVL in Unicorn exactly as the game's timer interrupt drives it (sequencer at
60 Hz, effects at 300 Hz) and records the speaker's pitch for all 45 sounds into
`src/sdl/sound_data.c`; the front end plays them back as a square wave.  The AdLib, Roland
and Tandy drivers (ASOUND/RSOUND/TSOUND.CVL) are not used.

Open questions: about 45 saved blocks are still `unk_*`; the city byte +26/27 and unit byte +8;
the (x+80, y+50) map layer; improvement bits 0x40/0x80.

## Lockstep harness (original vs port)

- `tools/dosmachine.py`: a DOS PC in Unicorn running the shipped CIV.EXE whole (EXEPACK unpacked
  in Python, DOS at int 21h, BIOS keyboard/video/timer, VGA DAC, int 33h with the game's event
  handler called for clicks).  Waiting for input = a blocking key read, or keyboard polls for 3
  s of virtual time.  `snapshot()`/`from_snapshot()` save the whole machine.
- `tools/lockstep.py --slot N --keys "..."`: boots the original to the load menu (cached in
  `build/lockstep/orig_loadmenu.snap`), sets the RNG to --seed, loads CIVILn; the port runs with
  `--play CIVILn.SVE CIVILn.MAP` and `$CIV_LOCKSTEP`/`$CIV_SEED`.  Each wait for input dumps the
  screen, .SVE bytes and RNG on both sides; `build/lockstep/report.html` shows them side by side
  with differing pixels and save fields (`save_field_name`).  `--port-only` reruns the port.
- Found with it: a loaded game resumes mid-turn (`g_ee80` = slot < 4 ? 1 : 2) with the view at
  (capital_x - 7, 19); the map/panel font is font 0 (mixed case); menu titles have a white key
  letter and grey rest; the unit panel shows the civ adjective.
- Original key facts: arrow scan codes (0x4D00) move units; keypad digits with NumLock on
  (0x4D36) scroll the map.
- Waiting for input (dosmachine): a blocking key read, or keyboard polls for 30 s of virtual
  time (longer than any animation, e.g. the 22 s founding walk); polls from the keyboard flush
  f_0402_44f5 do not count.  Mouse: the game installs an int 33h handler (ax=0Ch) and reads
  ds:5810/5812/5814/5816; a scripted click is a press and a release delivered at once.
- Scenarios: `tools/mksave.py IN.SVE IN.MAP OUT.SVE OUT.MAP field=value unitlayer:x,y,civ`
  edits a save (fields as save_field_name() names them); `lockstep.py --scenario DIR` plays
  DIR/CIVILn.* on both sides (the original reads build/dos_scratch first).  Slots 5-9 of
  orig/ cannot be used: the original hangs in their first turn.  tests/scenarios/attack: a
  legion next to a barbarian militia (slot 3).
- Reconstructed from the original with the harness (0 differing pixels apart from noted):
  the popup/menu widget (f_1d1e_003f/0483: stone texture SP299 (288,120), cyan highlight by
  recolouring 7->11 and 22->3, key letters in colour 0x8F); the four menus and ORDERS
  (f_1c9d_*: only what the unit can do); the city screen (cityscreen.c, hooks in city.c at the
  original's drawing points, input as at L4863); the city-name prompt (o_23); the city view's
  road pruning; Top 5 Cities (o_03_09ac); the fight display (f_1a19_000e); map clicks (right
  button: one-step GoTo to a neighbour, or the terrain's Civilopedia entry; left button: own
  city -> screen, own units -> the stack list f_0860_1f2a which wakes and activates one, else
  centre); the mouse pointer (TORCH.PIC, rows padded to an even width); the peace bar.
- The original's font numbers are 1-based (its font 2 is FONTS.CV font 1).  A byte 0x80+n in
  a string switches the text colour to n (the driver).
- advisors.c (0 differing pixels): the six advisors (o_14_*), Wonders (o_12_0801),
  Demographics (o_12_0d61), World Map (o_12_0000), Civilization Score (o_20_0c98, total kept
  in score[0]), the Civilopedia topic lists (o_08_0066, MSC stricmp order from "a").
- Palettes: the map's is SP256.PAL below 128 and BACKna/m.PAL above (f_0237_1be7, at every
  redraw_map); the advisors' portraits are GOVTna/m.PIC (0,0)-(160,60) copied into SP299
  (160,140) at load and on a change of government (o_05_1adf).  n = government / 2 (3 for
  communism), m once Invention is known (never for communism).
- The pointer is hidden on the full-screen pages (f_01a7_026a .. 0252); the peace bar shows
  the value at the last status report.
- dosmachine: DOS getch returns 0 then the scan code for an extended key.
- GAME menu (0 differing pixels): Tax/Luxuries rate (options 0..tax+sci, the current one
  highlighted via dialog_default = g_2f4a), FindCity (o_23_025b, prefix match any case),
  Options (check marks g_e712 -> '^', drawn over the still-open GAME menu: DLG_KEEP),
  Retire/Quit ("Are you sure you want to Quit?"), Save (o_11_0368: drive page with a random
  SP299 border (rnd(2)!), slots 0-3, result box with no highlight: DLG_NOSEL = g_e126) and
  Load (o_11_0000: all 10 slots).  A dialog at an odd y (but 139) has no box.
- The city screen's direct callers (city_process(i, 1) in the original) go through
  city_screen(): its loop and the re-pack of fortified units at the end.
- New game (`lockstep.py --newgame`, from the title menu; 0 differing pixels): title menu in a
  plain box (the stone texture g_2f48 only once a game is chosen), the world first with the
  story (o_07_176e, timed by the clock: not comparable), then o_05_0000's screens on DIFFS.PIC
  in font 7 (tribes by adjective), the intro page (*INIT + advances, the border drawn with the
  engine's frame_screen() style, the portrait at 134,20), Chieftain's labelled screen
  (o_02_17d2), help as a green "Civilization Note" (o_04_02d3, Instant Advice, BC only).
  world_seed is the startup random seed (f_1dde_0040): it places specials and huts.
- Animations (0 differing pixels apart from the pointer): a unit's move is f_0860_1d16 (17
  one-pixel steps, a 60 Hz tick each); a fight is f_1a19_000e (ticks of 1/60 s, not 55 ms).
- Newspaper (o_21_0000, tests/scenarios/government: 0 differing pixels): the page fills
  (0,0)-(320,100), the story word-wrapped by f_1f65_008e (40 x 8 px, at 16,40, font 4), the
  name in font 5 with ",-." ornaments when narrower than 200, EXTRA!/FLASH in font 3, the
  headline in font 2 at y 3, the crowd SP257 (176,128) 20x9 at (20i, 100 - rnd(2)), then
  "Press any key to continue." at (80,128).  city -2 (a new government) leaves the page for
  o_05_1b8b's cabinet below it (portraits SP299 (160+40k,140) -> (80k+20,118), titles without
  ':').  f_1f65_01b3 reads each line with "%[^\n]\n" and appends it with a ' ' after it
  (so a headline is centred with a trailing space; blank lines and leading blanks vanish).
- Discovery (research_complete, tests/scenarios/discovery: 0 differing pixels): with
  Animations (options & 8) DISCOVR1/2.PIC (2 once Electricity is known), the text boxless at
  (96, 1 / 25) (font 6 for wise men), the icon (o_08_16f7: clipped to x 319, then centred on
  (174, 96 / 87)) in ICONPG1.PAL with DISCOVR's colours 0..84; else, and for Future Tech, a
  newspaper.  Then the article o_08_062a, whose border is f_0860_25e1 (rnd(2): the engine
  draws it, ui_advance_screen/ui_wonder_view get the style).  o_08_062a: light grey 253 =
  (61,61,61) unless the picture's palette (f_1dde_047b: all but the 16 standard colours,
  45 = (51,39,25)) sets it; BLURBn "*NAME" as its own page with Civilopedia Text (options &
  64), then "*NAME2" (not for advances) and the facts.  Only ICONPG1.PAL exists for the
  advance pages.
- Dialog speakers (g_2f4e, once): 0-2 "Spies/Diplomats/Travelers report:", 3-6 the cabinet;
  the box grows 42 left and 8 up (at least 61 high), the portrait SP299 (40n+40,140) at
  (x-40, y-6, lower for tall boxes), the title at (x+5,y-4) in white underlined in cyan.
  g_2f50 adds "(HELP AVAILABLE)" (font 2, colour 10) bottom right.  The research choice is at
  (80,64) with the Science Advisor; the government choice at (100,64).
- redraw_map (f_0237_1b40) leaves the unit panel empty.
- Rivers (f_1b05_03b1): g_6d7e holds SP257 row 64 for 0-15 and row 80 for 16-31, and bit 8
  of the map layer at y + 150 adds 16 (the port had the rows the other way round: the "coast
  pixels" left at the load screen of CIVIL1).
- The sea's waves are palette cycling (f_1dde_05db: colours 96-103, 104-111, 112-127, one
  place every 15 timer ticks, f_0000_043c in the timer interrupt; f_1dde_0624 starts it at
  redraw_map, f_1dde_0656 stops it for full-screen pages).  gfx.c turns them while the
  palette holds the map's colours there; not in headless/lockstep runs (time-dependent).
- The test oracle stubs o_08_062a, but it consumes one rand() there (the border), as the
  game does; the lockstep harness showed it (tools/oracle.py: `article`).
- The turn loop marks each civ with a 6x6 square at (2,192): colour 8 before its cities and
  diplomacy, 15 after ai_strategy2 (ui_turn_marker); civ_move_units empties the unit panel at
  the end of the human's moves.
- Known differences left: the phase of the waves (palette cycling, timed); the land links of a new world
  (the original computes them from whatever is in page 2 at the time); the story's timing.
