/*  Audacious - Cross-platform multimedia player
 *  Copyright (C) 2005-2011  Audacious development team.
 *
 *  BMP - Cross-platform multimedia player
 *  Copyright (C) 2003-2004  BMP development team.
 *
 *  Based on XMMS:
 *  Copyright (C) 1998-2003  XMMS development team.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 *  The Audacious team does not consider modular code linking to
 *  Audacious or using our public API to be a derived work.
 */

#include <inttypes.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/drct.h>
#include <libaudcore/hook.h>
#include <libaudcore/i18n.h>
#include <libaudcore/playlist.h>
#include <libaudcore/runtime.h>
#include <libaudcore/tuple.h>
#include <libaudgui/libaudgui.h>

#include "actions-mainwin.h"
#include "dnd.h"
#include "drag-handle.h"
#include "menus.h"
#include "plugin.h"
#include "skins_cfg.h"
#include "ui_main.h"
#include "ui_playlist.h"
#include "ui_skinned_button.h"
#include "ui_skinned_playlist.h"
#include "ui_skinned_playlist_slider.h"
#include "ui_skinned_textbox.h"
#include "ui_skinned_window.h"
#include "view.h"

#define PLAYLISTWIN_MIN_WIDTH           MAINWIN_WIDTH
#define PLAYLISTWIN_MIN_HEIGHT          MAINWIN_HEIGHT
#define PLAYLISTWIN_WIDTH_SNAP          25
#define PLAYLISTWIN_HEIGHT_SNAP         29
#define PLAYLISTWIN_SHADED_HEIGHT       MAINWIN_SHADED_HEIGHT

#define APPEND(b, ...) snprintf (b + strlen (b), sizeof b - strlen (b), __VA_ARGS__)

int active_playlist = -1, active_length = 0;
char * active_title = nullptr;

GtkWidget * playlistwin, * playlistwin_list, * playlistwin_sinfo;

static GtkWidget *playlistwin_shade, *playlistwin_close;
static GtkWidget *playlistwin_shaded_shade, *playlistwin_shaded_close;

static GtkWidget *playlistwin_slider;
static GtkWidget *playlistwin_time_min, *playlistwin_time_sec;
static GtkWidget *playlistwin_info;
static GtkWidget *playlistwin_srew, *playlistwin_splay;
static GtkWidget *playlistwin_spause, *playlistwin_sstop;
static GtkWidget *playlistwin_sfwd, *playlistwin_seject;
static GtkWidget *playlistwin_sscroll_up, *playlistwin_sscroll_down;
static GtkWidget * resize_handle, * sresize_handle;
static GtkWidget * button_add, * button_sub, * button_sel, * button_misc,
 * button_list;

static void playlistwin_select_search_cbt_cb(GtkWidget *called_cbt,
                                             void * other_cbt);
static gboolean playlistwin_select_search_kp_cb(GtkWidget *entry,
                                                GdkEventKey *event,
                                                void * searchdlg_win);

static int resize_base_width, resize_base_height;
static int drop_position;
static gboolean song_changed;

static void playlistwin_update_info (void)
{
    StringBuf s1 = str_format_time (aud_playlist_get_selected_length (active_playlist));
    StringBuf s2 = str_format_time (aud_playlist_get_total_length (active_playlist));
    textbox_set_text (playlistwin_info, str_concat ({s1, "/", s2}));
}

static void update_rollup_text (void)
{
    int playlist = aud_playlist_get_active ();
    int entry = aud_playlist_get_position (playlist);
    char scratch[512];

    scratch[0] = 0;

    if (entry > -1)
    {
        int length = aud_playlist_entry_get_length (playlist, entry, TRUE);

        if (aud_get_bool (nullptr, "show_numbers_in_pl"))
            APPEND (scratch, "%d. ", 1 + entry);

        String title = aud_playlist_entry_get_title (playlist, entry, TRUE);
        APPEND (scratch, "%s", (const char *) title);

        if (length > 0)
        {
            StringBuf buf = str_format_time (length);
            APPEND (scratch, " (%s)", (const char *) buf);
        }
    }

    textbox_set_text (playlistwin_sinfo, scratch);
}

static void real_update (void)
{
    ui_skinned_playlist_update (playlistwin_list);
    playlistwin_update_info ();
    update_rollup_text ();
}

void playlistwin_update (void)
{
    if (! aud_playlist_update_pending ())
        real_update ();
}

static void
playlistwin_shade_toggle(void)
{
    view_set_playlist_shaded (! aud_get_bool ("skins", "playlist_shaded"));
}

static void playlistwin_scroll (gboolean up)
{
    int rows, first;

    ui_skinned_playlist_row_info (playlistwin_list, & rows, & first);
    ui_skinned_playlist_scroll_to (playlistwin_list, first + (up ? -1 : 1) *
     rows / 3);
}

static void playlistwin_scroll_up_pushed (void)
{
    playlistwin_scroll (TRUE);
}

static void playlistwin_scroll_down_pushed (void)
{
    playlistwin_scroll (FALSE);
}

static void
playlistwin_select_all(void)
{
    aud_playlist_select_all (active_playlist, 1);
}

static void
playlistwin_select_none(void)
{
    aud_playlist_select_all (active_playlist, 0);
}

