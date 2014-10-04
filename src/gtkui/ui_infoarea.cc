/*
 * ui_infoarea.c
 * Copyright 2010-2012 William Pitcock and John Lindgren
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

#include <math.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libaudcore/drct.h>
#include <libaudcore/hook.h>
#include <libaudcore/interface.h>
#include <libaudcore/playlist.h>
#include <libaudgui/libaudgui-gtk.h>

#include "ui_infoarea.h"

#define SPACING 8
#define ICON_SIZE 64
#define HEIGHT (ICON_SIZE + 2 * SPACING)

#define VIS_BANDS 12
#define VIS_WIDTH (8 * VIS_BANDS - 2)
#define VIS_CENTER (ICON_SIZE * 5 / 8 + SPACING)
#define VIS_DELAY 2 /* delay before falloff in frames */
#define VIS_FALLOFF 2 /* falloff in pixels per frame */

typedef struct {
    GtkWidget * box, * main;

    String title, artist, album;
    String last_title, last_artist, last_album;
    float alpha, last_alpha;

    gboolean stopped;
    int fade_timeout;

    GdkPixbuf * pb, * last_pb;
} UIInfoArea;

static struct {
    GtkWidget * widget;
    char bars[VIS_BANDS];
    char delay[VIS_BANDS];
} vis;

/****************************************************************************/

static UIInfoArea * area = nullptr;

static void vis_render_cb (const float * freq)
{
    /* xscale[i] = pow (256, i / VIS_BANDS) - 0.5; */
    const float xscale[VIS_BANDS + 1] = {0.5, 1.09, 2.02, 3.5, 5.85, 9.58,
     15.5, 24.9, 39.82, 63.5, 101.09, 160.77, 255.5};

    for (int i = 0; i < VIS_BANDS; i ++)
    {
        int a = ceilf (xscale[i]);
        int b = floorf (xscale[i + 1]);
        float n = 0;

        if (b < a)
            n += freq[b] * (xscale[i + 1] - xscale[i]);
        else
        {
            if (a > 0)
                n += freq[a - 1] * (a - xscale[i]);
            for (; a < b; a ++)
                n += freq[a];
            if (b < 256)
                n += freq[b] * (xscale[i + 1] - b);
        }

        /* 40 dB range */
        int x = 40 + 20 * log10f (n);
        x = aud::clamp (x, 0, 40);

        vis.bars[i] -= aud::max (0, VIS_FALLOFF - vis.delay[i]);

        if (vis.delay[i])
            vis.delay[i] --;

        if (x > vis.bars[i])
        {
            vis.bars[i] = x;
            vis.delay[i] = VIS_DELAY;
        }
    }

    if (vis.widget)
        gtk_widget_queue_draw (vis.widget);
}

static void vis_clear_cb (void)
{
    memset (vis.bars, 0, sizeof vis.bars);
    memset (vis.delay, 0, sizeof vis.delay);

    if (vis.widget)
        gtk_widget_queue_draw (vis.widget);
}

/****************************************************************************/

static void clear (GtkWidget * widget, cairo_t * cr)
{
    GtkAllocation alloc;
    gtk_widget_get_allocation (widget, & alloc);

    cairo_pattern_t * gradient = cairo_pattern_create_linear (0, 0, 0, alloc.height);
    cairo_pattern_add_color_stop_rgb (gradient, 0, 0.25, 0.25, 0.25);
    cairo_pattern_add_color_stop_rgb (gradient, 0.5, 0.15, 0.15, 0.15);
    cairo_pattern_add_color_stop_rgb (gradient, 0.5, 0.1, 0.1, 0.1);
    cairo_pattern_add_color_stop_rgb (gradient, 1, 0, 0, 0);

    cairo_set_source (cr, gradient);
    cairo_rectangle (cr, 0, 0, alloc.width, alloc.height);
    cairo_fill (cr);

    cairo_pattern_destroy (gradient);
}

