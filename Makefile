# Makefile for MCE
# Copyright © 2004-2011 Nokia Corporation.
# Written by David Weinehall
# Modified by Tuomo Tanskanen
#
# mce is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License
# version 2.1 as published by the Free Software Foundation.
#
# mce is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with mce.  If not, see <http://www.gnu.org/licenses/>.

VERSION := 1.12.5

INSTALL := install --mode=755
INSTALL_DIR := install -d
INSTALL_DATA := install --mode=644

DOXYGEN := doxygen

# Whether to enable wakelock compatibility code
ENABLE_WAKELOCKS ?= n

# Whether to enable sysinfod queries
ENABLE_SYSINFOD_QUERIES ?= n

# Whether to use builtin-gconf instead of the real thing
ENABLE_BUILTIN_GCONF ?= y

VARDIR := $(DESTDIR)/var/lib/mce
RUNDIR := $(DESTDIR)/var/run/mce
CONFDIR := /etc/mce
CONFINSTDIR := $(DESTDIR)$(CONFDIR)
SBINDIR := $(DESTDIR)/sbin
MODULEDIR := $(DESTDIR)/usr/lib/mce/modules
DBUSDIR := $(DESTDIR)/etc/dbus-1/system.d
LOCALEDIR := $(DESTDIR)/usr/share/locale
GCONFSCHEMADIR := $(DESTDIR)/etc/gconf/schemas
BACKUPCONFDIR := $(DESTDIR)/usr/share/backup-framework/applications
HELPERSCRIPTDIR := $(DESTDIR)/usr/share/mce
DEVICECLEARSCRIPTDIR := $(DESTDIR)/etc/osso-cud-scripts
FACTORYRESETSCRIPTDIR := $(DESTDIR)/etc/osso-rfs-scripts

TOPDIR := .
DOCDIR := $(TOPDIR)/doc
TOOLDIR := $(TOPDIR)/tools
TESTSDIR := $(TOPDIR)/tests
MODULE_DIR := $(TOPDIR)/modules

TOOLS := \
	$(TOOLDIR)/mcetool
TESTS := \
	$(TESTSDIR)/mcetorture
TARGETS := \
	mce
MODULES := \
	$(MODULE_DIR)/libradiostates.so \
	$(MODULE_DIR)/libfilter-brightness-als.so \
	$(MODULE_DIR)/libfilter-brightness-simple.so \
	$(MODULE_DIR)/libproximity.so \
	$(MODULE_DIR)/libkeypad.so \
	$(MODULE_DIR)/libinactivity.so \
	$(MODULE_DIR)/libcamera.so \
	$(MODULE_DIR)/libalarm.so \
	$(MODULE_DIR)/libbattery.so \
	$(MODULE_DIR)/libdisplay.so \
	$(MODULE_DIR)/libled.so \
	$(MODULE_DIR)/libcallstate.so \
	$(MODULE_DIR)/libaudiorouting.so \
	$(MODULE_DIR)/libpowersavemode.so
CONFFILE := mce.ini
RADIOSTATESCONFFILE := mce-radio-states.ini
COLORPROFILESCONFFILE := mce-color-profiles.ini
DBUSCONF := mce.conf
GCONFSCHEMAS := display.schemas energymanagement.schemas
BACKUPCONF := mcebackup.conf
BACKUPSCRIPTS := mce-backup mce-restore
PRIVILEGEDDEVICECLEARSCRIPT := mce-device-clear
REGULARDEVICECLEARSCRIPT := mce-device-clear.sh

WARNINGS := -Wextra -Wall -Wpointer-arith -Wundef -Wcast-align -Wshadow
WARNINGS += -Wbad-function-cast -Wwrite-strings -Wsign-compare
WARNINGS += -Waggregate-return -Wmissing-noreturn -Wnested-externs
WARNINGS += -Wchar-subscripts -Wmissing-prototypes -Wformat-security
WARNINGS += -Wformat=2 -Wformat-nonliteral -Winit-self
WARNINGS += -Wswitch-default -Wstrict-prototypes
WARNINGS += -Wdeclaration-after-statement
WARNINGS += -Wold-style-definition -Wmissing-declarations
WARNINGS += -Wmissing-include-dirs -Wstrict-aliasing=2
WARNINGS += -Wunsafe-loop-optimizations -Winvalid-pch
WARNINGS += -Waddress -Wvolatile-register-var
WARNINGS += -Wmissing-format-attribute
WARNINGS += -Wstack-protector
#WARNINGS += -Werror
WARNINGS += -Wno-declaration-after-statement

