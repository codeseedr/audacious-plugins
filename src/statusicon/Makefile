PLUGIN = statusicon${PLUGIN_SUFFIX}

SRCS = statusicon.cc \
       util.cc

include ../../buildsys.mk
include ../../extra.mk

plugindir := ${plugindir}/${GENERAL_PLUGIN_DIR}

LD = ${CXX}
CFLAGS += ${PLUGIN_CFLAGS}
CPPFLAGS += ${PLUGIN_CPPFLAGS} ${GTK_CFLAGS} ${GLIB_CFLAGS}  -I../..
LIBS += ${GTK_LIBS} ${GLIB_LIBS} -laudgui
