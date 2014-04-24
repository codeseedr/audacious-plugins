/*
 * Audacious: A cross-platform multimedia player
 * Copyright (c) 2006-2010 William Pitcock, Tony Vroon, George Averill, Giacomo
 *  Lozito, Derek Pomery and Yoshiki Yazawa, and John Lindgren.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include <libaudcore/i18n.h>
#include <libaudcore/playlist.h>
#include <libaudcore/plugin.h>
#include <libaudcore/audstrings.h>

static void strip_char (char * text, char c)
{
    char * set = text;
    char a;

    while ((a = * text ++))
        if (a != c)
            * set ++ = a;

    * set = 0;
}

static char * read_win_text (VFSFile * file)
{
    void * raw = NULL;
    vfs_file_read_all (file, & raw, NULL);
    if (! raw)
        return NULL;

    strip_char (raw, '\r');
    return raw;
}

static char * split_line (char * line)
{
    char * feed = strchr (line, '\n');
    if (! feed)
        return NULL;

    * feed = 0;
    return feed + 1;
}

static bool_t playlist_load_m3u (const char * path, VFSFile * file,
 char * * title, Index * filenames, Index * tuples)
{
    char * text = read_win_text (file);
    if (! text)
        return FALSE;

    * title = NULL;

    char * parse = text;

    while (parse)
    {
        char * next = split_line (parse);

        while (* parse == ' ' || * parse == '\t')
            parse ++;

        if (! * parse)
            goto NEXT;

        if (* parse == '#')
            goto NEXT;

        char * s = uri_construct (parse, path);
        if (s)
            index_insert (filenames, -1, s);

NEXT:
        parse = next;
    }

    g_free (text);
    return TRUE;
}

static bool_t playlist_save_m3u (const char * path, VFSFile * file,
 const char * title, Index * filenames, Index * tuples)
{
    int count = index_count (filenames);

    for (int i = 0; i < count; i ++)
        vfs_fprintf (file, "%s\n", (const char *) index_get (filenames, i));

    return TRUE;
}

static const char * const m3u_exts[] = {"m3u", "m3u8", NULL};

AUD_PLAYLIST_PLUGIN
(
 .name = N_("M3U Playlists"),
 .domain = PACKAGE,
 .extensions = m3u_exts,
 .load = playlist_load_m3u,
 .save = playlist_save_m3u
)