static void draw_text (GtkWidget * widget, cairo_t * cr, int x, int y, int
 width, float r, float g, float b, float a, const char * font,
 const char * text)
{
    cairo_move_to(cr, x, y);
    cairo_set_source_rgba(cr, r, g, b, a);

    PangoFontDescription * desc = pango_font_description_from_string (font);
    PangoLayout * pl = gtk_widget_create_pango_layout (widget, nullptr);
    pango_layout_set_text (pl, text, -1);
    pango_layout_set_font_description(pl, desc);
    pango_font_description_free(desc);
    pango_layout_set_width (pl, width * PANGO_SCALE);
    pango_layout_set_ellipsize (pl, PANGO_ELLIPSIZE_END);

    pango_cairo_show_layout(cr, pl);

    g_object_unref(pl);
}

/****************************************************************************/

static void rgb_to_hsv (float r, float g, float b, float * h, float * s,
 float * v)
{
    float max, min;

    max = r;
    if (g > max)
        max = g;
    if (b > max)
        max = b;

    min = r;
    if (g < min)
        min = g;
    if (b < min)
        min = b;

    * v = max;

    if (max == min)
    {
        * h = 0;
        * s = 0;
        return;
    }

    if (r == max)
        * h = 1 + (g - b) / (max - min);
    else if (g == max)
        * h = 3 + (b - r) / (max - min);
    else
        * h = 5 + (r - g) / (max - min);

    * s = (max - min) / max;
}

static void hsv_to_rgb (float h, float s, float v, float * r, float * g,
 float * b)
{
    for (; h >= 2; h -= 2)
    {
        float * p = r;
        r = g;
        g = b;
        b = p;
    }

    if (h < 1)
    {
        * r = 1;
        * g = 0;
        * b = 1 - h;
    }
    else
    {
        * r = 1;
        * g = h - 1;
        * b = 0;
    }

    * r = v * (1 - s * (1 - * r));
    * g = v * (1 - s * (1 - * g));
    * b = v * (1 - s * (1 - * b));
}

static void get_color (GtkWidget * widget, int i, float * r, float * g, float * b)
{
    GdkColor * c = (gtk_widget_get_style (widget))->base + GTK_STATE_SELECTED;
    float h, s, v;

    rgb_to_hsv (c->red / 65535.0, c->green / 65535.0, c->blue / 65535.0, & h, & s, & v);

    if (s < 0.1) /* monochrome theme? use blue instead */
    {
        h = 5;
        s = 0.75;
    }

    s = 1 - 0.9 * i / (VIS_BANDS - 1);
    v = 0.75 + 0.25 * i / (VIS_BANDS - 1);

    hsv_to_rgb (h, s, v, r, g, b);
}

static int expose_vis_cb (GtkWidget * widget, GdkEventExpose * event)
{
    cairo_t * cr = gdk_cairo_create (gtk_widget_get_window (widget));

    clear (widget, cr);

    for (int i = 0; i < VIS_BANDS; i++)
    {
        int x = SPACING + 8 * i;
        int t = VIS_CENTER - vis.bars[i];
        int m = aud::min (VIS_CENTER + vis.bars[i], HEIGHT);

        float r, g, b;
        get_color (widget, i, & r, & g, & b);

        cairo_set_source_rgb (cr, r, g, b);
        cairo_rectangle (cr, x, t, 6, VIS_CENTER - t);
        cairo_fill (cr);

        cairo_set_source_rgb (cr, r * 0.3, g * 0.3, b * 0.3);
        cairo_rectangle (cr, x, VIS_CENTER, 6, m - VIS_CENTER);
        cairo_fill (cr);
    }

    cairo_destroy (cr);
    return TRUE;
}

static void draw_album_art (cairo_t * cr)
{
    g_return_if_fail (area);

    if (area->pb != nullptr)
    {
        gdk_cairo_set_source_pixbuf (cr, area->pb, SPACING, SPACING);
        cairo_paint_with_alpha (cr, area->alpha);
    }

    if (area->last_pb != nullptr)
    {
        gdk_cairo_set_source_pixbuf (cr, area->last_pb, SPACING, SPACING);
        cairo_paint_with_alpha (cr, area->last_alpha);
    }
}

