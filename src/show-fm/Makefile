PLUGIN = show-fm${PLUGIN_SUFFIX}

SRCS = show-fm.c

include ../../buildsys.mk
include ../../extra.mk

plugindir := ${plugindir}/${GENERAL_PLUGIN_DIR}

CPPFLAGS += -I../.. ${GTK_CFLAGS}
CFLAGS += ${GLIB_CFLAGS} ${PLUGIN_CFLAGS}
LIBS += -lm ${GLIB_LIBS} ${GTK_LIBS}