static void copy_selected_to_new (int playlist)
{
    int entries = aud_playlist_entry_count (playlist);
    int new_list = aud_playlist_count ();
    Index<PlaylistAddItem> items;
    int entry;

    aud_playlist_insert (new_list);

    for (entry = 0; entry < entries; entry ++)
    {
        if (aud_playlist_entry_get_selected (playlist, entry))
        {
            items.append (
                aud_playlist_entry_get_filename (playlist, entry),
                aud_playlist_entry_get_tuple (playlist, entry, TRUE)
            );
        }
    }

    aud_playlist_entry_insert_batch (new_list, 0, std::move (items), FALSE);
    aud_playlist_set_active (new_list);
}

static void
playlistwin_select_search(void)
{
    GtkWidget *searchdlg_win, *searchdlg_grid;
    GtkWidget *searchdlg_hbox, *searchdlg_logo, *searchdlg_helptext;
    GtkWidget *searchdlg_entry_title, *searchdlg_label_title;
    GtkWidget *searchdlg_entry_album, *searchdlg_label_album;
    GtkWidget *searchdlg_entry_file_name, *searchdlg_label_file_name;
    GtkWidget *searchdlg_entry_performer, *searchdlg_label_performer;
    GtkWidget *searchdlg_checkbt_clearprevsel;
    GtkWidget *searchdlg_checkbt_newplaylist;
    GtkWidget *searchdlg_checkbt_autoenqueue;
    int result;

    /* create dialog */
    searchdlg_win = gtk_dialog_new_with_buttons(
      _("Search entries in active playlist") , GTK_WINDOW(mainwin) ,
      (GtkDialogFlags) (GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      _("Cancel") , GTK_RESPONSE_REJECT , _("Search") , GTK_RESPONSE_ACCEPT , nullptr );

    /* help text and logo */
    searchdlg_hbox = gtk_hbox_new (FALSE, 6);
    searchdlg_logo = gtk_image_new_from_icon_name( "edit-find" , GTK_ICON_SIZE_DIALOG );
    searchdlg_helptext = gtk_label_new( _("Select entries in playlist by filling one or more "
      "fields. Fields use regular expressions syntax, case-insensitive. If you don't know how "
      "regular expressions work, simply insert a literal portion of what you're searching for.") );
    gtk_label_set_line_wrap( GTK_LABEL(searchdlg_helptext) , TRUE );
    gtk_box_pack_start( GTK_BOX(searchdlg_hbox) , searchdlg_logo , FALSE , FALSE , 0 );
    gtk_box_pack_start( GTK_BOX(searchdlg_hbox) , searchdlg_helptext , FALSE , FALSE , 0 );

    /* title */
    searchdlg_label_title = gtk_label_new( _("Title:") );
    gtk_misc_set_alignment ((GtkMisc *) searchdlg_label_title, 1, 0.5);
    searchdlg_entry_title = gtk_entry_new();
    g_signal_connect( searchdlg_entry_title , "key-press-event" ,
      G_CALLBACK(playlistwin_select_search_kp_cb) , searchdlg_win );

    /* album */
    searchdlg_label_album= gtk_label_new( _("Album:") );
    gtk_misc_set_alignment ((GtkMisc *) searchdlg_label_album, 1, 0.5);
    searchdlg_entry_album= gtk_entry_new();
    g_signal_connect( searchdlg_entry_album , "key-press-event" ,
      G_CALLBACK(playlistwin_select_search_kp_cb) , searchdlg_win );

    /* artist */
    searchdlg_label_performer = gtk_label_new( _("Artist:") );
    gtk_misc_set_alignment ((GtkMisc *) searchdlg_label_performer, 1, 0.5);
    searchdlg_entry_performer = gtk_entry_new();
    g_signal_connect( searchdlg_entry_performer , "key-press-event" ,
      G_CALLBACK(playlistwin_select_search_kp_cb) , searchdlg_win );

    /* file name */
    searchdlg_label_file_name = gtk_label_new( _("Filename:") );
    gtk_misc_set_alignment ((GtkMisc *) searchdlg_label_file_name, 1, 0.5);
    searchdlg_entry_file_name = gtk_entry_new();
    g_signal_connect( searchdlg_entry_file_name , "key-press-event" ,
      G_CALLBACK(playlistwin_select_search_kp_cb) , searchdlg_win );

    /* some options that control behaviour */
    searchdlg_checkbt_clearprevsel = gtk_check_button_new_with_label(
      _("Clear previous selection before searching") );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(searchdlg_checkbt_clearprevsel) , TRUE );
    searchdlg_checkbt_autoenqueue = gtk_check_button_new_with_label(
      _("Automatically toggle queue for matching entries") );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(searchdlg_checkbt_autoenqueue) , FALSE );
    searchdlg_checkbt_newplaylist = gtk_check_button_new_with_label(
      _("Create a new playlist with matching entries") );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(searchdlg_checkbt_newplaylist) , FALSE );
    g_signal_connect( searchdlg_checkbt_autoenqueue , "clicked" ,
      G_CALLBACK(playlistwin_select_search_cbt_cb) , searchdlg_checkbt_newplaylist );
    g_signal_connect( searchdlg_checkbt_newplaylist , "clicked" ,
      G_CALLBACK(playlistwin_select_search_cbt_cb) , searchdlg_checkbt_autoenqueue );

    /* place fields in searchdlg_grid */
    searchdlg_grid = gtk_table_new (0, 0, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (searchdlg_grid), 6);
    gtk_table_set_col_spacings (GTK_TABLE (searchdlg_grid), 6);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_hbox, 0, 2, 0, 1);
    gtk_table_attach (GTK_TABLE (searchdlg_grid), searchdlg_label_title, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_entry_title, 1, 2, 1, 2);
    gtk_table_attach (GTK_TABLE (searchdlg_grid), searchdlg_label_album, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_entry_album, 1, 2, 2, 3);
    gtk_table_attach (GTK_TABLE (searchdlg_grid), searchdlg_label_performer, 0, 1, 3, 4, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_entry_performer, 1, 2, 3, 4);
    gtk_table_attach (GTK_TABLE (searchdlg_grid), searchdlg_label_file_name, 0, 1, 4, 5, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_entry_file_name, 1, 2, 4, 5);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_checkbt_clearprevsel, 0, 2, 5, 6);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_checkbt_autoenqueue, 0, 2, 6, 7);
    gtk_table_attach_defaults (GTK_TABLE (searchdlg_grid), searchdlg_checkbt_newplaylist, 0, 2, 7, 8);

    gtk_container_set_border_width( GTK_CONTAINER(searchdlg_grid) , 5 );
    gtk_container_add ( GTK_CONTAINER(gtk_dialog_get_content_area
     (GTK_DIALOG(searchdlg_win))) , searchdlg_grid );
    gtk_widget_show_all( searchdlg_win );
    result = gtk_dialog_run( GTK_DIALOG(searchdlg_win) );

    switch(result)
    {
      case GTK_RESPONSE_ACCEPT:
      {
         /* create a TitleInput tuple with user search data */
         Tuple tuple;
         char *searchdata = nullptr;

         searchdata = (char*)gtk_entry_get_text( GTK_ENTRY(searchdlg_entry_title) );
         AUDDBG("title=\"%s\"\n", searchdata);
         tuple.set_str (FIELD_TITLE, searchdata);

         searchdata = (char*)gtk_entry_get_text( GTK_ENTRY(searchdlg_entry_album) );
         AUDDBG("album=\"%s\"\n", searchdata);
         tuple.set_str (FIELD_ALBUM, searchdata);

         searchdata = (char*)gtk_entry_get_text( GTK_ENTRY(searchdlg_entry_performer) );
         AUDDBG("performer=\"%s\"\n", searchdata);
         tuple.set_str (FIELD_ARTIST, searchdata);

         searchdata = (char*)gtk_entry_get_text( GTK_ENTRY(searchdlg_entry_file_name) );
         AUDDBG("filename=\"%s\"\n", searchdata);
         tuple.set_str (FIELD_FILE_NAME, searchdata);

         /* check if previous selection should be cleared before searching */
         if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(searchdlg_checkbt_clearprevsel)) == TRUE )
             playlistwin_select_none();

         aud_playlist_select_by_patterns (active_playlist, tuple);

         /* check if a new playlist should be created after searching */
         if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(searchdlg_checkbt_newplaylist)) == TRUE )
             copy_selected_to_new (active_playlist);
         else
         {
             /* set focus on the first entry found */
             int entries = aud_playlist_entry_count (active_playlist);
             int count;

             for (count = 0; count < entries; count ++)
             {
                 if (aud_playlist_entry_get_selected (active_playlist, count))
                 {
                     ui_skinned_playlist_set_focused (playlistwin_list, count);
                     break;
                 }
             }

             /* check if matched entries should be queued */
             if (gtk_toggle_button_get_active ((GtkToggleButton *)
              searchdlg_checkbt_autoenqueue))
                 aud_playlist_queue_insert_selected (active_playlist, -1);
         }

         playlistwin_update ();
         break;
      }
      default:
         break;
    }
    /* done here :) */
    gtk_widget_destroy( searchdlg_win );
}

