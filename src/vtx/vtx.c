/*  VTX Input Plugin for Audacious
 *
 *  Copyright (C) 2002-2004 Sashnov Alexander
 *  Copyright (C) 2010 Michał Lipski <tallica@o2.pl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#include <libaudcore/i18n.h>
#include <libaudcore/input.h>
#include <libaudcore/plugin.h>

#include "vtx.h"
#include "ayemu.h"

#define SNDBUFSIZE 1024
static gchar sndbuf[SNDBUFSIZE];
static gint freq = 44100;
static gint chans = 2;
static gint bits = 16;

ayemu_ay_t ay;
ayemu_vtx_t vtx;

static const gchar *vtx_fmts[] = { "vtx", NULL };

gint vtx_is_our_fd(const gchar * filename, VFSFile * fp)
{
    gchar buf[2];
    if (vfs_fread(buf, 1, 2, fp) < 2)
        return FALSE;
    return (!g_ascii_strncasecmp(buf, "ay", 2) || !g_ascii_strncasecmp(buf, "ym", 2));
}

Tuple *vtx_get_song_tuple_from_vtx(const gchar * filename, ayemu_vtx_t * in)
{
    Tuple *out = tuple_new_from_filename(filename);

    tuple_set_str(out, FIELD_ARTIST, in->hdr.author);
    tuple_set_str(out, FIELD_TITLE, in->hdr.title);

    tuple_set_int(out, FIELD_LENGTH, in->hdr.regdata_size / 14 * 1000 / 50);

    tuple_set_str(out, FIELD_GENRE, (in->hdr.chiptype == AYEMU_AY) ? "AY chiptunes" : "YM chiptunes");
    tuple_set_str(out, FIELD_ALBUM, in->hdr.from);

    tuple_set_str(out, FIELD_QUALITY, _("sequenced"));
    tuple_set_str(out, FIELD_CODEC, in->hdr.tracker);

    tuple_set_int(out, FIELD_YEAR, in->hdr.year);

    return out;
}

Tuple *vtx_probe_for_tuple(const gchar *filename, VFSFile *fd)
{
    ayemu_vtx_t tmp;

    if (ayemu_vtx_open(&tmp, filename))
    {
        Tuple *ti = vtx_get_song_tuple_from_vtx(filename, &tmp);
        ayemu_vtx_free(&tmp);
        return ti;
    }

    return NULL;
}

static gboolean vtx_play(const gchar * filename, VFSFile * file)
{
    gboolean eof = FALSE;
    void *stream;               /* pointer to current position in sound buffer */
    guchar regs[14];
    gint need;
    gint left;                   /* how many sound frames can play with current AY register frame */
    gint donow;
    gint rate;

    left = 0;
    rate = chans * (bits / 8);

    memset(&ay, 0, sizeof(ay));

    if (!ayemu_vtx_open(&vtx, filename))
    {
        g_print("libvtx: Error read vtx header from %s\n", filename);
        return FALSE;
    }
    else if (!ayemu_vtx_load_data(&vtx))
    {
        g_print("libvtx: Error read vtx data from %s\n", filename);
        return FALSE;
    }

    ayemu_init(&ay);
    ayemu_set_chip_type(&ay, vtx.hdr.chiptype, NULL);
    ayemu_set_chip_freq(&ay, vtx.hdr.chipFreq);
    ayemu_set_stereo(&ay, vtx.hdr.stereo, NULL);

    if (aud_input_open_audio(FMT_S16_NE, freq, chans) == 0)
    {
        g_print("libvtx: output audio error!\n");
        return FALSE;
    }

    aud_input_set_bitrate(14 * 50 * 8);

    while (!aud_input_check_stop() && !eof)
    {
        /* (time in sec) * 50 = offset in AY register data frames */
        int seek_value = aud_input_check_seek();
        if (seek_value >= 0)
            vtx.pos = seek_value / 20;

        /* fill sound buffer */
        stream = sndbuf;

        for (need = SNDBUFSIZE / rate; need > 0; need -= donow)
        {
            if (left > 0)
            {                   /* use current AY register frame */
                donow = (need > left) ? left : need;
                left -= donow;
                stream = ayemu_gen_sound(&ay, (char *)stream, donow * rate);
            }
            else
            {                   /* get next AY register frame */
                if (ayemu_vtx_get_next_frame(&vtx, (char *)regs) == 0)
                {
                    donow = need;
                    memset(stream, 0, donow * rate);
                    eof = TRUE;
                }
                else
                {
                    left = freq / vtx.hdr.playerFreq;
                    ayemu_set_regs(&ay, regs);
                    donow = 0;
                }
            }
        }

        aud_input_write_audio(sndbuf, SNDBUFSIZE);
    }

    ayemu_vtx_free(&vtx);

    return TRUE;
}

static const char vtx_about[] =
 N_("Vortex file format player by Sashnov Alexander <sashnov@ngs.ru>\n"
    "Based on in_vtx.dll by Roman Sherbakov <v_soft@microfor.ru>\n"
    "Audacious plugin by Pavel Vymetalek <pvymetalek@seznam.cz>");

AUD_INPUT_PLUGIN
(
    .name = N_("VTX Decoder"),
    .domain = PACKAGE,
    .about_text = vtx_about,
    .play = vtx_play,
    .file_info_box = vtx_file_info,
    .probe_for_tuple = vtx_probe_for_tuple,
    .is_our_file_from_vfs = vtx_is_our_fd,
    .extensions = vtx_fmts
)
