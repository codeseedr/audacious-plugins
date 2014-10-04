/*
 * CoreAudio Output Plugin for Audacious
 * Copyright 2014 William Pitcock
 *
 * Based on:
 * SDL Output Plugin for Audacious
 * Copyright 2010 John Lindgren
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

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/interface.h>
#include <libaudcore/plugin.h>
#include <libaudcore/runtime.h>
#include <libaudcore/ringbuf.h>
#include <libaudcore/i18n.h>

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>

class CoreAudioPlugin : public OutputPlugin {
public:
    static const char about[];
    static const char * const defaults[];

    static constexpr PluginInfo info = {
        N_("CoreAudio output"),
        PACKAGE,
        about,
        nullptr
    };

    constexpr CoreAudioPlugin () : OutputPlugin (info, 5) {}

    bool init ();
    void cleanup ();

    StereoVolume get_volume ();
    void set_volume (StereoVolume vol);

    bool open_audio (int format, int rate_, int chan_);
    void close_audio ();

    int buffer_free ();
    void period_wait ();

    void write_audio (void * data, int len);
    void drain ();

    int output_time ();
    void pause (bool paused);
    void flush (int time);

protected:
    static OSStatus callback (void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData);
    void check_started ();
};

EXPORT CoreAudioPlugin aud_plugin_instance;

#define VOLUME_RANGE 40 /* decibels */

#define error(...) do { \
    aud_ui_show_error (str_printf ("CoreAudio error: " __VA_ARGS__)); \
} while (0)

struct AudioUnitFormatDescriptionMap {
    int aud_format;
    unsigned int mBitsPerChannel;
    unsigned int mBytesPerChannel;
    unsigned int mFormatFlags;
};
static struct AudioUnitFormatDescriptionMap AUFormatMap[] = {
    {FMT_S16_LE, 16, sizeof (int16_t), kAudioFormatFlagIsSignedInteger},
    {FMT_S16_BE, 16, sizeof (int16_t), kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsBigEndian},
    {FMT_U16_LE, 16, sizeof (int16_t), 0},
    {FMT_U16_BE, 16, sizeof (int16_t), kAudioFormatFlagIsBigEndian},
    {FMT_S24_LE, 24, sizeof (int32_t), kAudioFormatFlagIsSignedInteger},
    {FMT_S24_BE, 24, sizeof (int32_t), kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsBigEndian},
    {FMT_U24_LE, 24, sizeof (int32_t), 0},
    {FMT_U24_BE, 24, sizeof (int32_t), kAudioFormatFlagIsBigEndian},
    {FMT_S32_LE, 32, sizeof (int32_t), kAudioFormatFlagIsSignedInteger},
    {FMT_S32_BE, 32, sizeof (int32_t), kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsBigEndian},
    {FMT_U32_LE, 32, sizeof (int32_t), 0},
    {FMT_U32_BE, 32, sizeof (int32_t), kAudioFormatFlagIsBigEndian},
    {FMT_FLOAT,  32, sizeof (float),   kAudioFormatFlagIsFloat},
};

static RingBuf <unsigned char> buffer;
static int vol_left = 0, vol_right = 0;
static int chan = 0;
static int rate = 0;
static int buffer_bytes_per_channel = 0;

static int64_t frames_written = 0;
static bool prebuffer_flag = false;
static bool paused_flag = false;

static int block_delay = 0;
static struct timeval block_time = {0, 0};

static AudioComponent output_comp = nullptr;
static AudioComponentInstance output_instance = nullptr;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

const char CoreAudioPlugin::about[] =
    N_("CoreAudio Output Plugin for Audacious\n"
       "Copyright 2014 William Pitcock\n\n"
       "Based on SDL Output Plugin for Audacious\n"
       "Copyright 2010 John Lindgren");

const char * const CoreAudioPlugin::defaults[] = {
    "vol_left", "100",
    "vol_right", "100",
    nullptr};

