/*
 * menus.c
 * Copyright 2011-2014 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <libaudcore/drct.h>
#include <libaudcore/i18n.h>
#include <libaudcore/interface.h>
#include <libaudcore/playlist.h>
#include <libaudcore/plugins.h>
#include <libaudcore/runtime.h>
#include <libaudgui/libaudgui.h>
#include <libaudgui/libaudgui-gtk.h>
#include <libaudgui/menu.h>

#include "gtkui.h"
#include "playlist_util.h"
#include "ui_playlist_notebook.h"
#include "ui_playlist_widget.h"

#define SHIFT GDK_SHIFT_MASK
#define CTRL GDK_CONTROL_MASK
#define ALT GDK_MOD1_MASK

#define SHIFT_CTRL (GdkModifierType) (SHIFT | CTRL)
#define NONE       0, (GdkModifierType) 0

int menu_tab_playlist_id = -1; /* should really be stored in the menu somehow */

static void open_files (void) {audgui_run_filebrowser (TRUE); }
static void open_url (void) {audgui_show_add_url_window (TRUE); }
static void add_files (void) {audgui_run_filebrowser (FALSE); }
static void add_url (void) {audgui_show_add_url_window (FALSE); }

static void configure_effects (void) {audgui_show_prefs_for_plugin_type (PLUGIN_TYPE_EFFECT); }
static void configure_visualizations (void) {audgui_show_prefs_for_plugin_type (PLUGIN_TYPE_VIS); }

static void pl_sort_track (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_TRACK); }
static void pl_sort_title (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_TITLE); }
static void pl_sort_artist (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_ARTIST); }
static void pl_sort_album (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_ALBUM); }
static void pl_sort_date (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_DATE); }
static void pl_sort_length (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_LENGTH); }
static void pl_sort_path (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_PATH); }
static void pl_sort_custom (void) {aud_playlist_sort_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_FORMATTED_TITLE); }
static void pl_reverse (void) {aud_playlist_reverse (aud_playlist_get_active ()); }
static void pl_random (void) {aud_playlist_randomize (aud_playlist_get_active ()); }

static void pl_sort_selected_track (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_TRACK); }
static void pl_sort_selected_title (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_TITLE); }
static void pl_sort_selected_artist (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_ARTIST); }
static void pl_sort_selected_album (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_ALBUM); }
static void pl_sort_selected_date (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_DATE); }
static void pl_sort_selected_length (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_LENGTH); }
static void pl_sort_selected_path (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_PATH); }
static void pl_sort_selected_custom (void) {aud_playlist_sort_selected_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_FORMATTED_TITLE); }
static void pl_reverse_selected (void) {aud_playlist_reverse_selected (aud_playlist_get_active ()); }
static void pl_random_selected (void) {aud_playlist_randomize_selected (aud_playlist_get_active ()); }

static void pl_new (void)
{
    aud_playlist_insert (-1);
    aud_playlist_set_active (aud_playlist_count () - 1);
}

static void pl_play (void) {aud_drct_play_playlist (aud_playlist_get_active ()); }
static void pl_refresh (void) {aud_playlist_rescan (aud_playlist_get_active ()); }
static void pl_remove_failed (void) {aud_playlist_remove_failed (aud_playlist_get_active ()); }
static void pl_remove_dupes_by_title (void) {aud_playlist_remove_duplicates_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_TITLE); }
static void pl_remove_dupes_by_filename (void) {aud_playlist_remove_duplicates_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_FILENAME); }
static void pl_remove_dupes_by_path (void) {aud_playlist_remove_duplicates_by_scheme (aud_playlist_get_active (), PLAYLIST_SORT_PATH); }
static void pl_rename (void) {start_rename_playlist (aud_playlist_get_active ()); }
static void pl_close (void) {audgui_confirm_playlist_delete (aud_playlist_get_active ()); }
static void pl_refresh_sel (void) {aud_playlist_rescan_selected (aud_playlist_get_active ()); }
static void pl_select_all (void) {aud_playlist_select_all (aud_playlist_get_active (), TRUE); }