static void playlistwin_inverse_selection (void)
{
    int entries = aud_playlist_entry_count (active_playlist);
    int entry;

    for (entry = 0; entry < entries; entry ++)
        aud_playlist_entry_set_selected (active_playlist, entry,
         ! aud_playlist_entry_get_selected (active_playlist, entry));
}

/* note: height is ignored if the window is shaded */
static void playlistwin_resize (int w, int h)
{
    int tx, ty;

    g_return_if_fail(w > 0 && h > 0);

    tx = (w - PLAYLISTWIN_MIN_WIDTH) / PLAYLISTWIN_WIDTH_SNAP;
    tx = (tx * PLAYLISTWIN_WIDTH_SNAP) + PLAYLISTWIN_MIN_WIDTH;
    if (tx < PLAYLISTWIN_MIN_WIDTH)
        tx = PLAYLISTWIN_MIN_WIDTH;

    if (! aud_get_bool ("skins", "playlist_shaded"))
    {
        ty = (h - PLAYLISTWIN_MIN_HEIGHT) / PLAYLISTWIN_HEIGHT_SNAP;
        ty = (ty * PLAYLISTWIN_HEIGHT_SNAP) + PLAYLISTWIN_MIN_HEIGHT;
        if (ty < PLAYLISTWIN_MIN_HEIGHT)
            ty = PLAYLISTWIN_MIN_HEIGHT;
    }
    else
        ty = config.playlist_height;

    if (tx == config.playlist_width && ty == config.playlist_height)
        return;

    config.playlist_width = w = tx;
    config.playlist_height = h = ty;

    ui_skinned_playlist_resize (playlistwin_list, w - 31, h - 58);
    window_move_widget (playlistwin, FALSE, playlistwin_slider, w - 15, 20);
    ui_skinned_playlist_slider_resize (playlistwin_slider, h - 58);

    window_move_widget (playlistwin, TRUE, playlistwin_shaded_shade, w - 21, 3);
    window_move_widget (playlistwin, TRUE, playlistwin_shaded_close, w - 11, 3);
    window_move_widget (playlistwin, FALSE, playlistwin_shade, w - 21, 3);
    window_move_widget (playlistwin, FALSE, playlistwin_close, w - 11, 3);

    window_move_widget (playlistwin, FALSE, playlistwin_time_min, w - 82, h - 15);
    window_move_widget (playlistwin, FALSE, playlistwin_time_sec, w - 64, h - 15);
    window_move_widget (playlistwin, FALSE, playlistwin_info, w - 143, h - 28);

    window_move_widget (playlistwin, FALSE, playlistwin_srew, w - 144, h - 16);
    window_move_widget (playlistwin, FALSE, playlistwin_splay, w - 138, h - 16);
    window_move_widget (playlistwin, FALSE, playlistwin_spause, w - 128, h - 16);
    window_move_widget (playlistwin, FALSE, playlistwin_sstop, w - 118, h - 16);
    window_move_widget (playlistwin, FALSE, playlistwin_sfwd, w - 109, h - 16);
    window_move_widget (playlistwin, FALSE, playlistwin_seject, w - 100, h - 16);
    window_move_widget (playlistwin, FALSE, playlistwin_sscroll_up, w - 14, h - 35);
    window_move_widget (playlistwin, FALSE, playlistwin_sscroll_down, w - 14, h - 30);

    window_move_widget (playlistwin, FALSE, resize_handle, w - 20, h - 20);
    window_move_widget (playlistwin, TRUE, sresize_handle, w - 31, 0);

    textbox_set_width (playlistwin_sinfo, w - 35);

    window_move_widget (playlistwin, FALSE, button_add, 12, h - 29);
    window_move_widget (playlistwin, FALSE, button_sub, 40, h - 29);
    window_move_widget (playlistwin, FALSE, button_sel, 68, h - 29);
    window_move_widget (playlistwin, FALSE, button_misc, 100, h - 29);
    window_move_widget (playlistwin, FALSE, button_list, w - 46, h - 29);
}

