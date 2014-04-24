/*
 * plugin-window.c
 * Copyright 2014 John Lindgren
 *
 * This file is part of Audacious.
 *
 * Audacious is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2 or version 3 of the License.
 *
 * Audacious is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Audacious. If not, see <http://www.gnu.org/licenses/>.
 *
 * The Audacious team does not consider modular code linking to Audacious or
 * using our public API to be a derived work.
 */

/* TODO (someday): implement proper docking for plugin windows */

#include "plugin-window.h"

#include <gtk/gtk.h>

#include <libaudcore/interface.h>
#include <libaudcore/plugin.h>
#include <libaudcore/plugins.h>
#include <libaudcore/hook.h>

static GList * windows;

static bool_t delete_cb (GtkWidget * window, GdkEvent * event, PluginHandle * plugin)
{
    aud_plugin_enable (plugin, FALSE);
    return TRUE;
}

static bool_t add_dock_plugin (PluginHandle * plugin, void * unused)
{
    GtkWidget * widget = aud_plugin_get_widget (plugin);

    if (widget)
    {
        GtkWidget * window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title ((GtkWindow *) window, aud_plugin_get_name (plugin));
        gtk_window_set_default_size ((GtkWindow *) window, 300, 200);
        gtk_window_set_has_resize_grip ((GtkWindow *) window, FALSE);
        gtk_container_add ((GtkContainer *) window, widget);

        g_object_set_data ((GObject *) window, "skins-plugin-id", plugin);
        g_signal_connect (window, "delete-event", (GCallback) delete_cb, plugin);

        windows = g_list_prepend (windows, window);

        if (aud_ui_is_shown ())
            gtk_widget_show_all (window);
    }

    return TRUE;
}

static int find_cb (GtkWidget * window, PluginHandle * plugin)
{
    return (g_object_get_data ((GObject *) window, "skins-plugin-id") != plugin);
}

static bool_t remove_dock_plugin (PluginHandle * plugin, void * unused)
{
    GList * node = g_list_find_custom (windows, plugin, (GCompareFunc) find_cb);

    if (node)
    {
        gtk_widget_destroy (node->data);
        windows = g_list_delete_link (windows, node);
    }

    return TRUE;
}

void create_plugin_windows (void)
{
    aud_plugin_for_enabled (PLUGIN_TYPE_GENERAL, add_dock_plugin, NULL);
    aud_plugin_for_enabled (PLUGIN_TYPE_VIS, add_dock_plugin, NULL);

    hook_associate ("dock plugin enabled", (HookFunction) add_dock_plugin, NULL);
    hook_associate ("dock plugin disabled", (HookFunction) remove_dock_plugin, NULL);
}

void show_plugin_windows (void)
{
    g_list_foreach (windows, (GFunc) gtk_widget_show_all, NULL);
}

void hide_plugin_windows (void)
{
    g_list_foreach (windows, (GFunc) gtk_widget_hide, NULL);
}

void destroy_plugin_windows (void)
{
    aud_plugin_for_enabled (PLUGIN_TYPE_GENERAL, remove_dock_plugin, NULL);
    aud_plugin_for_enabled (PLUGIN_TYPE_VIS, remove_dock_plugin, NULL);

    hook_dissociate ("dock plugin enabled", (HookFunction) add_dock_plugin);
    hook_dissociate ("dock plugin disabled", (HookFunction) remove_dock_plugin);

    g_warn_if_fail (! windows);
}
