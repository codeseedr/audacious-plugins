/***
  This file is part of xmms-pulse.

  xmms-pulse is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  xmms-pulse is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with xmms-pulse; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
  USA.
***/

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <pulse/pulseaudio.h>

#include <libaudcore/runtime.h>
#include <libaudcore/drct.h>
#include <libaudcore/runtime.h>
#include <libaudcore/plugin.h>
#include <libaudcore/i18n.h>

static pa_context *context = nullptr;
static pa_stream *stream = nullptr;
static pa_threaded_mainloop *mainloop = nullptr;

static pa_cvolume volume;
static int volume_valid = 0;

static int do_trigger = 0;
static int64_t written;
static int flush_time;
static int bytes_per_second;

static int connected = 0;

static pa_time_event *volume_time_event = nullptr;

#define CHECK_DEAD_GOTO(label, warn) do { \
if (!mainloop || \
    !context || pa_context_get_state(context) != PA_CONTEXT_READY || \
    !stream || pa_stream_get_state(stream) != PA_STREAM_READY) { \
        if (warn) \
            AUDDBG("Connection died: %s\n", context ? pa_strerror(pa_context_errno(context)) : "nullptr"); \
        goto label; \
    }  \
} while(0);

#define CHECK_CONNECTED(retval) \
do { \
    if (!connected) return retval; \
} while (0);

static void info_cb(struct pa_context *c, const struct pa_sink_input_info *i, int is_last, void *userdata) {
    assert(c);

    if (!i)
        return;

    volume = i->volume;
    volume_valid = 1;
}

static void subscribe_cb(struct pa_context *c, enum pa_subscription_event_type t, uint32_t index, void *userdata) {
    pa_operation *o;

    assert(c);

    if (!stream ||
        index != pa_stream_get_index(stream) ||
        (t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE) &&
         t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_NEW)))
        return;

    if (!(o = pa_context_get_sink_input_info(c, index, info_cb, nullptr))) {
        AUDDBG("pa_context_get_sink_input_info() failed: %s\n", pa_strerror(pa_context_errno(c)));
        return;
    }

    pa_operation_unref(o);
}

static void context_state_cb(pa_context *c, void *userdata) {
    assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            pa_threaded_mainloop_signal(mainloop, 0);
            break;

        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
    }
}

static void stream_state_cb(pa_stream *s, void * userdata) {
    assert(s);

    switch (pa_stream_get_state(s)) {

        case PA_STREAM_READY:
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            pa_threaded_mainloop_signal(mainloop, 0);
            break;

        case PA_STREAM_UNCONNECTED:
        case PA_STREAM_CREATING:
            break;
    }
}

static void stream_success_cb(pa_stream *s, int success, void *userdata) {
    assert(s);

    if (userdata)
        *(int*) userdata = success;

    pa_threaded_mainloop_signal(mainloop, 0);
}

static void context_success_cb(pa_context *c, int success, void *userdata) {
    assert(c);

    if (userdata)
        *(int*) userdata = success;

    pa_threaded_mainloop_signal(mainloop, 0);
}

static void stream_request_cb(pa_stream *s, size_t length, void *userdata) {
    assert(s);

    pa_threaded_mainloop_signal(mainloop, 0);
}

static void stream_latency_update_cb(pa_stream *s, void *userdata) {
    assert(s);

    pa_threaded_mainloop_signal(mainloop, 0);
}

