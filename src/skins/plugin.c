/*
 * Audacious - a cross-platform multimedia player
 * Copyright (c) 2008 Tomasz Moń
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 * The Audacious team does not consider modular code linking to
 * Audacious or using our public API to be a derived work.
 */

#include <stdlib.h>

#include <libaudcore/drct.h>
#include <libaudcore/i18n.h>
#include <libaudcore/runtime.h>
#include <libaudcore/plugin.h>
#include <libaudcore/hook.h>
#include <libaudgui/libaudgui.h>

#include "menus.h"
#include "plugin.h"
#include "plugin-window.h"
#include "preset-browser.h"
#include "preset-list.h"
#include "skins_cfg.h"
#include "ui_equalizer.h"
#include "ui_main.h"
#include "ui_main_evlisteners.h"
#include "ui_playlist.h"
#include "ui_skin.h"
#include "view.h"

gchar * skins_paths[SKINS_PATH_COUNT];

static gboolean skins_init (void);
static void skins_cleanup (void);

AUD_IFACE_PLUGIN
(
    .name = N_("Winamp Classic Interface"),
    .domain = PACKAGE,
    .init = skins_init,
    .cleanup = skins_cleanup,
    .prefs = & skins_prefs,

    .show = view_show_player,
    .run = gtk_main,
    .quit = gtk_main_quit,

    .show_about_window = audgui_show_about_window,
    .hide_about_window = audgui_hide_about_window,
    .show_filebrowser = audgui_run_filebrowser,
    .hide_filebrowser = audgui_hide_filebrowser,
    .show_jump_to_song = audgui_jump_to_track,
    .hide_jump_to_song = audgui_jump_to_track_hide,
    .show_prefs_window = audgui_show_prefs_window,
    .hide_prefs_window = audgui_hide_prefs_window,
    .plugin_menu_add = audgui_plugin_menu_add,
    .plugin_menu_remove = audgui_plugin_menu_remove
)

static gint update_source;

static void skins_free_paths(void) {
    int i;

    for (i = 0; i < SKINS_PATH_COUNT; i++)  {
        g_free(skins_paths[i]);
        skins_paths[i] = NULL;
    }
}

static void skins_init_paths() {
    char *xdg_data_home;
    char *xdg_cache_home;

    xdg_data_home = (getenv("XDG_DATA_HOME") == NULL
        ? g_build_filename(g_get_home_dir(), ".local", "share", NULL)
        : g_strdup(getenv("XDG_DATA_HOME")));
    xdg_cache_home = (getenv("XDG_CACHE_HOME") == NULL
        ? g_build_filename(g_get_home_dir(), ".cache", NULL)
        : g_strdup(getenv("XDG_CACHE_HOME")));

    skins_paths[SKINS_PATH_USER_SKIN_DIR] =
        g_build_filename(xdg_data_home, "audacious", "Skins", NULL);
    skins_paths[SKINS_PATH_SKIN_THUMB_DIR] =
        g_build_filename(xdg_cache_home, "audacious", "thumbs", NULL);

    g_free(xdg_data_home);
    g_free(xdg_cache_home);
}

static gboolean update_cb (void * unused)
{
    mainwin_update_song_info ();
    return TRUE;
}

static gboolean skins_init (void)
{
    audgui_init ();

    skins_init_paths();
    skins_cfg_load();

    menu_init ();

    char * skin = aud_get_str ("skins", "skin");
    init_skins (skin);
    str_unref (skin);

    view_apply_on_top ();
    view_apply_sticky ();

    if (aud_drct_get_playing ())
    {
        ui_main_evlistener_playback_begin (NULL, NULL);
        if (aud_drct_get_paused ())
            ui_main_evlistener_playback_pause (NULL, NULL);
    }
    else
        mainwin_update_song_info ();

    update_source = g_timeout_add (250, update_cb, NULL);

    create_plugin_windows ();

    return TRUE;
}

static void skins_cleanup (void)
{
    destroy_plugin_windows ();

    mainwin_unhook ();
    playlistwin_unhook ();
    g_source_remove (update_source);

    skins_cfg_save();

    cleanup_skins();
    skins_free_paths();

    eq_preset_browser_cleanup ();
    eq_preset_list_cleanup ();

    menu_cleanup ();

    audgui_cleanup ();
}

bool_t handle_window_close (void)
{
    bool_t handled = FALSE;
    hook_call ("window close", & handled);

    if (! handled)
        aud_quit ();

    return TRUE;
}
