libclutterincludedir = $(includedir)/clutter-@CLUTTER_API_VERSION@/clutter
libclutterinclude_HEADERS = clutter-win32.h

clutter-win32-$(CLUTTER_API_VERSION).pc: clutter-win32.pc
	@cp -f $< $(@F)

pkgconfig_DATA = clutter-win32-@CLUTTER_API_VERSION@.pc
pkgconfigdir   = $(libdir)/pkgconfig

AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"ClutterWin32\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/clutter \
	-I$(top_builddir)/clutter \
	$(CLUTTER_CFLAGS) \
	$(CLUTTER_DEBUG_CFLAGS) \
	$(GCC_FLAGS)

LDADD = $(CLUTTER_LIBS)

noinst_LTLIBRARIES = libclutter-win32.la

libclutter_win32_la_SOURCES = \
	clutter-backend-win32.h \
	clutter-backend-win32.c \
	clutter-event-win32.c \
	clutter-stage-win32.h \
	clutter-stage-win32.c \
	clutter-win32.h

CLEANFILES = clutter-win32-$(CLUTTER_API_VERSION).pc

EXTRA_DIST = clutter-win32.pc.in