static void pulse_get_volume (int * l, int * r)
{
    * l = * r = 0;

    if (! connected || ! volume_valid)
        return;

    pa_threaded_mainloop_lock (mainloop);
    CHECK_DEAD_GOTO (fail, 1);

    if (volume.channels == 2)
    {
        * l = (volume.values[0] * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM;
        * r = (volume.values[1] * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM;
    }
    else
        * l = * r = (pa_cvolume_avg (& volume) * 100 + PA_VOLUME_NORM / 2) /
         PA_VOLUME_NORM;

fail:
    pa_threaded_mainloop_unlock (mainloop);
}

static void volume_time_cb(pa_mainloop_api *api, pa_time_event *e, const struct timeval *tv, void *userdata) {
    pa_operation *o;

    if (!(o = pa_context_set_sink_input_volume(context, pa_stream_get_index(stream), &volume, nullptr, nullptr)))
        AUDDBG("pa_context_set_sink_input_volume() failed: %s\n", pa_strerror(pa_context_errno(context)));
    else
        pa_operation_unref(o);

    /* We don't wait for completion of this command */

    api->time_free(volume_time_event);
    volume_time_event = nullptr;
}

static void pulse_set_volume(int l, int r) {

    if (! connected)
        return;

    pa_threaded_mainloop_lock (mainloop);
    CHECK_DEAD_GOTO (fail, 1);

    /* sanitize output volumes. */
    l = aud::clamp (l, 0, 100);
    r = aud::clamp (r, 0, 100);

    if (!volume_valid || volume.channels !=  1) {
        volume.values[0] = ((pa_volume_t) l * PA_VOLUME_NORM + 50) / 100;
        volume.values[1] = ((pa_volume_t) r * PA_VOLUME_NORM + 50) / 100;
        volume.channels = 2;
    } else {
        volume.values[0] = ((pa_volume_t) aud::max (l, r) * PA_VOLUME_NORM + 50) / 100;
        volume.channels = 1;
    }

    volume_valid = 1;

    if (connected && !volume_time_event) {
        struct timeval tv;
        pa_mainloop_api *api = pa_threaded_mainloop_get_api(mainloop);
        volume_time_event = api->time_new(api, pa_timeval_add(pa_gettimeofday(&tv), 100000), volume_time_cb, nullptr);
    }

fail:
    if (connected)
        pa_threaded_mainloop_unlock(mainloop);
}

static void pulse_pause (bool b)
{
    pa_operation *o = nullptr;
    int success = 0;

    CHECK_CONNECTED();

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 1);

    if (!(o = pa_stream_cork(stream, b, stream_success_cb, &success))) {
        AUDDBG("pa_stream_cork() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto fail;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(fail, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success)
        AUDDBG("pa_stream_cork() failed: %s\n", pa_strerror(pa_context_errno(context)));

fail:

    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
}

static int pulse_free(void) {
    size_t l = 0;
    pa_operation *o = nullptr;

    CHECK_CONNECTED(0);

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 1);

    if ((l = pa_stream_writable_size(stream)) == (size_t) -1) {
        AUDDBG("pa_stream_writable_size() failed: %s\n", pa_strerror(pa_context_errno(context)));
        l = 0;
        goto fail;
    }

    /* If this function is called twice with no pulse_write() call in
     * between this means we should trigger the playback */
    if (do_trigger) {
        int success = 0;

        if (!(o = pa_stream_trigger(stream, stream_success_cb, &success))) {
            AUDDBG("pa_stream_trigger() failed: %s\n", pa_strerror(pa_context_errno(context)));
            goto fail;
        }

        while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
            CHECK_DEAD_GOTO(fail, 1);
            pa_threaded_mainloop_wait(mainloop);
        }

        if (!success)
            AUDDBG("pa_stream_trigger() failed: %s\n", pa_strerror(pa_context_errno(context)));
    }

fail:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);

    do_trigger = !!l;
    return (int) l;
}

static int pulse_get_output_time (void)
{
    int time = 0;

    CHECK_CONNECTED(0);

    pa_threaded_mainloop_lock(mainloop);

    time = written * (int64_t) 1000 / bytes_per_second;

    pa_usec_t usec;
    int neg;
    if (pa_stream_get_latency (stream, & usec, & neg) == PA_OK)
        time -= usec / 1000;

    /* fix for AUDPLUG-308: pa_stream_get_latency() still returns positive even
     * immediately after a flush; fix the result so that we don't return less
     * than the flush time */
    if (time < flush_time)
        time = flush_time;

    pa_threaded_mainloop_unlock(mainloop);

    return time;
}

