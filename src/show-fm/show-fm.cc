
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/playlist.h>
#include <libaudcore/interface.h>
#include <libaudcore/audstrings.h>
#include <libaudgui/libaudgui.h>
#include <libaudgui/libaudgui-gtk.h>

static const int menus[] = {AUD_MENU_MAIN, AUD_MENU_PLAYLIST};

static void bury_child(int signal)
{
    waitpid(-1, NULL, WNOHANG);
}

static void open_dir_fm(char *dir)
{
    //falling back to MIME type handler detected by xdg-open
    char *cmd = "xdg-open";
    char * const argv[3] = {cmd, dir, NULL};

    signal(SIGCHLD, bury_child);

    if (fork() == 0)
    {
        for (int i = 3; i < 255; i++)
            close(i);
        execvp(cmd, argv);
    }
}

static const uint DIRNAMES_COUNT_MAX = 5;

//handles first selected playlist entry only
static void show_playlist_entries(void)
{
    char *dir_to_show = NULL;

    int playlist = aud_playlist_get_active();
    int entry_count = aud_playlist_entry_count(playlist);

    for (int i = 0; i < entry_count; i ++)
    {
        if (!aud_playlist_entry_get_selected(playlist, i))
            continue;

        String uri = aud_playlist_entry_get_filename(playlist, i);
        StringBuf filename = uri_to_filename(uri);

        if (!filename)
        {
            aud_ui_show_error(_("Unable to show %s in File Manager: not a local file."));
        }
        else
        {
            if (!dir_to_show)
                dir_to_show = g_path_get_dirname(filename);
        }

        if (dir_to_show)
            break;
    }

    open_dir_fm(dir_to_show);
}

static bool sfm_init(void)
{
    for (uint i = 0; i < G_N_ELEMENTS (menus); i ++)
        aud_plugin_menu_add (menus[i], show_playlist_entries,
            "Show in File Manager", "folder");

    return TRUE;
}

static void sfm_cleanup(void)
{
    for (uint i = 0; i < G_N_ELEMENTS (menus); i ++)
        aud_plugin_menu_remove(menus[i], show_playlist_entries);
}

#define AUD_PLUGIN_NAME        N_("Show in File Manager")
#define AUD_PLUGIN_INIT        sfm_init
#define AUD_PLUGIN_CLEANUP     sfm_cleanup

#define AUD_DECLARE_GENERAL
#include <libaudcore/plugin-declare.h>

