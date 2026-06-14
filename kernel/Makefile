.SUFFIXES:

override OUTPUT := kernel

RELEASE = 0

GIT_ROOT  := $(shell git rev-parse --show-toplevel 2>/dev/null)
GIT_SHA   := $(shell git -C $(GIT_ROOT) rev-parse --short HEAD 2>/dev/null)

GIT_DIRTY := $(shell git -C $(GIT_ROOT) diff --quiet --ignore-submodules HEAD 2>/dev/null; \
    if [ $$? -ne 0 ]; then echo dirty; else echo clean; fi)

ifneq ($(RELEASE),1)
    VERSION := 0.1.2-dev-$(GIT_DIRTY)-$(GIT_SHA)
else
    VERSION := 0.1.2
endif

VER_BASE  := $(firstword $(subst -, ,$(VERSION)))
VER_NOTE  := $(patsubst $(VER_BASE)%,%,$(VERSION))
VER_MAJOR := $(word 1,$(subst ., ,$(VER_BASE)))
VER_MINOR := $(word 2,$(subst ., ,$(VER_BASE)))
VER_PATCH := $(word 3,$(subst ., ,$(VER_BASE)))
VER_NOTE_ESCAPED := $(shell printf '%s' '$(VER_NOTE)' | sed 's/"/\\"/g')

PREFIX := /usr/local

TOOLCHAIN :=
TOOLCHAIN_PREFIX :=
ifneq ($(TOOLCHAIN),)
    ifeq ($(TOOLCHAIN_PREFIX),)
        TOOLCHAIN_PREFIX := $(TOOLCHAIN)-
    endif
endif

ifneq ($(TOOLCHAIN_PREFIX),)
    CC := $(TOOLCHAIN_PREFIX)gcc
else
    CC := cc
endif

LD := $(TOOLCHAIN_PREFIX)ld

ifeq ($(TOOLCHAIN),llvm)
    CC := clang
    LD := ld.lld
endif

CFLAGS := -g -O2 -pipe
CPPFLAGS :=
NASMFLAGS := -g
LDFLAGS :=

ifneq ($(filter-out clean distclean,$(or $(MAKECMDGOALS),default)),)
    ifeq ($(wildcard .deps-obtained),)
        $(error Please run the ./conf/get-deps script first)
    endif
endif

override CC_IS_CLANG := $(shell ! $(CC) --version 2>/dev/null | grep -q '^Target: '; echo $$?)

ifeq ($(CC_IS_CLANG),1)
    override CC += -target x86_64-unknown-none-elf
endif

override CFLAGS += \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -nostdinc \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel

override CPPFLAGS := \
    -I include \
    -I limine-protocol/include \
    -I uACPI/include \
    -I Flanterm/src \
    -isystem freestnd-c-hdrs/include \
    $(CPPFLAGS) \
    -DVER_MAJOR=$(VER_MAJOR) \
    -DVER_MINOR=$(VER_MINOR) \
    -DVER_PATCH=$(VER_PATCH) \
    -DVER_NOTE=\"$(VER_NOTE_ESCAPED)\" \
    -MMD \
    -MP

override NASMFLAGS := \
    -f elf64 \
    $(patsubst -g,-g -F dwarf,$(NASMFLAGS)) \
    -Wall

override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    -T conf/linker.ld

override SRCFILES := $(shell find -L src cc-runtime/src uACPI/source Flanterm/src -type f 2>/dev/null | LC_ALL=C sort)
override CFILES := $(filter %.c,$(SRCFILES))
override ASFILES := $(filter %.S,$(SRCFILES))
override NASMFILES := $(filter %.asm,$(SRCFILES))

override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
override HEADER_DEPS := $(addprefix obj/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

.PHONY: all
all: bin/$(OUTPUT)

-include $(HEADER_DEPS)

bin/$(OUTPUT): Makefile conf/linker.ld $(OBJ)
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(OBJ) -o $@

obj/%.c.o: %.c Makefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.S.o: %.S Makefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.asm.o: %.asm Makefile
	mkdir -p "$(dir $@)"
	nasm $(NASMFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf bin obj

.PHONY: distclean
distclean: clean
	rm -rf .deps-obtained .cache compile_commands.json freestnd-c-hdrs cc-runtime limine-protocol uACPI

.PHONY: install
install: all
	install -d "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)"
	install -m 644 bin/$(OUTPUT) "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)/"

.PHONY: uninstall
uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)/$(OUTPUT)"
	-rmdir "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)"