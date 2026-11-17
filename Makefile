# Tile Puzzle Deluxe - Nintendo 3DS
# Needs devkitPro with the "3ds-dev" package group (devkitARM + libctru + citro2d + citro3d).
#
#   make        -> tilepuzzle.elf, tilepuzzle.smdh, tilepuzzle.3dsx
#   make test   -> runs the game-logic unit tests on your PC (needs gcc)
#   make clean
#
# The .cia is produced by the GitHub Actions workflow (.github/workflows/build.yml).

ifneq ($(MAKECMDGOALS),test)
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO first, e.g. export DEVKITPRO=/opt/devkitpro")
endif
ifeq ($(strip $(DEVKITARM)),)
$(error "Set DEVKITARM first, e.g. export DEVKITARM=$(DEVKITPRO)/devkitARM")
endif
endif

TARGET        := tilepuzzle
APP_TITLE     := Tile Puzzle Deluxe
APP_DESC      := Sliding tile puzzle with 45 levels and 4 modes
APP_AUTHOR    := fragahh
ICON          := assets/icon.png

SRC           := source/main.c source/game.c
OBJ           := $(patsubst source/%.c,build/%.o,$(SRC))

CTRULIB       := $(DEVKITPRO)/libctru
CC            := $(DEVKITARM)/bin/arm-none-eabi-gcc
SMDHTOOL      := $(DEVKITPRO)/tools/bin/smdhtool
DSXTOOL       := $(DEVKITPRO)/tools/bin/3dsxtool

ARCH          := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS        := -g -Wall -O2 -mword-relocations -ffunction-sections -std=gnu11 $(ARCH) \
                 -I$(CTRULIB)/include -Isource -D__3DS__ -D_GNU_SOURCE=1
LDFLAGS       := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib
LIBS          := -lcitro2d -lcitro3d -lctru -lm

.PHONY: all clean test

all: $(TARGET).3dsx

build/%.o: source/%.c source/game.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(TARGET).smdh: $(ICON)
	$(SMDHTOOL) --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	$(DSXTOOL) $(TARGET).elf $@ --smdh=$(TARGET).smdh

test:
	gcc -Wall -Wextra -std=gnu11 -Isource tests/test_game.c source/game.c -o build_test && ./build_test && rm -f build_test

clean:
	rm -rf build $(TARGET).elf $(TARGET).smdh $(TARGET).3dsx $(TARGET).cia banner.bnr build_test