static void
playlistwin_fileinfo(void)
{
    audgui_infowin_show (active_playlist, aud_playlist_get_focus (active_playlist));
}

static void
playlistwin_scrolled(GtkWidget * widget,
                     GdkEventScroll * event,
                     void * callback_data)
{
    switch (event->direction)
    {
    case GDK_SCROLL_DOWN:
        playlistwin_scroll (FALSE);
        break;
    case GDK_SCROLL_UP:
        playlistwin_scroll (TRUE);
        break;
    default:
        break;
    }
}

static gboolean
playlistwin_press(GtkWidget * widget,
                  GdkEventButton * event,
                  void * callback_data)
{
    if (event->button == 1 && event->type == GDK_2BUTTON_PRESS &&
     event->window == gtk_widget_get_window (widget) && event->y < 14)
        playlistwin_shade_toggle();
    else if (event->button == 3)
        menu_popup (UI_MENU_PLAYLIST, event->x_root, event->y_root, FALSE, FALSE, 3, event->time);

    return TRUE;
}

void
playlistwin_hide_timer(void)
{
    textbox_set_text (playlistwin_time_min, "   ");
    textbox_set_text (playlistwin_time_sec, "  ");
}

void playlistwin_set_time (const char * minutes, const char * seconds)
{
    textbox_set_text (playlistwin_time_min, minutes);
    textbox_set_text (playlistwin_time_sec, seconds);
}

static void drag_motion (GtkWidget * widget, GdkDragContext * context, int x,
 int y, unsigned time, void * unused)
{
    if (! aud_get_bool ("skins", "playlist_shaded"))
        ui_skinned_playlist_hover (playlistwin_list, x - 12, y - 20);
}

static void drag_leave (GtkWidget * widget, GdkDragContext * context, unsigned time,
 void * unused)
{
    if (! aud_get_bool ("skins", "playlist_shaded"))
        ui_skinned_playlist_hover_end (playlistwin_list);
}

static void drag_drop (GtkWidget * widget, GdkDragContext * context, int x,
 int y, unsigned time, void * unused)
{
    if (aud_get_bool ("skins", "playlist_shaded"))
        drop_position = -1;
    else
    {
        ui_skinned_playlist_hover (playlistwin_list, x - 12, y - 20);
        drop_position = ui_skinned_playlist_hover_end (playlistwin_list);
    }
}

static void drag_data_received (GtkWidget * widget, GdkDragContext * context,
 int x, int y, GtkSelectionData * data, unsigned info, unsigned time, void * unused)
{
    audgui_urilist_insert (active_playlist, drop_position, (const char *)
     gtk_selection_data_get_data (data));
    drop_position = -1;
}

