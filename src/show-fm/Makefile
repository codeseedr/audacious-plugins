PLUGIN = show-fm${PLUGIN_SUFFIX}

SRCS = show-fm.cc

include ../../buildsys.mk
include ../../extra.mk

plugindir := ${plugindir}/${GENERAL_PLUGIN_DIR}

LD = ${CXX}
CFLAGS += ${GLIB_CFLAGS} ${PLUGIN_CFLAGS}
CPPFLAGS += ${PLUGIN_CPPFLAGS} ${GTK_CFLAGS} -I../..
LIBS += -lm ${GTK_LIBS} ${GLIB_LIBS} -laudgui