static void pl_tab_play (void)
{
    int playlist = aud_playlist_by_unique_id (menu_tab_playlist_id);
    if (playlist >= 0)
        aud_drct_play_playlist (playlist);
}

static void pl_tab_rename (void)
{
    int playlist = aud_playlist_by_unique_id (menu_tab_playlist_id);
    if (playlist >= 0)
        start_rename_playlist (playlist);
}

static void pl_tab_close (void)
{
    int playlist = aud_playlist_by_unique_id (menu_tab_playlist_id);
    if (playlist >= 0)
        audgui_confirm_playlist_delete (playlist);
}

static GtkWidget * get_services_main (void) {return audgui_get_plugin_menu (AUD_MENU_MAIN); }
static GtkWidget * get_services_pl (void) {return audgui_get_plugin_menu (AUD_MENU_PLAYLIST); }

static void volume_up (void) {aud_drct_set_volume_main (aud_drct_get_volume_main () + 5); }
static void volume_down (void) {aud_drct_set_volume_main (aud_drct_get_volume_main () - 5); }

static const AudguiMenuItem file_items[] = {
    MenuCommand (N_("_Open Files ..."), "document-open", 'o', CTRL, open_files),
    MenuCommand (N_("Open _URL ..."), "folder-remote", 'l', CTRL, open_url),
    MenuCommand (N_("_Add Files ..."), "list-add", 'o', SHIFT_CTRL, add_files),
    MenuCommand (N_("Add U_RL ..."), "folder-remote", 'l', SHIFT_CTRL, add_url),
    MenuSep (),
    MenuCommand (N_("Search _Library"), "edit-find", 'y', CTRL, activate_search_tool),
    MenuSep (),
    MenuCommand (N_("A_bout ..."), "help-about", NONE, audgui_show_about_window),
    MenuCommand (N_("_Settings ..."), "preferences-system", NONE, audgui_show_prefs_window),
    MenuCommand (N_("_Quit"), "application-exit", 'q', CTRL, aud_quit)
};

static const AudguiMenuItem playback_items[] = {
    MenuCommand (N_("_Play"), "media-playback-start", GDK_KEY_Return, CTRL, aud_drct_play),
    MenuCommand (N_("Paus_e"), "media-playback-pause", ',', CTRL, aud_drct_pause),
    MenuCommand (N_("_Stop"), "media-playback-stop", '.', CTRL, aud_drct_stop),
    MenuCommand (N_("Pre_vious"), "media-skip-backward", GDK_KEY_Up, ALT, aud_drct_pl_prev),
    MenuCommand (N_("_Next"), "media-skip-forward", GDK_KEY_Down, ALT, aud_drct_pl_next),
    MenuSep (),
    MenuToggle (N_("_Repeat"), nullptr, 'r', CTRL, nullptr, "repeat", nullptr, "set repeat"),
    MenuToggle (N_("S_huffle"), nullptr, 's', CTRL, nullptr, "shuffle", nullptr, "set shuffle"),
    MenuToggle (N_("N_o Playlist Advance"), nullptr, 'n', CTRL, nullptr, "no_playlist_advance", nullptr, "set no_playlist_advance"),
    MenuToggle (N_("Stop A_fter This Song"), nullptr, 'm', CTRL, nullptr, "stop_after_current_song", nullptr, "set stop_after_current_song"),
    MenuSep (),
    MenuCommand (N_("Song _Info ..."), "dialog-information", 'i', CTRL, audgui_infowin_show_current),
    MenuCommand (N_("Jump to _Time ..."), "go-jump", 'k', CTRL, audgui_jump_to_time),
    MenuCommand (N_("_Jump to Song ..."), "go-jump", 'j', CTRL, audgui_jump_to_track),
    MenuSep (),
    MenuCommand (N_("Set Repeat Point _A"), nullptr, '1', CTRL, set_ab_repeat_a),
    MenuCommand (N_("Set Repeat Point _B"), nullptr, '2', CTRL, set_ab_repeat_b),
    MenuCommand (N_("_Clear Repeat Points"), nullptr, '3', CTRL, clear_ab_repeat)
};

