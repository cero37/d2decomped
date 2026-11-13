/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "al.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;
typedef char ALCboolean;
typedef char ALCchar;
typedef int ALCint;
typedef int ALCenum;
#define ALC_DEVICE_SPECIFIER 0x1005
ALCdevice* alcOpenDevice(const ALCchar* name);
ALCboolean alcCloseDevice(ALCdevice* device);
ALCcontext* alcCreateContext(ALCdevice* device, const ALCint* attributes);
void alcDestroyContext(ALCcontext* context);
ALCboolean alcMakeContextCurrent(ALCcontext* context);
const ALCchar* alcGetString(ALCdevice* device, ALCenum param);
#ifdef __cplusplus
}
#endif
