libclutterincludedir = $(includedir)/clutter-@CLUTTER_API_VERSION@/cogl
libclutterinclude_HEADERS = $(top_builddir)/clutter/cogl/cogl.h \
                            $(top_builddir)/clutter/cogl/cogl-defines-gles.h

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/clutter \
	-I$(top_srcdir)/clutter/cogl \
	-I$(top_srcdir)/clutter/cogl/common \
	-I$(top_srcdir)/clutter/cogl/$(CLUTTER_COGL) \
	-I$(top_builddir)/clutter \
	-I$(top_builddir)/clutter/cogl \
	$(CLUTTER_CFLAGS) \
	$(CLUTTER_DEBUG_CFLAGS) \
	$(GCC_FLAGS)

LDADD = $(CLUTTER_LIBS)

noinst_LTLIBRARIES = libclutter-cogl.la

libclutter_cogl_la_SOURCES = \
	$(top_builddir)/clutter/cogl/cogl.h \
	$(top_builddir)/clutter/cogl/cogl-defines-gles.h \
	cogl-internal.h \
	cogl-texture.h \
	cogl-fbo.h \
	cogl-context.h \
	cogl.c \
	cogl-primitives.c \
	cogl-texture.c \
	cogl-fbo.c \
	cogl-context.c \
	cogl-gles2-wrapper.h \
	cogl-program.h \
	cogl-program.c \
	cogl-shader.h \
	cogl-shader.c

if USE_GLES2_WRAPPER
libclutter_cogl_la_SOURCES += \
	cogl-gles2-wrapper.c \
	cogl-fixed-vertex-shader.h \
	cogl-fixed-vertex-shader.c \
	cogl-fixed-fragment-shader.h \
	cogl-fixed-fragment-shader.c 		
endif

EXTRA_DIST = cogl-defines.h.in \
	stringify.sh \
	cogl-fixed-vertex-shader.glsl \
	cogl-fixed-fragment-shader.glsl

libclutter_cogl_la_LIBADD =  $(top_builddir)/clutter/cogl/common/libclutter-cogl-common.la

.glsl.h :
	/bin/sh $(top_srcdir)/clutter/cogl/gles/stringify.sh -h $<

.glsl.c :
	/bin/sh $(top_srcdir)/clutter/cogl/gles/stringify.sh $<

cogl-gles2-wrapper.lo : cogl-fixed-vertex-shader.h cogl-fixed-fragment-shader.h
