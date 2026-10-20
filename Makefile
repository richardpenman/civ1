# Uses the locally unpacked toolchain in .toolchain/ when present (see NOTES.md).
TC      := $(CURDIR)/.toolchain
ifneq ($(wildcard $(TC)/gcc),)
CC      := $(TC)/gcc
TCLIB   := $(TC)/root/usr/lib/x86_64-linux-gnu
LDFLAGS += -Wl,--disable-new-dtags -Wl,-rpath,$(TCLIB) -Wl,-rpath,$(TCLIB)/pulseaudio -Wl,-rpath-link,$(TCLIB)
endif
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c99 -pedantic
# The original game's files (make ORIG=dir, or $CIV_ORIG); the tools and tests read it too.
ORIG    ?= $(if $(CIV_ORIG),$(CIV_ORIG),$(HOME)/Dos/Civ1)
export CIV_ORIG := $(ORIG)

CORE    := src/rules_data.o src/pic.o src/save.o src/game.o src/map.o src/util.o src/civ.o src/city.o \
           src/unit.o src/ai.o src/diplomacy.o src/replay.o src/science.o src/production.o src/caravan.o src/turn.o src/combat.o src/move.o src/found.o src/goto.o src/view.o src/ai_unit.o src/ai_strategy.o src/endturn.o src/mapgen.o src/newgame.o src/spaceship.o src/meeting.o src/fresh_save.o src/todo.o src/ui_headless.o
CFLAGS  += -fPIC

# (the sources include <SDL2/SDL.h>: the directory above sdl2-config's include path)
SDLCFLAGS := $(if $(wildcard $(TC)/root/usr/include/SDL2),-I$(TC)/root/usr/include,$(shell sdl2-config --cflags 2>/dev/null) $(addprefix -I,$(addsuffix /include,$(shell sdl2-config --prefix 2>/dev/null))))
# (libpulse's own RUNPATH hides our RPATH from it: load its helper library up front)
PULSE     := $(wildcard $(TCLIB)/pulseaudio/libpulsecommon-*.so)
SDLLIBS   := $(or $(shell sdl2-config --libs 2>/dev/null),-lSDL2)
ifneq ($(wildcard $(TCLIB)/libSDL2.so),)
SDLLIBS   := -L$(TCLIB) -Wl,--no-as-needed $(PULSE) -Wl,--as-needed -lSDL2
endif
GUI_BASE  := src/sdl/gfx.o src/sdl/font.o src/sdl/assets.o src/sdl/mapview.o src/sdl/dialog.o
GUI       := $(GUI_BASE) src/sdl/cityscreen.o src/sdl/advisors.o src/sdl/cityview.o src/sdl/palace.o src/sdl/article.o src/sdl/sound.o src/sdl/sound_data.o

all: civdump libciv.so civ shot

# Shared build of the game logic, loaded by the oracle tests (tests/).
libciv.so: $(CORE) src/capi.o
	$(CC) -shared $(LDFLAGS) -o $@ $^ -ldl

civdump: $(CORE) src/capi.o src/tools/civdump.o
	$(CC) $(LDFLAGS) -o $@ $^ -ldl

shot: $(CORE) $(GUI_BASE) src/sdl/shot.o
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ $(SDLLIBS) -ldl -lm

civ: $(filter-out src/ui_headless.o,$(CORE)) $(GUI) src/sdl/ui_sdl.o src/sdl/main.o
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ $(SDLLIBS) -ldl -lm

src/sdl/%.o: src/sdl/%.c $(wildcard src/*.h src/sdl/*.h)
	$(CC) $(CFLAGS) $(SDLCFLAGS) -c -o $@ $<

# Data generated from the original game's files (not kept in the repository).
src/rules_data.c: tools/extract_rules.py $(ORIG)/CIV.EXE
	python3 -I tools/extract_rules.py $(ORIG)/CIV.EXE $@

src/fresh_save.c: tools/extract_fresh.py $(ORIG)/CIV.EXE build/asm/symbols.txt
	python3 -I tools/extract_fresh.py $@

# The disassembly (build/asm/*.asm, symbols.txt): the emulator-based tools and tests need it.
build/asm/symbols.txt: tools/disasm.py tools/symbols.map $(ORIG)/CIV.EXE
	python3 -I tools/disasm.py $(ORIG)/CIV.EXE build/asm

src/sdl/sound_data.c: tools/extract_sound.py $(ORIG)/ISOUND.CVL
	python3 -I tools/extract_sound.py $(ORIG)/ISOUND.CVL $@

%.o: %.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

test: civdump libciv.so build/asm/symbols.txt
	@for s in $(ORIG)/CIVIL*.SVE; do m=$${s%.SVE}.MAP; ./civdump $$s $$m | tail -1 | sed "s|^|$$s: |"; done
	python3 -I tests/run.py

smoke: civ
	sh tests/smoke.sh

clean:
	rm -f civdump libciv.so civ shot src/*.o src/tools/*.o src/sdl/*.o

.PHONY: all test smoke clean
