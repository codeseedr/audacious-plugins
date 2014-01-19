
#include <sys/wait.h>
#include <signal.h>

#include <glib.h>

#include <audacious/i18n.h>
#include <audacious/misc.h>
#include <audacious/plugin.h>
#include <audacious/playlist.h>
#include <libaudcore/hook.h>
#include <libaudcore/audstrings.h>
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
    char *argv[3] = {cmd, dir, NULL};

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

        char *uri = aud_playlist_entry_get_filename(playlist, i);
        char *filename = uri_to_filename(uri);

        if (!filename)
        {
            SPRINTF(error, _("Unable to show %s in File Manager: not a local file."), uri);
            aud_interface_show_error(error);
        }
        else
        {
            if (!dir_to_show)
                dir_to_show = g_path_get_dirname(filename);
        }

        str_unref(filename);
        str_unref(uri);

        if (dir_to_show)
            break;
    }

    open_dir_fm(dir_to_show);
}

static bool_t init(void)
{
    for (int i = 0; i < G_N_ELEMENTS (menus); i ++)
        aud_plugin_menu_add (menus[i], show_playlist_entries,
            _("Show in File Manager"), "folder");

    return TRUE;
}

static void cleanup(void)
{
    for (int i = 0; i < G_N_ELEMENTS (menus); i ++)
        aud_plugin_menu_remove(menus[i], show_playlist_entries);
}

AUD_GENERAL_PLUGIN
(
    .name = N_("Show in File Manager"),
    .domain = PACKAGE,
    .init = init,
    .cleanup = cleanup,
)