bool CoreAudioPlugin::init (void)
{
    aud_config_set_defaults ("coreaudio", defaults);

    vol_left = aud_get_int ("coreaudio", "vol_left");
    vol_right = aud_get_int ("coreaudio", "vol_right");

    /* open the default audio device */
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    desc.componentManufacturer = 0;

    output_comp = AudioComponentFindNext (NULL, & desc);
    if (! output_comp)
    {
        error ("Failed to open default audio device.\n");
        return 0;
    }

    if (AudioComponentInstanceNew (output_comp, & output_instance))
    {
        error ("Failed to open default audio device.\n");
        return 0;
    }

    AURenderCallbackStruct input;
    input.inputProc = callback;
    input.inputProcRefCon = this;

    if (AudioUnitSetProperty (output_instance, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, & input, sizeof input))
    {
        error ("Unable to attach an IOProc to the selected audio unit.\n");
        return 0;
    }

    return 1;
}

void CoreAudioPlugin::cleanup (void)
{
}

StereoVolume CoreAudioPlugin::get_volume ()
{
    return {vol_left, vol_right};
}

void CoreAudioPlugin::set_volume (StereoVolume vol)
{
    int vol_max;
    float factor;

    vol_left = vol.left;
    vol_right = vol.right;

    vol_max = aud::max (vol_left, vol_right);

    aud_set_int ("coreaudio", "vol_left", vol_left);
    aud_set_int ("coreaudio", "vol_right", vol_right);

    factor = (vol_max == 0) ? 0.0 : powf (10, (float) VOLUME_RANGE * (vol_max - 100) / 100 / 20);

    AudioUnitSetParameter (output_instance, kHALOutputParam_Volume, kAudioUnitScope_Global, 0, factor, 0);
}

OSStatus CoreAudioPlugin::callback (void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
    pthread_mutex_lock (& mutex);

    int len = ioData->mBuffers[0].mDataByteSize;
    unsigned char * buf = (unsigned char *) ioData->mBuffers[0].mData;
    ioData->mBuffers[0].mNumberChannels = chan;

    int copy = aud::min (len, buffer.len ());

    buffer.move_out (buf, copy);
    if (copy < len)
        memset (buf + copy, 0, len - copy);

    /* At this moment, we know that there is a delay of (at least) the block of
     * data just written.  We save the block size and the current time for
     * estimating the delay later on. */
    block_delay = copy / (buffer_bytes_per_channel * chan) * 1000 / rate;
    gettimeofday (& block_time, nullptr);

    pthread_cond_broadcast (& cond);
    pthread_mutex_unlock (& mutex);

    return 0;
}

bool CoreAudioPlugin::open_audio (int format, int rate_, int chan_)
{
    struct AudioUnitFormatDescriptionMap * m = nullptr;

    for (struct AudioUnitFormatDescriptionMap it : AUFormatMap)
    {
        if (it.aud_format == format)
        {
            m = & it;
            break;
        }
    }

    if (! m)
    {
        error ("The requested audio format %d is unsupported.\n", format);
        return 0;
    }

    AUDDBG ("Opening audio for %d channels, %d Hz.\n", chan, rate);

    chan = chan_;
    rate = rate_;

    buffer_bytes_per_channel = m->mBytesPerChannel;

    int buffer_size = buffer_bytes_per_channel * chan * (aud_get_int (nullptr, "output_buffer_size") * rate / 1000);
    buffer.alloc (buffer_size);

    frames_written = 0;
    prebuffer_flag = 1;
    paused_flag = 0;

    if (AudioUnitInitialize (output_instance))
    {
        error ("Unable to initialize audio unit instance\n");
        return 0;
    }

    AudioStreamBasicDescription streamFormat;
    streamFormat.mSampleRate = rate;
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags = m->mFormatFlags;
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mChannelsPerFrame = chan;
    streamFormat.mBitsPerChannel = m->mBitsPerChannel;
    streamFormat.mBytesPerPacket = chan * buffer_bytes_per_channel;
    streamFormat.mBytesPerFrame = chan * buffer_bytes_per_channel;

    AUDDBG("Stream format:\n");
    AUDDBG(" Channels: %d\n", streamFormat.mChannelsPerFrame);
    AUDDBG(" Sample rate: %f\n", streamFormat.mSampleRate);
    AUDDBG(" Bits per channel: %d\n", streamFormat.mBitsPerChannel);
    AUDDBG(" Bytes per frame: %d\n", streamFormat.mBytesPerFrame);

    if (AudioUnitSetProperty (output_instance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &streamFormat, sizeof(streamFormat)))
    {
        error ("Failed to set audio unit input property.\n");
        buffer.destroy ();
        return 0;
    }

    if (AudioOutputUnitStart (output_instance))
    {
        error ("Unable to start audio unit.\n");
        buffer.destroy ();
        return 0;
    }

    return 1;
}

