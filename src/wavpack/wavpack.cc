#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <wavpack/wavpack.h>

#define WANT_VFS_STDIO_COMPAT
#include <audacious/audtag.h>
#include <libaudcore/runtime.h>
#include <libaudcore/i18n.h>
#include <libaudcore/input.h>
#include <libaudcore/plugin.h>
#include <libaudcore/audstrings.h>

#define BUFFER_SIZE 256 /* read buffer size, in samples / frames */
#define SAMPLE_SIZE(a) (a == 8 ? sizeof(uint8_t) : (a == 16 ? sizeof(uint16_t) : sizeof(uint32_t)))
#define SAMPLE_FMT(a) (a == 8 ? FMT_S8 : (a == 16 ? FMT_S16_NE : (a == 24 ? FMT_S24_NE : FMT_S32_NE)))


/* Audacious VFS wrappers for Wavpack stream reading
 */

static int32_t
wv_read_bytes(void *id, void *data, int32_t bcount)
{
    return ((VFSFile *) id)->fread (data, 1, bcount);
}

static uint32_t
wv_get_pos(void *id)
{
    return aud::clamp (((VFSFile *) id)->ftell (), (int64_t) 0, (int64_t) 0xffffffff);
}

static int
wv_set_pos_abs(void *id, uint32_t pos)
{
    return ((VFSFile *) id)->fseek (pos, VFS_SEEK_SET);
}

static int
wv_set_pos_rel(void *id, int32_t delta, int mode)
{
    return ((VFSFile *) id)->fseek (delta, to_vfs_seek_type(mode));
}

static int
wv_push_back_byte(void *id, int c)
{
    return (((VFSFile *) id)->fseek (-1, VFS_SEEK_CUR) == 0) ? c : -1;
}

static uint32_t
wv_get_length(void *id)
{
    return aud::clamp (((VFSFile *) id)->fsize (), (int64_t) 0, (int64_t) 0xffffffff);
}

static int wv_can_seek(void *id)
{
    return (((VFSFile *) id)->fsize () >= 0);
}

static int32_t wv_write_bytes(void *id, void *data, int32_t bcount)
{
    return ((VFSFile *) id)->fwrite (data, 1, bcount);
}

WavpackStreamReader wv_readers = {
    wv_read_bytes,
    wv_get_pos,
    wv_set_pos_abs,
    wv_set_pos_rel,
    wv_push_back_byte,
    wv_get_length,
    wv_can_seek,
    wv_write_bytes
};

static bool wv_attach (const char * filename, VFSFile & wv_input,
 VFSFile & wvc_input, WavpackContext * * ctx, char * error, int flags)
{
    if (flags & OPEN_WVC)
    {
        StringBuf corrFilename = str_concat ({filename, "c"});
        if (VFSFile::test_file (corrFilename, VFS_IS_REGULAR))
            wvc_input = VFSFile (corrFilename, "r");
    }

    * ctx = WavpackOpenFileInputEx (& wv_readers, & wv_input, & wvc_input, error, flags, 0);
    return (* ctx != nullptr);
}

static void wv_deattach (WavpackContext * ctx)
{
    WavpackCloseFile(ctx);
}