static void playlistwin_hide (void)
{
    view_set_show_playlist (FALSE);
}

static void resize_press (void)
{
    resize_base_width = config.playlist_width;
    resize_base_height = config.playlist_height;
}

static void resize_drag (int x_offset, int y_offset)
{
    gboolean shaded = aud_get_bool ("skins", "playlist_shaded");

    /* compromise between rounding and truncating; this has no real
     * justification at all other than it "looks about right". */
    playlistwin_resize (resize_base_width + x_offset + PLAYLISTWIN_WIDTH_SNAP /
     3, resize_base_height + y_offset + PLAYLISTWIN_HEIGHT_SNAP / 3);
    window_set_size (playlistwin, config.playlist_width, shaded ?
     PLAYLISTWIN_SHADED_HEIGHT : config.playlist_height);
}

static void button_add_cb (GtkWidget * button, GdkEventButton * event)
{
    int xpos, ypos;
    gtk_window_get_position ((GtkWindow *) playlistwin, & xpos, & ypos);
    menu_popup (UI_MENU_PLAYLIST_ADD, xpos + 12 * config.scale,
     ypos + (config.playlist_height - 8) * config.scale, FALSE, TRUE,
     event->button, event->time);
}

static void button_sub_cb (GtkWidget * button, GdkEventButton * event)
{
    int xpos, ypos;
    gtk_window_get_position ((GtkWindow *) playlistwin, & xpos, & ypos);
    menu_popup (UI_MENU_PLAYLIST_REMOVE, xpos + 40 * config.scale,
     ypos + (config.playlist_height - 8) * config.scale, FALSE, TRUE,
     event->button, event->time);
}

static void button_sel_cb (GtkWidget * button, GdkEventButton * event)
{
    int xpos, ypos;
    gtk_window_get_position ((GtkWindow *) playlistwin, & xpos, & ypos);
    menu_popup (UI_MENU_PLAYLIST_SELECT, xpos + 68 * config.scale,
     ypos + (config.playlist_height - 8) * config.scale, FALSE, TRUE,
     event->button, event->time);
}

static void button_misc_cb (GtkWidget * button, GdkEventButton * event)
{
    int xpos, ypos;
    gtk_window_get_position ((GtkWindow *) playlistwin, & xpos, & ypos);
    menu_popup (UI_MENU_PLAYLIST_SORT, xpos + 100 * config.scale,
     ypos + (config.playlist_height - 8) * config.scale, FALSE, TRUE,
     event->button, event->time);
}

static void button_list_cb (GtkWidget * button, GdkEventButton * event)
{
    int xpos, ypos;
    gtk_window_get_position ((GtkWindow *) playlistwin, & xpos, & ypos);
    menu_popup (UI_MENU_PLAYLIST,
     xpos + (config.playlist_width - 12) * config.scale,
     ypos + (config.playlist_height - 8) * config.scale, TRUE, TRUE,
     event->button, event->time);
}

