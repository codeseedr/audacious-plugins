/*
 *  Copyright (C) 2005 Ralf Ertzinger
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
#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#define _ENTER _DEBUG("enter")
#define _LEAVE _DEBUG("leave"); return
#define _MESSAGE(tag, string, ...) do { fprintf(stderr, "%s: ALSA: %s:%d (%s): " string "\n", \
    tag, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while(0)

#define _ERROR(...) _MESSAGE("ERROR", __VA_ARGS__)

#ifdef ALSA_DEBUG
#define _DEBUG(...) _MESSAGE("DEBUG",  __VA_ARGS__)
#else
#define _DEBUG(...) {}
#endif

#endif