COMMON_CFLAGS := -D_GNU_SOURCE
COMMON_CFLAGS += -std=c99
COMMON_CFLAGS += -I. $(WARNINGS)
COMMON_CFLAGS += -DG_DISABLE_DEPRECATED
COMMON_CFLAGS += -DOSSOLOG_COMPILE
COMMON_CFLAGS += -DMCE_VAR_DIR=$(VARDIR) -DMCE_RUN_DIR=$(RUNDIR)
COMMON_CFLAGS += -DPRG_VERSION=$(VERSION)

ifeq ($(strip $(ENABLE_WAKELOCKS)),y)
COMMON_CFLAGS += -DENABLE_WAKELOCKS
endif

ifeq ($(strip $(ENABLE_SYSINFOD_QUERIES)),y)
COMMON_CFLAGS += -DENABLE_SYSINFOD_QUERIES
endif

ifeq ($(strip $(ENABLE_BUILTIN_GCONF)),y)
COMMON_CFLAGS += -DENABLE_BUILTIN_GCONF
endif

# allow "make clean" outside sdk chroot to work without warnings
# about missing pkg-config files
ifeq ($(MAKECMDGOALS),clean)
PKG_CONFIG = true
endif
PKG_CONFIG ?= pkg-config

MCE_CFLAGS := $(COMMON_CFLAGS)
MCE_CFLAGS += -DMCE_CONF_FILE=$(CONFDIR)/$(CONFFILE)
MCE_CFLAGS += $(shell $(PKG_CONFIG) --cflags gobject-2.0 glib-2.0 gio-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0)

MCE_LDFLAGS := $(shell $(PKG_CONFIG) --libs  gobject-2.0 glib-2.0 gio-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0 dsme)

LIBS := tklock.c modetransition.c powerkey.c mce-dbus.c mce-dsme.c mce-gconf.c event-input.c event-switches.c mce-hal.c mce-log.c mce-conf.c datapipe.c mce-modules.c mce-io.c mce-lib.c

# HACK: do not link against libgconf-2
ifeq ($(strip $(ENABLE_BUILTIN_GCONF)),y)
LIBS        += builtin-gconf.c
MCE_LDFLAGS := $(filter-out -lgconf-2, $(MCE_LDFLAGS))
endif

HEADERS := tklock.h modetransition.h powerkey.h mce.h mce-dbus.h mce-dsme.h mce-gconf.h event-input.h event-switches.h mce-hal.h mce-log.h mce-conf.h datapipe.h mce-modules.h mce-io.h mce-lib.h

ifeq ($(strip $(ENABLE_WAKELOCKS)),y)
LIBS    += libwakelock.c
HEADERS += libwakelock.h
endif

MODULE_CFLAGS := $(COMMON_CFLAGS)
MODULE_CFLAGS += -fPIC -shared
MODULE_CFLAGS += -I.
MODULE_CFLAGS += -DMCE_RADIO_STATES_CONF_FILE=$(CONFDIR)/$(RADIOSTATESCONFFILE)
MODULE_CFLAGS += -DMCE_COLOR_PROFILES_CONF_FILE=$(CONFDIR)/$(COLORPROFILESCONFFILE)
MODULE_CFLAGS += $(shell $(PKG_CONFIG) --cflags gobject-2.0 glib-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0)

MODULE_LDFLAGS := $(shell $(PKG_CONFIG)  --libs gobject-2.0 glib-2.0 gmodule-2.0 dbus-1 dbus-glib-1 gconf-2.0)

MODULE_LIBS := datapipe.c mce-hal.c mce-log.c mce-dbus.c mce-conf.c mce-gconf.c median_filter.c mce-lib.c

# HACK: do not link against libgconf-2
ifeq ($(strip $(ENABLE_BUILTIN_GCONF)),y)
MODULE_LIBS    += builtin-gconf.c
MODULE_LDFLAGS := $(filter-out -lgconf-2, $(MODULE_LDFLAGS))
endif

MODULE_HEADERS := datapipe.h mce-hal.h mce-log.h mce-dbus.h mce-conf.h mce-gconf.h mce.h median_filter.h mce-lib.h