static void
playlistwin_create_widgets(void)
{
    int w = config.playlist_width, h = config.playlist_height;

    playlistwin_sinfo = textbox_new (w - 35, "", nullptr, config.autoscroll);
    window_put_widget (playlistwin, TRUE, playlistwin_sinfo, 4, 4);

    playlistwin_shaded_shade = button_new (9, 9, 128, 45, 150, 42, SKIN_PLEDIT, SKIN_PLEDIT);
    window_put_widget (playlistwin, TRUE, playlistwin_shaded_shade, w - 21, 3);
    button_on_release (playlistwin_shaded_shade, (ButtonCB) playlistwin_shade_toggle);

    playlistwin_shaded_close = button_new (9, 9, 138, 45, 52, 42, SKIN_PLEDIT, SKIN_PLEDIT);
    window_put_widget (playlistwin, TRUE, playlistwin_shaded_close, w - 11, 3);
    button_on_release (playlistwin_shaded_close, (ButtonCB) playlistwin_hide);

    playlistwin_shade = button_new (9, 9, 157, 3, 62, 42, SKIN_PLEDIT, SKIN_PLEDIT);
    window_put_widget (playlistwin, FALSE, playlistwin_shade, w - 21, 3);
    button_on_release (playlistwin_shade, (ButtonCB) playlistwin_shade_toggle);

    playlistwin_close = button_new (9, 9, 167, 3, 52, 42, SKIN_PLEDIT, SKIN_PLEDIT);
    window_put_widget (playlistwin, FALSE, playlistwin_close, w - 11, 3);
    button_on_release (playlistwin_close, (ButtonCB) playlistwin_hide);

    String font = aud_get_str ("skins", "playlist_font");
    playlistwin_list = ui_skinned_playlist_new (w - 31, h - 58, font);
    window_put_widget (playlistwin, FALSE, playlistwin_list, 12, 20);

    /* playlist list box slider */
    playlistwin_slider = ui_skinned_playlist_slider_new (playlistwin_list, h - 58);
    window_put_widget (playlistwin, FALSE, playlistwin_slider, w - 15, 20);
    ui_skinned_playlist_set_slider (playlistwin_list, playlistwin_slider);

    playlistwin_time_min = textbox_new (15, "", nullptr, FALSE);
    window_put_widget (playlistwin, FALSE, playlistwin_time_min, w - 82, h - 15);

    playlistwin_time_sec = textbox_new (10, "", nullptr, FALSE);
    window_put_widget (playlistwin, FALSE, playlistwin_time_sec, w - 64, h - 15);

    g_signal_connect(playlistwin_time_min, "button-press-event", G_CALLBACK(change_timer_mode_cb), nullptr);
    g_signal_connect(playlistwin_time_sec, "button-press-event", G_CALLBACK(change_timer_mode_cb), nullptr);

    playlistwin_info = textbox_new (90, "", nullptr, FALSE);
    window_put_widget (playlistwin, FALSE, playlistwin_info, w - 143, h - 28);

    /* mini play control buttons at right bottom corner */

    playlistwin_srew = button_new_small (8, 7);
    window_put_widget (playlistwin, FALSE, playlistwin_srew, w - 144, h - 16);
    button_on_release (playlistwin_srew, (ButtonCB) aud_drct_pl_prev);

    playlistwin_splay = button_new_small (10, 7);
    window_put_widget (playlistwin, FALSE, playlistwin_splay, w - 138, h - 16);
    button_on_release (playlistwin_splay, (ButtonCB) aud_drct_play);

    playlistwin_spause = button_new_small (10, 7);
    window_put_widget (playlistwin, FALSE, playlistwin_spause, w - 128, h - 16);
    button_on_release (playlistwin_spause, (ButtonCB) aud_drct_pause);

    playlistwin_sstop = button_new_small (9, 7);
    window_put_widget (playlistwin, FALSE, playlistwin_sstop, w - 118, h - 16);
    button_on_release (playlistwin_sstop, (ButtonCB) aud_drct_stop);

    playlistwin_sfwd = button_new_small (8, 7);
    window_put_widget (playlistwin, FALSE, playlistwin_sfwd, w - 109, h - 16);
    button_on_release (playlistwin_sfwd, (ButtonCB) aud_drct_pl_next);

    playlistwin_seject = button_new_small (9, 7);
    window_put_widget (playlistwin, FALSE, playlistwin_seject, w - 100, h - 16);
    button_on_release (playlistwin_seject, (ButtonCB) action_play_file);

    playlistwin_sscroll_up = button_new_small (8, 5);
    window_put_widget (playlistwin, FALSE, playlistwin_sscroll_up, w - 14, h - 35);
    button_on_release (playlistwin_sscroll_up, (ButtonCB) playlistwin_scroll_up_pushed);

    playlistwin_sscroll_down = button_new_small (8, 5);
    window_put_widget (playlistwin, FALSE, playlistwin_sscroll_down, w - 14, h - 30);
    button_on_release (playlistwin_sscroll_down, (ButtonCB) playlistwin_scroll_down_pushed);

    /* resize handles */

    resize_handle = drag_handle_new (20, 20, resize_press, resize_drag);
    window_put_widget (playlistwin, FALSE, resize_handle, w - 20, h - 20);

    sresize_handle = drag_handle_new (9, PLAYLISTWIN_SHADED_HEIGHT, resize_press, resize_drag);
    window_put_widget (playlistwin, TRUE, sresize_handle, w - 31, 0);

    /* lower button row */

    button_add = button_new_small (25, 18);
    window_put_widget (playlistwin, FALSE, button_add, 12, h - 29);
    button_on_press (button_add, button_add_cb);

    button_sub = button_new_small (25, 18);
    window_put_widget (playlistwin, FALSE, button_sub, 40, h - 29);
    button_on_press (button_sub, button_sub_cb);

    button_sel = button_new_small (25, 18);
    window_put_widget (playlistwin, FALSE, button_sel, 68, h - 29);
    button_on_press (button_sel, button_sel_cb);

    button_misc = button_new_small (25, 18);
    window_put_widget (playlistwin, FALSE, button_misc, 100, h - 29);
    button_on_press (button_misc, button_misc_cb);

    button_list = button_new_small (23, 18);
    window_put_widget (playlistwin, FALSE, button_list, w - 46, h - 29);
    button_on_press (button_list, button_list_cb);
}

static void pl_win_draw (GtkWidget * window, cairo_t * cr)
{
    if (aud_get_bool ("skins", "playlist_shaded"))
        skin_draw_playlistwin_shaded (cr, config.playlist_width, TRUE);
    else
        skin_draw_playlistwin_frame (cr, config.playlist_width,
         config.playlist_height, TRUE);
}