static const AudguiMenuItem dupe_items[] = {
    MenuCommand (N_("By _Title"), nullptr, NONE, pl_remove_dupes_by_title),
    MenuCommand (N_("By _Filename"), nullptr, NONE, pl_remove_dupes_by_filename),
    MenuCommand (N_("By File _Path"), nullptr, NONE, pl_remove_dupes_by_path)
};

static const AudguiMenuItem sort_items[] = {
    MenuCommand (N_("By Track _Number"), nullptr, NONE, pl_sort_track),
    MenuCommand (N_("By _Title"), nullptr, NONE, pl_sort_title),
    MenuCommand (N_("By _Artist"), nullptr, NONE, pl_sort_artist),
    MenuCommand (N_("By Al_bum"), nullptr, NONE, pl_sort_album),
    MenuCommand (N_("By Release _Date"), nullptr, NONE, pl_sort_date),
    MenuCommand (N_("By _Length"), nullptr, NONE, pl_sort_length),
    MenuCommand (N_("By _File Path"), nullptr, NONE, pl_sort_path),
    MenuCommand (N_("By _Custom Title"), nullptr, NONE, pl_sort_custom),
    MenuSep (),
    MenuCommand (N_("R_everse Order"), "view-sort-descending", NONE, pl_reverse),
    MenuCommand (N_("_Random Order"), nullptr, NONE, pl_random)
};

 static const AudguiMenuItem sort_selected_items[] = {
    MenuCommand (N_("By Track _Number"), nullptr, NONE, pl_sort_selected_track),
    MenuCommand (N_("By _Title"), nullptr, NONE, pl_sort_selected_title),
    MenuCommand (N_("By _Artist"), nullptr, NONE, pl_sort_selected_artist),
    MenuCommand (N_("By Al_bum"), nullptr, NONE, pl_sort_selected_album),
    MenuCommand (N_("By Release _Date"), nullptr, NONE, pl_sort_selected_date),
    MenuCommand (N_("By _Length"), nullptr, NONE, pl_sort_selected_length),
    MenuCommand (N_("By _File Path"), nullptr, NONE, pl_sort_selected_path),
    MenuCommand (N_("By _Custom Title"), nullptr, NONE, pl_sort_selected_custom),
    MenuSep (),
    MenuCommand (N_("R_everse Order"), "view-sort-descending", NONE, pl_reverse_selected),
    MenuCommand (N_("_Random Order"), nullptr, NONE, pl_random_selected)
};

static const AudguiMenuItem playlist_items[] = {
    MenuCommand (N_("_Play This Playlist"), "media-playback-start", GDK_KEY_Return, SHIFT, pl_play),
    MenuCommand (N_("_Refresh"), "view-refresh", GDK_KEY_F5, (GdkModifierType) 0, pl_refresh),
    MenuSep (),
    MenuSub (N_("_Sort"), "view-sort-ascending", {sort_items}),
    MenuSub (N_("Sort Se_lected"), "view-sort-ascending", {sort_selected_items}),
    MenuSub (N_("Remove _Duplicates"), "edit-copy", {dupe_items}),
    MenuCommand (N_("Remove _Unavailable Files"), "dialog-warning", NONE, pl_remove_failed),
    MenuSep (),
    MenuCommand (N_("_New"), "document-new", 't', CTRL, pl_new),
    MenuCommand (N_("Ren_ame ..."), "insert-text", GDK_KEY_F2, (GdkModifierType) 0, pl_rename),
    MenuCommand (N_("Remo_ve"), "edit-delete", 'w', CTRL, pl_close),
    MenuSep (),
    MenuCommand (N_("_Import ..."), "document-open", NONE, audgui_import_playlist),
    MenuCommand (N_("_Export ..."), "document-save", NONE, audgui_export_playlist),
    MenuSep (),
    MenuCommand (N_("Playlist _Manager ..."), "audio-x-generic", 'p', CTRL, audgui_playlist_manager),
    MenuCommand (N_("_Queue Manager ..."), nullptr, 'u', CTRL, audgui_queue_manager_show)
};