TOOLS_CFLAGS := $(COMMON_CFLAGS)
TOOLS_CFLAGS += -I.
TOOLS_CFLAGS += $(shell $(PKG_CONFIG) --cflags gobject-2.0 glib-2.0 dbus-1 gconf-2.0)

TOOLS_LDFLAGS := $(shell $(PKG_CONFIG)  --libs gobject-2.0 glib-2.0 dbus-1 gconf-2.0)

TOOLS_HEADERS := tklock.h mce-dsme.h tools/mcetool.h

# HACK: do not link against libgconf-2
ifeq ($(strip $(ENABLE_BUILTIN_GCONF)),y)
TOOLS_LDFLAGS := $(filter-out -lgconf-2, $(TOOLS_LDFLAGS))
endif

.PHONY: all
all: $(TARGETS) $(MODULES) $(TOOLS)

.PHONY: targets
targets: $(TARGETS)

$(TARGETS): %: %.c $(HEADERS) $(LIBS)
	@$(CC) $(CFLAGS) $(MCE_CFLAGS) -o $@ $< $(LIBS) $(LDFLAGS) $(MCE_LDFLAGS)

.PHONY: modules
modules: $(MODULES)

$(MODULES): $(MODULE_DIR)/lib%.so: $(MODULE_DIR)/%.c $(MODULE_HEADERS) $(MODULE_LIBS)
	@$(CC) $(CFLAGS) $(MODULE_CFLAGS) -o $@ $< $(MODULE_LIBS) $(LDFLAGS) $(MODULE_LDFLAGS)

.PHONY: tools
tools: $(TOOLS)

$(TOOLS): %: %.c $(TOOLS_HEADERS)
	@$(CC) $(CFLAGS) $(TOOLS_CFLAGS) -o $@ $< $(LDFLAGS) mce-log.c $(TOOLS_LDFLAGS)

.PHONY: tags
tags:
	@find . $(MODULE_DIR) -maxdepth 1 -type f -name '*.[ch]' | xargs ctags -a --extra=+f

.PHONY: doc
doc:
	@$(DOXYGEN) 2> $(DOCDIR)/warnings > /dev/null

.PHONY: install
install: all
	$(INSTALL_DIR) $(SBINDIR) $(DBUSDIR) $(VARDIR) $(MODULEDIR)	&&\
	$(INSTALL_DIR) $(RUNDIR) $(CONFINSTDIR) $(GCONFSCHEMADIR)	&&\
	$(INSTALL_DIR) $(BACKUPCONFDIR) $(HELPERSCRIPTDIR)		&&\
	$(INSTALL_DIR) $(DEVICECLEARSCRIPTDIR) $(FACTORYRESETSCRIPTDIR)	&&\
	$(INSTALL) $(TARGETS) $(SBINDIR)				&&\
	$(INSTALL) $(TOOLS) $(TESTS) $(SBINDIR)				&&\
	$(INSTALL) $(MODULES) $(MODULEDIR)				&&\
	$(INSTALL) $(BACKUPSCRIPTS) $(HELPERSCRIPTDIR)			&&\
	$(INSTALL) $(PRIVILEGEDDEVICECLEARSCRIPT) $(HELPERSCRIPTDIR)	&&\
	$(INSTALL) $(REGULARDEVICECLEARSCRIPT) $(DEVICECLEARSCRIPTDIR)	&&\
	$(INSTALL) $(REGULARDEVICECLEARSCRIPT) $(FACTORYRESETSCRIPTDIR)	&&\
	$(INSTALL_DATA) $(CONFFILE) $(CONFINSTDIR)			&&\
	$(INSTALL_DATA) $(RADIOSTATESCONFFILE) $(CONFINSTDIR)		&&\
	$(INSTALL_DATA) $(GCONFSCHEMAS) $(GCONFSCHEMADIR)		&&\
	$(INSTALL_DATA) $(DBUSCONF) $(DBUSDIR)				&&\
	$(INSTALL_DATA) $(BACKUPCONF) $(BACKUPCONFDIR)

.PHONY: fixme
fixme:
	@find . -type f -name "*.[ch]" | xargs grep -E "FIXME|XXX|TODO"

.PHONY: clean
clean:
	@rm -f $(TARGETS) $(TOOLS) $(MODULES)
	@if [ x"$(DOCDIR)" != x"" ] && [ -d "$(DOCDIR)" ]; then		\
		rm -rf $(DOCDIR)/*;					\
	fi

.PHONY: distclean
distclean: clean
	@rm -f tags