static void
playlistwin_create_window(void)
{
    gboolean shaded = aud_get_bool ("skins", "playlist_shaded");

    playlistwin = window_new (& config.playlist_x, & config.playlist_y,
     config.playlist_width, shaded ? PLAYLISTWIN_SHADED_HEIGHT :
     config.playlist_height, FALSE, shaded, pl_win_draw);

    gtk_window_set_title(GTK_WINDOW(playlistwin), _("Audacious Playlist Editor"));

    gtk_window_set_transient_for(GTK_WINDOW(playlistwin),
                                 GTK_WINDOW(mainwin));
    gtk_window_set_skip_pager_hint(GTK_WINDOW(playlistwin), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(playlistwin), TRUE);

    gtk_widget_add_events(playlistwin, GDK_POINTER_MOTION_MASK |
                          GDK_FOCUS_CHANGE_MASK | GDK_BUTTON_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                          GDK_SCROLL_MASK | GDK_VISIBILITY_NOTIFY_MASK);

    g_signal_connect (playlistwin, "delete-event", (GCallback) handle_window_close, nullptr);
    g_signal_connect (playlistwin, "button-press-event", (GCallback) playlistwin_press, nullptr);
    g_signal_connect (playlistwin, "scroll-event", (GCallback) playlistwin_scrolled, nullptr);
    g_signal_connect (playlistwin, "key-press-event", (GCallback) mainwin_keypress, nullptr);

    drag_dest_set (playlistwin);
    drop_position = -1;

    g_signal_connect (playlistwin, "drag-motion", (GCallback) drag_motion, nullptr);
    g_signal_connect (playlistwin, "drag-leave", (GCallback) drag_leave, nullptr);
    g_signal_connect (playlistwin, "drag-drop", (GCallback) drag_drop, nullptr);
    g_signal_connect (playlistwin, "drag-data-received", (GCallback) drag_data_received, nullptr);
}

static void get_title (void)
{
    int playlists = aud_playlist_count ();

    g_free (active_title);

    if (playlists > 1)
    {
        String title = aud_playlist_get_title (active_playlist);
        active_title = g_strdup_printf (_("%s (%d of %d)"),
         (const char *) title, 1 + active_playlist, playlists);
    }
    else
        active_title = nullptr;
}

static void update_cb (void * unused, void * another)
{
    int old = active_playlist;

    active_playlist = aud_playlist_get_active ();
    active_length = aud_playlist_entry_count (active_playlist);
    get_title ();

    if (active_playlist != old)
    {
        ui_skinned_playlist_scroll_to (playlistwin_list, 0);
        song_changed = TRUE;
    }

    if (song_changed)
    {
        ui_skinned_playlist_set_focused (playlistwin_list,
         aud_playlist_get_position (active_playlist));
        song_changed = FALSE;
    }

    real_update ();
}

static void follow_cb (void * data, void * another)
{
    int list = GPOINTER_TO_INT (data);
    aud_playlist_select_all (list, FALSE);

    int row = aud_playlist_get_position (list);
    if (row >= 0)
        aud_playlist_entry_set_selected (list, row, TRUE);

    if (list == active_playlist)
        song_changed = TRUE;
}

void
playlistwin_create(void)
{
    active_playlist = aud_playlist_get_active ();
    active_length = aud_playlist_entry_count (active_playlist);
    active_title = nullptr;
    get_title ();

    playlistwin_create_window();

    playlistwin_create_widgets();
    window_show_all (playlistwin);

    gtk_window_add_accel_group ((GtkWindow *) playlistwin, menu_get_accel_group ());

    aud_playlist_select_all (active_playlist, FALSE);

    int row = aud_playlist_get_position (active_playlist);
    if (row >= 0)
        aud_playlist_entry_set_selected (active_playlist, row, TRUE);

    ui_skinned_playlist_set_focused (playlistwin_list, row);

    song_changed = FALSE;

    hook_associate ("playlist position", follow_cb, nullptr);
    hook_associate ("playlist activate", update_cb, nullptr);
    hook_associate ("playlist update", update_cb, nullptr);
}

void playlistwin_unhook (void)
{
    hook_dissociate ("playlist position", follow_cb);
    hook_dissociate ("playlist activate", update_cb);
    hook_dissociate ("playlist update", update_cb);
    g_free (active_title);
    active_title = nullptr;
}

void action_playlist_track_info(void)
{
    playlistwin_fileinfo();
}

void action_queue_toggle (void)
{
    int focus = aud_playlist_get_focus (active_playlist);
    if (focus == -1)
        return;

    int at = aud_playlist_queue_find_entry (active_playlist, focus);

    if (at == -1)
        aud_playlist_queue_insert_selected (active_playlist, -1);
    else
        aud_playlist_queue_delete (active_playlist, at, 1);
}

void action_playlist_sort_by_track_number (void)
{
    aud_playlist_sort_by_scheme (active_playlist, PLAYLIST_SORT_TRACK);
}

void action_playlist_sort_by_title (void)
{
    aud_playlist_sort_by_scheme (active_playlist, PLAYLIST_SORT_TITLE);
}

void action_playlist_sort_by_album (void)
{
    aud_playlist_sort_by_scheme (active_playlist, PLAYLIST_SORT_ALBUM);
}

void action_playlist_sort_by_artist (void)
{
    aud_playlist_sort_by_scheme (active_playlist, PLAYLIST_SORT_ARTIST);
}

void action_playlist_sort_by_full_path (void)
{
    aud_playlist_sort_by_scheme (active_playlist, PLAYLIST_SORT_PATH);
}

void action_playlist_sort_by_date (void)
{
    aud_playlist_sort_by_scheme (active_playlist, PLAYLIST_SORT_DATE);
}

void action_playlist_sort_by_filename (void)
{
    aud_playlist_sort_by_scheme (active_playlist, PLAYLIST_SORT_FILENAME);
}

void action_playlist_sort_selected_by_track_number (void)
{
    aud_playlist_sort_selected_by_scheme (active_playlist, PLAYLIST_SORT_TRACK);
}

void action_playlist_sort_selected_by_title (void)
{
    aud_playlist_sort_selected_by_scheme (active_playlist, PLAYLIST_SORT_TITLE);
}