static bool wv_play (const char * filename, VFSFile & file)
{
    int32_t *input = nullptr;
    void *output = nullptr;
    int sample_rate, num_channels, bits_per_sample;
    unsigned num_samples;
    WavpackContext *ctx = nullptr;
    VFSFile wvc_input;
    bool error = false;

    if (! wv_attach (filename, file, wvc_input, & ctx, nullptr, OPEN_TAGS | OPEN_WVC))
    {
        AUDERR ("Error opening Wavpack file '%s'.", filename);
        error = true;
        goto error_exit;
    }

    sample_rate = WavpackGetSampleRate(ctx);
    num_channels = WavpackGetNumChannels(ctx);
    bits_per_sample = WavpackGetBitsPerSample(ctx);
    num_samples = WavpackGetNumSamples(ctx);

    if (!aud_input_open_audio(SAMPLE_FMT(bits_per_sample), sample_rate, num_channels))
    {
        AUDERR ("Error opening audio output.");
        error = true;
        goto error_exit;
    }

    input = g_new(int32_t, BUFFER_SIZE * num_channels);
    output = g_malloc(BUFFER_SIZE * num_channels * SAMPLE_SIZE(bits_per_sample));
    if (input == nullptr || output == nullptr)
        goto error_exit;

    aud_input_set_bitrate(WavpackGetAverageBitrate(ctx, num_channels));

    while (! aud_input_check_stop ())
    {
        int seek_value = aud_input_check_seek ();
        if (seek_value >= 0)
            WavpackSeekSample (ctx, (int64_t) seek_value * sample_rate / 1000);

        /* Decode audio data */
        unsigned samples_left = num_samples - WavpackGetSampleIndex(ctx);

        if (samples_left == 0)
            break;

        int ret = WavpackUnpackSamples(ctx, input, BUFFER_SIZE);

        if (ret < 0)
        {
            AUDERR ("Error decoding file.\n");
            break;
        }
        else
        {
            /* Perform audio data conversion and output */
            int32_t *rp = input;
            int8_t *wp = (int8_t *) output;
            int16_t *wp2 = (int16_t *) output;
            int32_t *wp4 = (int32_t *) output;

            if (bits_per_sample == 8)
            {
                for (int i = 0; i < ret * num_channels; i++, wp++, rp++)
                    *wp = *rp & 0xff;
            }
            else if (bits_per_sample == 16)
            {
                for (int i = 0; i < ret * num_channels; i++, wp2++, rp++)
                    *wp2 = *rp & 0xffff;
            }
            else if (bits_per_sample == 24 || bits_per_sample == 32)
            {
                for (int i = 0; i < ret * num_channels; i++, wp4++, rp++)
                    *wp4 = *rp;
            }

            aud_input_write_audio(output, ret * num_channels * SAMPLE_SIZE(bits_per_sample));
        }
    }

error_exit:

    g_free(input);
    g_free(output);
    wv_deattach (ctx);

    return ! error;
}

static StringBuf
wv_get_quality(WavpackContext *ctx)
{
    int mode = WavpackGetMode(ctx);
    const char *quality;

    if (mode & MODE_LOSSLESS)
        quality = _("lossless");
    else if (mode & MODE_HYBRID)
        quality = _("lossy (hybrid)");
    else
        quality = _("lossy");

    return str_concat ({quality, (mode & MODE_WVC) ? " (wvc corrected)" : "",
     (mode & MODE_DNS) ? " (dynamic noise shaped)" : ""});
}

static Tuple
wv_probe_for_tuple(const char * filename, VFSFile & fd)
{
    WavpackContext *ctx;
    Tuple tuple;
    char error[1024];

    ctx = WavpackOpenFileInputEx(&wv_readers, &fd, nullptr, error, OPEN_TAGS, 0);

    if (ctx == nullptr)
        return tuple;

    AUDDBG("starting probe of %s\n", fd.filename ());

    tuple.set_filename (filename);

    tuple.set_int (FIELD_LENGTH,
        ((uint64_t) WavpackGetNumSamples(ctx) * 1000) / (uint64_t) WavpackGetSampleRate(ctx));
    tuple.set_str (FIELD_CODEC, "WavPack");

    tuple.set_str (FIELD_QUALITY, wv_get_quality (ctx));

    WavpackCloseFile(ctx);

    if (! fd.fseek (0, VFS_SEEK_SET))
        audtag::tuple_read (tuple, fd);

    AUDDBG("returning tuple for file %s\n", fd.filename ());
    return tuple;
}

static bool wv_write_tag (const char * filename, VFSFile & handle, const Tuple & tuple)
{
    return audtag::tuple_write(tuple, handle, audtag::TagType::APE);
}

static const char wv_about[] =
 N_("Copyright 2006 William Pitcock <nenolod@nenolod.net>\n\n"
    "Some of the plugin code was by Miles Egan.");

static const char *wv_fmts[] = { "wv", nullptr };

#define AUD_PLUGIN_NAME        N_("WavPack Decoder")
#define AUD_PLUGIN_ABOUT       wv_about
#define AUD_INPUT_IS_OUR_FILE  nullptr
#define AUD_INPUT_PLAY         wv_play
#define AUD_INPUT_EXTS         wv_fmts
#define AUD_INPUT_READ_TUPLE   wv_probe_for_tuple
#define AUD_INPUT_WRITE_TUPLE  wv_write_tag

#define AUD_DECLARE_INPUT
#include <libaudcore/plugin-declare.h>
