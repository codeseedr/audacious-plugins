/*
 * delete-files.c
 * Copyright 2013 John Lindgren
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/i18n.h>
#include <libaudcore/interface.h>
#include <libaudcore/playlist.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <libaudcore/runtime.h>
#include <libaudgui/libaudgui-gtk.h>

static const int menus[] = {AUD_MENU_MAIN, AUD_MENU_PLAYLIST, AUD_MENU_PLAYLIST_REMOVE};

static GtkWidget * dialog = nullptr;

static void move_to_trash (const char * filename)
{
    GFile * gfile = g_file_new_for_path (filename);
    GError * gerror = nullptr;

    if (! g_file_trash (gfile, nullptr, & gerror))
    {
        aud_ui_show_error (str_printf (_("Error moving %s to trash: %s."),
         filename, gerror->message));
        g_error_free (gerror);
    }

    g_object_unref ((GObject *) gfile);
}

static void really_delete (const char * filename)
{
    if (g_unlink (filename) < 0)
        aud_ui_show_error (str_printf (_("Error deleting %s: %s."), filename, strerror (errno)));
}

static void confirm_delete (void)
{
    Index<String> files;

    int playlist = aud_playlist_get_active ();
    int entry_count = aud_playlist_entry_count (playlist);

    for (int i = 0; i < entry_count; i ++)
    {
        if (aud_playlist_entry_get_selected (playlist, i))
            files.append (aud_playlist_entry_get_filename (playlist, i));
    }

    aud_playlist_delete_selected (playlist);

    for (const String & uri : files)
    {
        StringBuf filename = uri_to_filename (uri);

        if (filename)
        {
            if (aud_get_bool ("delete_files", "use_trash"))
                move_to_trash (filename);
            else
                really_delete (filename);
        }
        else
            aud_ui_show_error (str_printf
             (_("Error deleting %s: not a local file."), (const char *) uri));
    }
}

static void start_delete (void)
{
    if (dialog)
    {
        gtk_window_present ((GtkWindow *) dialog);
        return;
    }

    const char * message;
    GtkWidget * button1, * button2;

    if (aud_get_bool ("delete_files", "use_trash"))
    {
        message = _("Do you want to move the selected files to the trash?");
        button1 = audgui_button_new (_("Move to Trash"), "user-trash",
         (AudguiCallback) confirm_delete, nullptr);
    }
    else
    {
        message = _("Do you want to permanently delete the selected files?");
        button1 = audgui_button_new (_("Delete"), "edit-delete",
         (AudguiCallback) confirm_delete, nullptr);
    }

    button2 = audgui_button_new (_("Cancel"), "process-stop", nullptr, nullptr);
    dialog = audgui_dialog_new (GTK_MESSAGE_QUESTION, _("Delete Files"),
     message, button1, button2);

    g_signal_connect (dialog, "destroy", (GCallback) gtk_widget_destroyed, & dialog);
    gtk_widget_show_all (dialog);
}

static const char * const delete_files_defaults[] = {
 "use_trash", "TRUE",
 nullptr};

static bool delete_files_init (void)
{
#if ! GLIB_CHECK_VERSION (2, 36, 0)
    g_type_init ();
#endif

    aud_config_set_defaults ("delete_files", delete_files_defaults);

    for (int menu : menus)
        aud_plugin_menu_add (menu, start_delete, _("Delete Selected Files"), "edit-delete");

    return TRUE;
}

static void delete_files_cleanup (void)
{
    if (dialog)
        gtk_widget_destroy (dialog);

    for (int menu : menus)
        aud_plugin_menu_remove (menu, start_delete);
}

static const PreferencesWidget delete_files_widgets[] = {
    WidgetLabel (N_("<b>Delete Method</b>")),
    WidgetCheck (N_("Move to trash instead of deleting immediately"),
        WidgetBool ("delete_files", "use_trash"))
};

static const PluginPreferences delete_files_prefs = {{delete_files_widgets}};

#define AUD_PLUGIN_NAME        N_("Delete Files")
#define AUD_PLUGIN_INIT        delete_files_init
#define AUD_PLUGIN_CLEANUP     delete_files_cleanup
#define AUD_PLUGIN_PREFS       & delete_files_prefs

#define AUD_DECLARE_GENERAL
#include <libaudcore/plugin-declare.h>