static const AudguiMenuItem output_items[] = {
    MenuCommand (N_("Volume _Up"), "audio-volume-high", '+', CTRL, volume_up),
    MenuCommand (N_("Volume _Down"), "audio-volume-low", '-', CTRL, volume_down),
    MenuSep (),
    MenuCommand (N_("_Equalizer"), "multimedia-volume-control", 'e', CTRL, audgui_show_equalizer_window),
    MenuSep (),
    MenuCommand (N_("E_ffects ..."), nullptr, NONE, configure_effects)
};

static const AudguiMenuItem view_items[] = {
    MenuToggle (N_("Show _Menu Bar"), nullptr, 'm', SHIFT_CTRL, "gtkui", "menu_visible", show_hide_menu),
    MenuToggle (N_("Show I_nfo Bar"), nullptr, 'i', SHIFT_CTRL, "gtkui", "infoarea_visible", show_hide_infoarea),
    MenuToggle (N_("Show Info Bar Vis_ualization"), nullptr, NONE, "gtkui", "infoarea_show_vis", show_hide_infoarea_vis),
    MenuToggle (N_("Show _Status Bar"), nullptr, 's', SHIFT_CTRL, "gtkui", "statusbar_visible", show_hide_statusbar),
    MenuSep (),
    MenuToggle (N_("Show _Remaining Time"), nullptr, 'r', SHIFT_CTRL, "gtkui", "show_remaining_time"),
    MenuSep (),
    MenuCommand (N_("_Visualizations ..."), nullptr, NONE, configure_visualizations)
};

static const AudguiMenuItem main_items[] = {
    MenuSub (N_("_File"), nullptr, {file_items}),
    MenuSub (N_("_Playback"), nullptr, {playback_items}),
    MenuSub (N_("P_laylist"), nullptr, {playlist_items}),
    MenuSub (N_("_Services"), nullptr, get_services_main),
    MenuSub (N_("_Output"), nullptr, {output_items}),
    MenuSub (N_("_View"), nullptr, {view_items})
};

static const AudguiMenuItem rclick_items[] = {
    MenuCommand (N_("Song _Info ..."), "dialog-information", 'i', ALT, playlist_song_info),
    MenuCommand (N_("_Queue/Unqueue"), nullptr, 'q', ALT, playlist_queue_toggle),
    MenuCommand (N_("_Refresh"), "view-refresh", GDK_KEY_F6, (GdkModifierType) 0, pl_refresh_sel),
    MenuSep (),
    MenuCommand (N_("Cu_t"), "edit-cut", NONE, playlist_cut),
    MenuCommand (N_("_Copy"), "edit-copy", NONE, playlist_copy),
    MenuCommand (N_("_Paste"), "edit-paste", NONE, playlist_paste),
    MenuCommand (N_("Select _All"), "edit-select-all", NONE, pl_select_all),
    MenuSep (),
    MenuSub (N_("_Services"), nullptr, get_services_pl)
};

static const AudguiMenuItem tab_items[] = {
    MenuCommand (N_("_Play"), "media-playback-start", NONE, pl_tab_play),
    MenuCommand (N_("_Rename ..."), "insert-text", NONE, pl_tab_rename),
    MenuCommand (N_("Remo_ve"), "edit-delete", NONE, pl_tab_close)
};


GtkWidget * make_menu_bar (GtkAccelGroup * accel)
{
    GtkWidget * bar = gtk_menu_bar_new ();
    audgui_menu_init (bar, {main_items}, accel);
    return bar;
}

GtkWidget * make_menu_main (GtkAccelGroup * accel)
{
    GtkWidget * shell = gtk_menu_new ();
    audgui_menu_init (shell, {main_items}, accel);
    return shell;
}

GtkWidget * make_menu_rclick (GtkAccelGroup * accel)
{
    GtkWidget * shell = gtk_menu_new ();
    audgui_menu_init (shell, {rclick_items}, accel);
    return shell;
}

GtkWidget * make_menu_tab (GtkAccelGroup * accel)
{
    GtkWidget * shell = gtk_menu_new ();
    audgui_menu_init (shell, {tab_items}, accel);
    return shell;
}