void CoreAudioPlugin::close_audio ()
{
    AUDDBG ("Closing audio.\n");

    AudioOutputUnitStop (output_instance);

    buffer.destroy ();
}

int CoreAudioPlugin::buffer_free ()
{
    pthread_mutex_lock (& mutex);
    int space = buffer.space ();
    pthread_mutex_unlock (& mutex);
    return space;
}

void CoreAudioPlugin::check_started ()
{
    if (! prebuffer_flag)
        return;

    AUDDBG ("Starting playback.\n");
    prebuffer_flag = 0;
    block_delay = 0;
}

void CoreAudioPlugin::period_wait ()
{
    pthread_mutex_lock (& mutex);

    while (! buffer.space ())
    {
        if (! paused_flag)
            check_started ();

        pthread_cond_wait (& cond, & mutex);
    }

    pthread_mutex_unlock (& mutex);
}

void CoreAudioPlugin::write_audio (void * data, int len)
{
    pthread_mutex_lock (& mutex);

    assert (len <= buffer.space ());

    buffer.copy_in ((const unsigned char *) data, len);

    frames_written += len / (buffer_bytes_per_channel * chan);

    pthread_mutex_unlock (& mutex);
}

void CoreAudioPlugin::drain ()
{
    AUDDBG ("Draining.\n");
    pthread_mutex_lock (& mutex);

    check_started ();

    while (buffer.len ())
        pthread_cond_wait (& cond, & mutex);

    pthread_mutex_unlock (& mutex);
}

int CoreAudioPlugin::output_time ()
{
    pthread_mutex_lock (& mutex);

    int out = (int64_t) (frames_written - buffer.len () / (buffer_bytes_per_channel * chan))
     * 1000 / rate;

    /* Estimate the additional delay of the last block written. */
    if (! prebuffer_flag && ! paused_flag && block_delay)
    {
        struct timeval cur;
        gettimeofday (& cur, nullptr);

        int elapsed = 1000 * (cur.tv_sec - block_time.tv_sec) + (cur.tv_usec -
         block_time.tv_usec) / 1000;

        if (elapsed < block_delay)
            out -= block_delay - elapsed;
    }

    pthread_mutex_unlock (& mutex);
    return out;
}

void CoreAudioPlugin::pause (bool pause)
{
    AUDDBG ("%sause.\n", pause ? "P" : "Unp");
    pthread_mutex_lock (& mutex);

    paused_flag = pause;

    if (pause)
        AudioOutputUnitStop (output_instance);
    else
    {
        if (AudioOutputUnitStart (output_instance))
        {
            error ("Unable to restart audio unit after pausing.\n");
            close_audio ();
        }
    }

    pthread_cond_broadcast (& cond); /* wake up period wait */
    pthread_mutex_unlock (& mutex);
}

void CoreAudioPlugin::flush (int time)
{
    AUDDBG ("Seek requested; discarding buffer.\n");
    pthread_mutex_lock (& mutex);

    buffer.discard ();

    frames_written = aud::rescale<int64_t> (time, 1000, rate);
    prebuffer_flag = 1;

    pthread_cond_broadcast (& cond); /* wake up period wait */
    pthread_mutex_unlock (& mutex);
}