static void draw_title (cairo_t * cr)
{
    g_return_if_fail (area);

    GtkAllocation alloc;
    gtk_widget_get_allocation (area->main, & alloc);

    int x = ICON_SIZE + SPACING * 2;
    int width = alloc.width - x;

    if (area->title != nullptr)
        draw_text (area->main, cr, x, SPACING, width, 1, 1, 1, area->alpha,
         "18", area->title);
    if (area->last_title != nullptr)
        draw_text (area->main, cr, x, SPACING, width, 1, 1, 1, area->last_alpha,
         "18", area->last_title);
    if (area->artist != nullptr)
        draw_text (area->main, cr, x, SPACING + ICON_SIZE / 2, width, 1, 1, 1,
         area->alpha, "9", area->artist);
    if (area->last_artist != nullptr)
        draw_text (area->main, cr, x, SPACING + ICON_SIZE / 2, width, 1, 1, 1,
         area->last_alpha, "9", area->last_artist);
    if (area->album != nullptr)
        draw_text (area->main, cr, x, SPACING + ICON_SIZE * 3 / 4, width, 0.7,
         0.7, 0.7, area->alpha, "9", area->album);
    if (area->last_album != nullptr)
        draw_text (area->main, cr, x, SPACING + ICON_SIZE * 3 / 4, width, 0.7,
         0.7, 0.7, area->last_alpha, "9", area->last_album);
}

static int expose_cb (GtkWidget * widget, GdkEventExpose * event)
{
    cairo_t * cr = gdk_cairo_create (gtk_widget_get_window (widget));

    clear (widget, cr);

    draw_album_art (cr);
    draw_title (cr);

    cairo_destroy (cr);
    return TRUE;
}

static gboolean ui_infoarea_do_fade (void)
{
    g_return_val_if_fail (area, FALSE);
    gboolean ret = FALSE;

    if (aud_drct_get_playing () && area->alpha < 1)
    {
        area->alpha += 0.1;
        ret = TRUE;
    }

    if (area->last_alpha > 0)
    {
        area->last_alpha -= 0.1;
        ret = TRUE;
    }

    gtk_widget_queue_draw (area->main);

    if (! ret)
        area->fade_timeout = 0;

    return ret;
}

static void ui_infoarea_set_title (void)
{
    g_return_if_fail (area);

    if (! aud_drct_get_playing ())
        return;

    int playlist = aud_playlist_get_playing ();
    int entry = aud_playlist_get_position (playlist);

    String title, artist, album;
    aud_playlist_entry_describe (playlist, entry, title, artist, album, TRUE);

    if (! g_strcmp0 (title, area->title) && ! g_strcmp0 (artist, area->artist)
     && ! g_strcmp0 (album, area->album))
        return;

    area->title = std::move (title);
    area->artist = std::move (artist);
    area->album = std::move (album);

    gtk_widget_queue_draw (area->main);
}

static void set_album_art (void)
{
    g_return_if_fail (area);

    if (area->pb)
        g_object_unref (area->pb);

    area->pb = audgui_pixbuf_request_current ();
    if (! area->pb)
        area->pb = audgui_pixbuf_fallback ();
    if (area->pb)
        audgui_pixbuf_scale_within (& area->pb, ICON_SIZE);
}

static void album_art_ready (void)
{
    g_return_if_fail (area);

    if (! aud_drct_get_playing ())
        return;

    set_album_art ();
    gtk_widget_queue_draw (area->main);
}

static void infoarea_next (void)
{
    g_return_if_fail (area);

    if (area->last_pb)
        g_object_unref (area->last_pb);
    area->last_pb = area->pb;
    area->pb = nullptr;

    area->last_title = std::move (area->title);
    area->last_artist = std::move (area->artist);
    area->last_album = std::move (area->album);

    area->last_alpha = area->alpha;
    area->alpha = 0;

    gtk_widget_queue_draw (area->main);
}

static void ui_infoarea_playback_start (void)
{
    g_return_if_fail (area);

    if (! area->stopped) /* moved to the next song without stopping? */
        infoarea_next ();
    area->stopped = FALSE;

    ui_infoarea_set_title ();
    set_album_art ();

    if (! area->fade_timeout)
        area->fade_timeout = g_timeout_add (30, (GSourceFunc)
         ui_infoarea_do_fade, area);
}

