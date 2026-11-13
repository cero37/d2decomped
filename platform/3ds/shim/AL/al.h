/* SPDX-License-Identifier: GPL-3.0-or-later */
/* 3DS: the part of OpenAL that d2d calls, on the console's DSP (al_ndsp.cpp). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef char ALboolean;
typedef char ALchar;
typedef int ALint;
typedef unsigned int ALuint;
typedef int ALsizei;
typedef int ALenum;
typedef float ALfloat;
typedef void ALvoid;

#define AL_NONE 0
#define AL_FALSE 0
#define AL_TRUE 1
#define AL_NO_ERROR 0
#define AL_INVALID_NAME 0xA001
#define AL_INVALID_ENUM 0xA002
#define AL_INVALID_VALUE 0xA003
#define AL_INVALID_OPERATION 0xA004
#define AL_OUT_OF_MEMORY 0xA005
#define AL_SOURCE_RELATIVE 0x202
#define AL_POSITION 0x1004
#define AL_LOOPING 0x1007
#define AL_BUFFER 0x1009
#define AL_GAIN 0x100A
#define AL_SOURCE_STATE 0x1010
#define AL_INITIAL 0x1011
#define AL_PLAYING 0x1012
#define AL_PAUSED 0x1013
#define AL_STOPPED 0x1014
#define AL_BUFFERS_QUEUED 0x1015
#define AL_BUFFERS_PROCESSED 0x1016
#define AL_SAMPLE_OFFSET 0x1025
#define AL_FORMAT_MONO8 0x1100
#define AL_FORMAT_MONO16 0x1101
#define AL_FORMAT_STEREO8 0x1102
#define AL_FORMAT_STEREO16 0x1103
#define AL_VENDOR 0xB001
#define AL_VERSION 0xB002
#define AL_RENDERER 0xB003

ALenum alGetError(void);
const ALchar* alGetString(ALenum param);
ALboolean alIsExtensionPresent(const ALchar* name);
void alGenBuffers(ALsizei n, ALuint* buffers);
void alDeleteBuffers(ALsizei n, const ALuint* buffers);
void alBufferData(ALuint buffer, ALenum format, const ALvoid* data, ALsizei size, ALsizei freq);
void alGenSources(ALsizei n, ALuint* sources);
void alDeleteSources(ALsizei n, const ALuint* sources);
void alSourcei(ALuint source, ALenum param, ALint value);
void alSourcef(ALuint source, ALenum param, ALfloat value);
void alSource3f(ALuint source, ALenum param, ALfloat x, ALfloat y, ALfloat z);
void alGetSourcei(ALuint source, ALenum param, ALint* value);
void alGetSourcef(ALuint source, ALenum param, ALfloat* value);
void alSourcePlay(ALuint source);
void alSourcePlayv(ALsizei n, const ALuint* sources);
void alSourcePause(ALuint source);
void alSourcePausev(ALsizei n, const ALuint* sources);
void alSourceStop(ALuint source);
void alSourceQueueBuffers(ALuint source, ALsizei n, const ALuint* buffers);
void alSourceUnqueueBuffers(ALuint source, ALsizei n, ALuint* buffers);
#ifdef __cplusplus
}
#endif