static void pulse_drain(void) {
    pa_operation *o = nullptr;
    int success = 0;

    CHECK_CONNECTED();

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 0);

    if (!(o = pa_stream_drain(stream, stream_success_cb, &success))) {
        AUDDBG("pa_stream_drain() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto fail;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(fail, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success)
        AUDDBG("pa_stream_drain() failed: %s\n", pa_strerror(pa_context_errno(context)));

fail:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
}

static void pulse_flush(int time) {
    pa_operation *o = nullptr;
    int success = 0;

    CHECK_CONNECTED();

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 1);

    written = time * (int64_t) bytes_per_second / 1000;
    flush_time = time;

    if (!(o = pa_stream_flush(stream, stream_success_cb, &success))) {
        AUDDBG("pa_stream_flush() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto fail;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(fail, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success)
        AUDDBG("pa_stream_flush() failed: %s\n", pa_strerror(pa_context_errno(context)));

fail:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
}

static void pulse_write(void* ptr, int length) {
    int writeoffs, remain, writable;

    CHECK_CONNECTED();

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 1);

    /* break large fragments into smaller fragments. --nenolod */
    for (writeoffs = 0, remain = length;
         writeoffs < length;
         writeoffs += writable, remain -= writable)
    {
         void * pptr = (char *) ptr + writeoffs;

         writable = length - writeoffs;
         int fragsize = pa_stream_writable_size(stream);

         /* don't write more than what PA is willing to handle right now. */
         if (writable > fragsize)
             writable = fragsize;

         if (pa_stream_write(stream, pptr, writable, nullptr, PA_SEEK_RELATIVE,
          (pa_seek_mode_t) 0) < 0)
         {
             AUDDBG("pa_stream_write() failed: %s\n", pa_strerror(pa_context_errno(context)));
             goto fail;
         }
    }

    do_trigger = 0;
    written += length;

fail:
    pa_threaded_mainloop_unlock(mainloop);
}

static void pulse_close(void)
{
    connected = 0;

    if (mainloop)
        pa_threaded_mainloop_stop(mainloop);

    if (stream) {
        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
        stream = nullptr;
    }

    if (context) {
        pa_context_disconnect(context);
        pa_context_unref(context);
        context = nullptr;
    }

    if (mainloop) {
        pa_threaded_mainloop_free(mainloop);
        mainloop = nullptr;
    }

    volume_time_event = nullptr;
    volume_valid = 0;
}

static pa_sample_format_t to_pulse_format (int aformat)
{
    switch (aformat)
    {
    case FMT_U8:      return PA_SAMPLE_U8;
    case FMT_S16_LE:  return PA_SAMPLE_S16LE;
    case FMT_S16_BE:  return PA_SAMPLE_S16BE;
#ifdef PA_SAMPLE_S24_32LE
    case FMT_S24_LE:  return PA_SAMPLE_S24_32LE;
    case FMT_S24_BE:  return PA_SAMPLE_S24_32BE;
#endif
#ifdef PA_SAMPLE_S32LE
    case FMT_S32_LE:  return PA_SAMPLE_S32LE;
    case FMT_S32_BE:  return PA_SAMPLE_S32BE;
#endif
	case FMT_FLOAT:   return PA_SAMPLE_FLOAT32NE;
    default:          return PA_SAMPLE_INVALID;
    }
}

static bool pulse_open(int fmt, int rate, int nch) {
    pa_sample_spec ss;

    assert(!mainloop);
    assert(!context);
    assert(!stream);
    assert(!connected);

    ss.format = to_pulse_format (fmt);
    if (ss.format == PA_SAMPLE_INVALID)
        return false;

    ss.rate = rate;
    ss.channels = nch;

    if (!pa_sample_spec_valid(&ss))
        return false;

    if (!(mainloop = pa_threaded_mainloop_new())) {
        AUDERR ("Failed to allocate main loop\n");
        return false;
    }

    pa_threaded_mainloop_lock(mainloop);

    if (!(context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "Audacious"))) {
        AUDERR ("Failed to allocate context\n");
        goto FAIL1;
    }

    pa_context_set_state_callback(context, context_state_cb, nullptr);
    pa_context_set_subscribe_callback(context, subscribe_cb, nullptr);

    if (pa_context_connect(context, nullptr, (pa_context_flags_t) 0, nullptr) < 0) {
        AUDERR ("Failed to connect to server: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL1;
    }

    if (pa_threaded_mainloop_start(mainloop) < 0) {
        AUDERR ("Failed to start main loop\n");
        goto FAIL1;
    }

    /* Wait until the context is ready */
    pa_threaded_mainloop_wait(mainloop);

    if (pa_context_get_state(context) != PA_CONTEXT_READY) {
        AUDERR ("Failed to connect to server: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL1;
    }

    if (!(stream = pa_stream_new(context, "Audacious", &ss, nullptr))) {
        AUDERR ("Failed to create stream: %s\n", pa_strerror(pa_context_errno(context)));

FAIL1:
        pa_threaded_mainloop_unlock (mainloop);
        pulse_close ();
        return false;
    }

    pa_stream_set_state_callback(stream, stream_state_cb, nullptr);
    pa_stream_set_write_callback(stream, stream_request_cb, nullptr);
    pa_stream_set_latency_update_callback(stream, stream_latency_update_cb, nullptr);

    /* Connect stream with sink and default volume */
    /* Buffer struct */

    int aud_buffer = aud_get_int(nullptr, "output_buffer_size");
    size_t buffer_size = pa_usec_to_bytes(aud_buffer, &ss) * 1000;
    pa_buffer_attr buffer = {(uint32_t) -1, (uint32_t) buffer_size,
     (uint32_t) -1, (uint32_t) -1, (uint32_t) buffer_size};

    pa_operation *o = nullptr;
    int success;

    if (pa_stream_connect_playback (stream, nullptr, & buffer, (pa_stream_flags_t)
     (PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE), nullptr, nullptr) < 0)
    {
        AUDERR ("Failed to connect stream: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }


    /* Wait until the stream is ready */
    pa_threaded_mainloop_wait(mainloop);

    if (pa_stream_get_state(stream) != PA_STREAM_READY) {
        AUDERR ("Failed to connect stream: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    /* Now subscribe to events */
    if (!(o = pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK_INPUT, context_success_cb, &success))) {
        AUDERR ("pa_context_subscribe() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    success = 0;
    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(FAIL2, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success) {
        AUDERR ("pa_context_subscribe() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    pa_operation_unref(o);

    /* Now request the initial stream info */
    if (!(o = pa_context_get_sink_input_info(context, pa_stream_get_index(stream), info_cb, nullptr))) {
        AUDERR ("pa_context_get_sink_input_info() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(FAIL2, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!volume_valid) {
        AUDERR ("pa_context_get_sink_input_info() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }
    pa_operation_unref(o);

    do_trigger = 0;
    written = 0;
    flush_time = 0;
    bytes_per_second = FMT_SIZEOF (fmt) * nch * rate;
    connected = 1;
    volume_time_event = nullptr;

    pa_threaded_mainloop_unlock(mainloop);

    return true;

FAIL2:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
    pulse_close();
    return false;
}

static bool pulse_init (void)
{
    if (! pulse_open (FMT_S16_NE, 44100, 2))
        return false;

    pulse_close ();
    return true;
}

static const char pulse_about[] =
 N_("Audacious PulseAudio Output Plugin\n\n"
    "This program is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,\n"
    "USA.");

#define AUD_PLUGIN_NAME        N_("PulseAudio Output")
#define AUD_PLUGIN_ABOUT       pulse_about
#define AUD_OUTPUT_PRIORITY    8
#define AUD_PLUGIN_INIT        pulse_init
#define AUD_OUTPUT_GET_VOLUME  pulse_get_volume
#define AUD_OUTPUT_SET_VOLUME  pulse_set_volume
#define AUD_OUTPUT_OPEN        pulse_open
#define AUD_OUTPUT_WRITE       pulse_write
#define AUD_OUTPUT_CLOSE       pulse_close
#define AUD_OUTPUT_FLUSH       pulse_flush
#define AUD_OUTPUT_PAUSE       pulse_pause
#define AUD_OUTPUT_GET_FREE    pulse_free
#define AUD_OUTPUT_WAIT_FREE   nullptr
#define AUD_OUTPUT_DRAIN       pulse_drain
#define AUD_OUTPUT_GET_TIME    pulse_get_output_time

#define AUD_DECLARE_OUTPUT
#include <libaudcore/plugin-declare.h>