static void ui_infoarea_playback_stop (void)
{
    g_return_if_fail (area);

    infoarea_next ();
    area->stopped = TRUE;

    if (! area->fade_timeout)
        area->fade_timeout = g_timeout_add (30, (GSourceFunc)
         ui_infoarea_do_fade, area);
}

static void realize_cb (GtkWidget * widget)
{
    /* using a native window avoids redrawing parent widgets */
    gdk_window_ensure_native (gtk_widget_get_window (widget));
}

void ui_infoarea_show_vis (gboolean show)
{
    if (! area)
        return;

    if (show)
    {
        if (vis.widget)
            return;

        vis.widget = gtk_drawing_area_new ();

        /* note: "realize" signal must be connected before adding to box */
        g_signal_connect (vis.widget, "realize", (GCallback) realize_cb, nullptr);

        gtk_widget_set_size_request (vis.widget, VIS_WIDTH + 2 * SPACING, HEIGHT);
        gtk_box_pack_start ((GtkBox *) area->box, vis.widget, FALSE, FALSE, 0);

        g_signal_connect (vis.widget, "expose-event", (GCallback) expose_vis_cb, nullptr);
        gtk_widget_show (vis.widget);

        aud_vis_func_add (AUD_VIS_TYPE_CLEAR, (VisFunc) vis_clear_cb);
        aud_vis_func_add (AUD_VIS_TYPE_FREQ, (VisFunc) vis_render_cb);
    }
    else
    {
        if (! vis.widget)
            return;

        aud_vis_func_remove ((VisFunc) vis_clear_cb);
        aud_vis_func_remove ((VisFunc) vis_render_cb);

        gtk_widget_destroy (vis.widget);

        memset (& vis, 0, sizeof vis);
    }
}

static void destroy_cb (GtkWidget * widget)
{
    g_return_if_fail (area);

    ui_infoarea_show_vis (FALSE);

    hook_dissociate ("playlist update", (HookFunction) ui_infoarea_set_title);
    hook_dissociate ("playback begin", (HookFunction) ui_infoarea_playback_start);
    hook_dissociate ("playback stop", (HookFunction) ui_infoarea_playback_stop);
    hook_dissociate ("current art ready", (HookFunction) album_art_ready);

    if (area->fade_timeout)
    {
        g_source_remove (area->fade_timeout);
        area->fade_timeout = 0;
    }

    if (area->pb)
        g_object_unref (area->pb);
    if (area->last_pb)
        g_object_unref (area->last_pb);

    delete area;
    area = nullptr;
}

GtkWidget * ui_infoarea_new (void)
{
    g_return_val_if_fail (! area, nullptr);
    area = new UIInfoArea ();

    area->box = gtk_hbox_new (FALSE, 0);

    area->main = gtk_drawing_area_new ();
    gtk_widget_set_size_request (area->main, ICON_SIZE + 2 * SPACING, HEIGHT);
    gtk_box_pack_start ((GtkBox *) area->box, area->main, TRUE, TRUE, 0);

    g_signal_connect (area->main, "expose-event", (GCallback) expose_cb, nullptr);

    hook_associate ("playlist update", (HookFunction) ui_infoarea_set_title, nullptr);
    hook_associate ("playback begin", (HookFunction) ui_infoarea_playback_start, nullptr);
    hook_associate ("playback stop", (HookFunction) ui_infoarea_playback_stop, nullptr);
    hook_associate ("current art ready", (HookFunction) album_art_ready, nullptr);

    g_signal_connect (area->box, "destroy", (GCallback) destroy_cb, nullptr);

    if (aud_drct_get_playing ())
    {
        ui_infoarea_playback_start ();

        /* skip fade-in */
        area->alpha = 1;

        if (area->fade_timeout)
        {
            g_source_remove (area->fade_timeout);
            area->fade_timeout = 0;
        }
    }

    GtkWidget * frame = gtk_frame_new (nullptr);
    gtk_frame_set_shadow_type ((GtkFrame *) frame, GTK_SHADOW_IN);
    gtk_container_add ((GtkContainer *) frame, area->box);
    return frame;
}
