/*
   XMMS-SID - SIDPlay input plugin for X MultiMedia System (XMMS)

   libSIDPlay v2 support

   Programmed and designed by Matti 'ccr' Hamalainen <ccr@tnsp.org>
   (C) Copyright 1999-2009 Tecnic Software productions (TNSP)

   Ported to sidplayfp:
   (C) Copyright 2013 Cristian Morales Vega and Hans de Goede

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "xs_config.h"
#include "xs_sidplay2.h"

#include <string.h>

#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidDatabase.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/builders/residfp.h>

#include <libaudcore/runtime.h>
#include <libaudcore/vfs.h>

struct SidState {
    sidplayfp *currEng;
    sidbuilder *currBuilder;
    SidTune *currTune;
};

static SidState state;


/* Check if we can play the given file
 */
bool xs_sidplayfp_probe(const void *buf, int64_t bufSize)
{
    if (bufSize < 4)
        return false;

    return !memcmp(buf, "PSID", 4) || !memcmp(buf, "RSID", 4);
}


/* Initialize SIDPlayFP
 */
bool xs_sidplayfp_init()
{
    /* Initialize the engine */
    state.currEng = new sidplayfp;

    /* Get current configuration */
    SidConfig config = state.currEng->config();

    /* Configure channels and stuff */
    switch (xs_cfg.audioChannels)
    {
    case XS_CHN_STEREO:
        config.playback = SidConfig::STEREO;
        break;

    case XS_CHN_MONO:
        config.playback = SidConfig::MONO;
        break;
    }

    /* Audio parameters sanity checking and setup */
    config.frequency = xs_cfg.audioFrequency;

    /* Initialize builder object */
    state.currBuilder = new ReSIDfpBuilder("ReSIDfp builder");

    /* Builder object created, initialize it */
    state.currBuilder->create(state.currEng->info().maxsids());
    if (!state.currBuilder->getStatus()) {
        AUDERR("reSID->create() failed.\n");
        return false;
    }

    state.currBuilder->filter(xs_cfg.emulateFilters);
    if (!state.currBuilder->getStatus()) {
        AUDERR("reSID->filter(%d) failed.\n", xs_cfg.emulateFilters);
        return false;
    }

    config.sidEmulation = state.currBuilder;

    /* Clockspeed settings */
    switch (xs_cfg.clockSpeed) {
    case XS_CLOCK_NTSC:
        config.defaultC64Model = SidConfig::NTSC;
        break;

    default:
        AUDERR("[SIDPlayFP] Invalid clockSpeed=%d, falling back to PAL.\n",
            xs_cfg.clockSpeed);

    case XS_CLOCK_PAL:
        config.defaultC64Model = SidConfig::PAL;
        xs_cfg.clockSpeed = XS_CLOCK_PAL;
        break;
    }

    config.forceC64Model = xs_cfg.forceSpeed;

    /* Configure rest of the emulation */
    if (xs_cfg.mos8580)
        config.defaultSidModel = SidConfig::MOS8580;
    else
        config.defaultSidModel = SidConfig::MOS6581;

    config.forceSidModel = xs_cfg.forceModel;

    /* Now set the emulator configuration */
    if (!state.currEng->config(config)) {
        AUDERR("[SIDPlayFP] Emulator engine configuration failed!\n");
        return false;
    }

    /* Create the sidtune */
    state.currTune = new SidTune(0);

    return true;
}


/* Close SIDPlayFP engine
 */
void xs_sidplayfp_close()
{
    /* Free internals */
    if (state.currBuilder) {
        delete state.currBuilder;
        state.currBuilder = nullptr;
    }

    if (state.currEng) {
        delete state.currEng;
        state.currEng = nullptr;
    }

    if (state.currTune) {
        delete state.currTune;
        state.currTune = nullptr;
    }
}


/* Initialize current song and sub-tune
 */
bool xs_sidplayfp_initsong(int subtune)
{
    if (!state.currTune->selectSong(subtune)) {
        AUDERR("[SIDPlayFP] currTune->selectSong() failed\n");
        return false;
    }

    if (!state.currEng->load(state.currTune)) {
        AUDERR("[SIDPlayFP] currEng->load() failed\n");
        return false;
    }

    return true;
}


/* Emulate and render audio data to given buffer
 */
unsigned xs_sidplayfp_fillbuffer(char * audioBuffer, unsigned audioBufSize)
{
    return state.currEng->play((short *)audioBuffer, audioBufSize / 2) * 2;
}


/* Load a given SID-tune file
 */
