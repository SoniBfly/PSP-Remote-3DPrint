# PSP Remote 3DPrint — PSP homebrew (pspdev classic Makefile)
#
# Layout: everything generated goes to build/
#   build/temp/  intermediates: www/ (Vite output), www.pak, www_pak.o, obj/*.o,
#                flsun_remote.elf, PARAM.SFO, EBOOT.PBP (+ old/ archived packages)
#   build/dist/  final deliverable: EBOOT.PBP
TMPDIR_B  = build/temp
OBJDIR    = $(TMPDIR_B)/obj
DISTDIR   = build/dist

TARGET = $(TMPDIR_B)/flsun_remote
OBJS = $(OBJDIR)/main.o \
       $(OBJDIR)/net.o $(OBJDIR)/socket.o $(OBJDIR)/printer_mks.o $(OBJDIR)/httpd.o $(OBJDIR)/worker.o \
       $(OBJDIR)/model_storage.o $(OBJDIR)/printer.o $(OBJDIR)/session.o $(OBJDIR)/screen.o \
       $(OBJDIR)/preview.o $(OBJDIR)/splash.o $(OBJDIR)/render.o $(OBJDIR)/text.o $(OBJDIR)/widget.o \
       $(OBJDIR)/progress.o $(OBJDIR)/value.o $(OBJDIR)/common.o $(OBJDIR)/models.o \
       $(OBJDIR)/tune.o $(OBJDIR)/dash.o $(OBJDIR)/preview_screen.o $(OBJDIR)/confirm.o \
       $(OBJDIR)/log.o $(OBJDIR)/config.o $(OBJDIR)/key_control.o $(OBJDIR)/www_bundle.o $(TMPDIR_B)/www_pak.o

# Source search path for staged layout (core/net/ui).
vpath %.c src src/core src/net src/ui src/ui/base src/ui/screens

CFLAGS = -O2 -G0 -Wall -I src -I src/core -I src/net -I src/ui -I src/ui/base -I src/ui/screens
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

# make DEBUG=1 → card logging (log.txt) + keeps runtime log=net|off
ifeq ($(DEBUG),1)
CFLAGS += -DFLSUN_DEBUG
endif

# Modern SDK specs place these stubs after libc glue. Duplicating them earlier
# can split import tables; retain explicit extras only for older toolchains.
SDK_DEFAULT_LIBS := $(shell psp-gcc -dumpspecs)
LIBS = -lpspwlan
ifeq ($(filter -lpspnet_inet,$(SDK_DEFAULT_LIBS)),)
LIBS += -lpspnet_inet
endif
ifeq ($(filter -lpsputility,$(SDK_DEFAULT_LIBS)),)
LIBS += -lpsputility
endif

# build.mak overridable outputs: keep PARAM.SFO / EBOOT.PBP out of the project root.
PSP_EBOOT_SFO = $(TMPDIR_B)/PARAM.SFO
PSP_EBOOT     = $(TMPDIR_B)/EBOOT.PBP
EXTRA_TARGETS = $(PSP_EBOOT)
PSP_EBOOT_TITLE = PSP Remote 3DPrint
PSP_EBOOT_ICON = assets/icon0.png

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

.PHONY: dist clean-all www-pak FORCE

# Objects live in build/temp/obj/; vpath finds sources under src/ and subdirs.
# Changing DEBUG must rebuild objects; generated dependency files track header edits.
FORCE:

$(OBJDIR)/.build_mode: FORCE | $(OBJDIR)
	@printf '%s\n' '$(if $(filter 1,$(DEBUG)),debug,release)' | cmp -s - $@ || \
	  printf '%s\n' '$(if $(filter 1,$(DEBUG)),debug,release)' > $@

$(OBJDIR)/%.o: %.c $(OBJDIR)/.build_mode | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(wildcard $(OBJDIR)/*.d)

$(OBJDIR) $(DISTDIR):
	mkdir -p $@

$(OBJDIR)/splash.o: src/ui/splash_art.h

$(PSP_EBOOT): assets/icon0.png

$(PSP_EBOOT_SFO): | $(OBJDIR)

# Pack Vite build/temp/www/ into FLWW blob linked into the ELF (single-file phone deploy).
www-pak: $(TMPDIR_B)/www.pak src/net/www_bundle_meta.h

# index.html is rewritten by every Vite build, so a fresh web build re-packs.
$(TMPDIR_B)/www.pak: scripts/pack_www.py $(wildcard $(TMPDIR_B)/www/index.html)
	@if [ ! -f $(TMPDIR_B)/www/index.html ]; then \
	  echo "$(TMPDIR_B)/www/index.html missing — run: cd web && npm run build" >&2; \
	  exit 1; \
	fi
	python3 scripts/pack_www.py

# A single producer runs before either consumer, including with parallel make.
src/net/www_bundle_meta.h: $(TMPDIR_B)/www.pak
	python3 scripts/pack_www.py

# cd keeps the objcopy symbol names (_binary_www_pak_*) unchanged.
$(TMPDIR_B)/www_pak.o: $(TMPDIR_B)/www.pak src/net/www_bundle_meta.h
	cd $(TMPDIR_B) && psp-objcopy -I binary -O elf32-littlemips -B mips www.pak www_pak.o

$(OBJDIR)/www_bundle.o: src/net/www_bundle_meta.h

dist: $(PSP_EBOOT) | $(DISTDIR)
	cp -f $(PSP_EBOOT) $(DISTDIR)/EBOOT.PBP
	@echo "→ $(DISTDIR)/EBOOT.PBP (web files checked at startup; changed files exported)"
	@ls -la $(DISTDIR)/EBOOT.PBP

# Leaves build/temp/old/ (archived packages) alone.
clean-all: clean
	rm -f $(DISTDIR)/EBOOT.PBP
	rm -f $(TMPDIR_B)/www.pak $(TMPDIR_B)/www_pak.o src/net/www_bundle_meta.h
	rm -rf $(OBJDIR) $(TMPDIR_B)/www