void action_playlist_sort_selected_by_album (void)
{
    aud_playlist_sort_selected_by_scheme (active_playlist, PLAYLIST_SORT_ALBUM);
}

void action_playlist_sort_selected_by_artist (void)
{
    aud_playlist_sort_selected_by_scheme (active_playlist, PLAYLIST_SORT_ARTIST);
}

void action_playlist_sort_selected_by_full_path (void)
{
    aud_playlist_sort_selected_by_scheme (active_playlist, PLAYLIST_SORT_PATH);
}

void action_playlist_sort_selected_by_date (void)
{
    aud_playlist_sort_selected_by_scheme (active_playlist, PLAYLIST_SORT_DATE);
}

void action_playlist_sort_selected_by_filename (void)
{
    aud_playlist_sort_selected_by_scheme (active_playlist, PLAYLIST_SORT_FILENAME);
}

void action_playlist_randomize_list (void)
{
    aud_playlist_randomize (active_playlist);
}

void action_playlist_reverse_list (void)
{
    aud_playlist_reverse (active_playlist);
}

void action_playlist_clear_queue (void)
{
    aud_playlist_queue_delete (active_playlist, 0, aud_playlist_queue_count
     (active_playlist));
}

void action_playlist_remove_unavailable (void)
{
    aud_playlist_remove_failed (active_playlist);
}

void action_playlist_remove_dupes_by_title (void)
{
    aud_playlist_remove_duplicates_by_scheme (active_playlist,
     PLAYLIST_SORT_TITLE);
}

void action_playlist_remove_dupes_by_filename (void)
{
    aud_playlist_remove_duplicates_by_scheme (active_playlist,
     PLAYLIST_SORT_FILENAME);
}

void action_playlist_remove_dupes_by_full_path (void)
{
    aud_playlist_remove_duplicates_by_scheme (active_playlist,
     PLAYLIST_SORT_PATH);
}

void action_playlist_remove_all (void)
{
    aud_playlist_entry_delete (active_playlist, 0, aud_playlist_entry_count
     (active_playlist));
}

void action_playlist_remove_selected (void)
{
    aud_playlist_delete_selected (active_playlist);
}

void action_playlist_remove_unselected (void)
{
    playlistwin_inverse_selection ();
    aud_playlist_delete_selected (active_playlist);
    aud_playlist_select_all (active_playlist, TRUE);
}

void action_playlist_copy (void)
{
    GtkClipboard * clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    StringBuf list = audgui_urilist_create_from_selected (active_playlist);

    if (list == nullptr)
        return;

    gtk_clipboard_set_text (clip, list, -1);
}

void action_playlist_cut (void)
{
    action_playlist_copy ();
    action_playlist_remove_selected ();
}

void action_playlist_paste (void)
{
    GtkClipboard * clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    char * list = gtk_clipboard_wait_for_text (clip);

    if (list == nullptr)
        return;

    audgui_urilist_insert (active_playlist,
     aud_playlist_get_focus (active_playlist), list);
    g_free (list);
}

void
action_playlist_add_files(void)
{
    audgui_run_filebrowser(FALSE); /* FALSE = NO_PLAY_BUTTON */
}

void
action_playlist_add_url(void)
{
    audgui_show_add_url_window (FALSE);
}

void action_playlist_play (void)
{
    aud_drct_play_playlist (aud_playlist_get_active ());
}

void action_playlist_new (void)
{
    int playlist = aud_playlist_count ();

    aud_playlist_insert (playlist);
    aud_playlist_set_active (playlist);
}

void action_playlist_prev (void)
{
    if (active_playlist > 0)
        aud_playlist_set_active (active_playlist - 1);
    else
    {
        int count = aud_playlist_count ();
        if (count > 1)
            aud_playlist_set_active (count - 1);
    }
}

void action_playlist_next (void)
{
    int count = aud_playlist_count ();

    if (active_playlist + 1 < count)
        aud_playlist_set_active (active_playlist + 1);
    else if (count > 1)
        aud_playlist_set_active (0);
}

void action_playlist_rename (void)
{
    audgui_show_playlist_rename (active_playlist);
}

void action_playlist_delete (void)
{
    audgui_confirm_playlist_delete (active_playlist);
}

void
action_playlist_refresh_list(void)
{
    aud_playlist_rescan (active_playlist);
}

void
action_playlist_search_and_select(void)
{
    playlistwin_select_search();
}

void
action_playlist_invert_selection(void)
{
    playlistwin_inverse_selection();
}

void
action_playlist_select_none(void)
{
    playlistwin_select_none();
}

void
action_playlist_select_all(void)
{
    playlistwin_select_all();
}


static void
playlistwin_select_search_cbt_cb(GtkWidget *called_cbt, void * other_cbt)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(called_cbt)) == TRUE)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other_cbt), FALSE);
    return;
}

static gboolean
playlistwin_select_search_kp_cb(GtkWidget *entry, GdkEventKey *event,
                                void * searchdlg_win)
{
    switch (event->keyval)
    {
        case GDK_KEY_Return:
            gtk_dialog_response(GTK_DIALOG(searchdlg_win), GTK_RESPONSE_ACCEPT);
            return TRUE;
        default:
            return FALSE;
    }
}