bool xs_sidplayfp_load(const void *buf, int64_t bufSize)
{
    static bool loaded_roms = false;

    /* In xs_sidplayfp_init aud-vfs is not initialized yet, so try to load
       the optional rom files on the first xs_sidplayfp_load call. */
    if (!loaded_roms) {
        VFSFile kernal_file("file://" SIDDATADIR "sidplayfp/kernal", "r");
        VFSFile basic_file("file://" SIDDATADIR "sidplayfp/basic", "r");
        VFSFile chargen_file("file://" SIDDATADIR "sidplayfp/chargen", "r");

        if (kernal_file && basic_file && chargen_file)
        {
            Index<char> kernal = kernal_file.read_all();
            Index<char> basic = basic_file.read_all();
            Index<char> chargen = chargen_file.read_all();

            if (kernal.len() == 8192 && basic.len() == 8192 && chargen.len() == 4096)
                state.currEng->setRoms((uint8_t*)kernal.begin(), (uint8_t*)basic.begin(), (uint8_t*)chargen.begin());
        }

        loaded_roms = true;
    }

    /* Try to get the tune */
    state.currTune->read((const uint8_t*)buf, bufSize);

    return state.currTune->getStatus();
}


bool xs_sidplayfp_getinfo(xs_tuneinfo_t &ti, const char *filename, const void *buf, int64_t bufSize)
{
    static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
    static int got_db = -1;
    static SidDatabase database;

    /* Check if the tune exists and is readable */
    SidTune myTune((const uint8_t*)buf, bufSize);

    if (!myTune.getStatus())
        return false;

    /* Get general tune information */
    const SidTuneInfo *myInfo = myTune.getInfo();

    /* Allocate tuneinfo structure and set information */
    ti.sidFilename = String (filename);

    ti.sidName = String (myInfo->infoString(0));
    ti.sidComposer = String (myInfo->infoString(1));
    ti.sidCopyright = String (myInfo->infoString(2));

    ti.nsubTunes = myInfo->songs();
    ti.startTune = myInfo->startSong();

    ti.loadAddr = myInfo->loadAddr();
    ti.initAddr = myInfo->initAddr();
    ti.playAddr = myInfo->playAddr();
    ti.dataFileLen = myInfo->dataFileLen();
    ti.sidFormat = String (myInfo->formatString());

    ti.sidModel = myInfo->sidModel1();

    /* Allocate space for subtune information */
    ti.subTunes.insert(0, ti.nsubTunes);

    /* Fill in sub-tune information */
    for (int i = 0; i < ti.nsubTunes; i++)
    {
        ti.subTunes[i].tuneLength = -1;
        ti.subTunes[i].tuneSpeed = -1;
    }

    pthread_mutex_lock(&db_mutex);

    if (got_db == -1)
        got_db = database.open(SIDDATADIR "sidplayfp/Songlengths.txt");

    if (got_db)
    {
        for (int i = 0; i < ti.nsubTunes; i++)
        {
            myTune.selectSong(i + 1);
            ti.subTunes[i].tuneLength = database.length(myTune);
        }
    }

    pthread_mutex_unlock(&db_mutex);

    return true;
}

bool xs_sidplayfp_updateinfo(xs_tuneinfo_t &ti, int subtune)
{
    /* Get currently playing tune information */
    const SidTuneInfo *myInfo = state.currTune->getInfo();

    /* NOTICE! Here we assume that libSIDPlay[12] headers define
     * SIDTUNE_SIDMODEL_* similarly to our enums in xs_config.h ...
     */
    ti.sidModel = myInfo->sidModel1();

    if ((subtune > 0) && (subtune <= ti.nsubTunes)) {
        int tmpSpeed = -1;

        switch (myInfo->clockSpeed()) {
        case SidTuneInfo::CLOCK_PAL:
            tmpSpeed = XS_CLOCK_PAL;
            break;
        case SidTuneInfo::CLOCK_NTSC:
            tmpSpeed = XS_CLOCK_NTSC;
            break;
        case SidTuneInfo::CLOCK_ANY:
            tmpSpeed = XS_CLOCK_ANY;
            break;
        case SidTuneInfo::CLOCK_UNKNOWN:
            switch (myInfo->songSpeed()) {
            case SidTuneInfo::SPEED_VBI:
                tmpSpeed = XS_CLOCK_VBI;
                break;
            case SidTuneInfo::SPEED_CIA_1A:
                tmpSpeed = XS_CLOCK_CIA;
                break;
            default:
                tmpSpeed = myInfo->songSpeed();
                break;
            }
            break;
        default:
            tmpSpeed = myInfo->clockSpeed();
            break;
        }

        ti.subTunes[subtune - 1].tuneSpeed = tmpSpeed;
    }

    return true;
}
