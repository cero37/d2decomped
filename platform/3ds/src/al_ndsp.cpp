// SPDX-License-Identifier: GPL-3.0-or-later
// 3DS: the part of OpenAL that d2d calls, played by the console's DSP
// (ndsp, 24 voices mixed in hardware) so mixing costs the CPU nothing.
// One AL source = one DSP voice while it plays. Called from one thread.
#include <AL/al.h>
#include <AL/alc.h>

#include <3ds.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <unordered_map>

namespace {

constexpr int kVoices = 24;

struct Buffer {
    void* data = nullptr;   // linear memory, as the DSP needs
    std::uint32_t frames = 0;
    int freq = 22050;
    bool stereo = false;
    bool pcm16 = true;
    ~Buffer() { if (data) linearFree(data); }
};

struct Source {
    int voice = -1;
    ALuint buffer = 0;                 // AL_BUFFER (static)
    std::deque<ALuint> queued;         // alSourceQueueBuffers
    std::deque<std::unique_ptr<ndspWaveBuf>> waves;   // what the voice was given, in order
    std::size_t processed = 0;         // queued buffers already handed back as processed
    float gain = 1.f;
    float pan = 0.f;
    bool positioned = false;
    bool looping = false;
    ALenum state = AL_INITIAL;
    std::uint32_t offset = 0;          // frames skipped at the start (AL_SAMPLE_OFFSET)
    bool lead_in = false;              // waves[0] is the part after the offset, waves[1] the loop
};

bool g_open = false;
ALenum g_error = AL_NO_ERROR;
ALuint g_next_id = 1;
std::unordered_map<ALuint, std::unique_ptr<Buffer>> g_buffers;
std::unordered_map<ALuint, std::unique_ptr<Source>> g_sources;
std::array<ALuint, kVoices> g_voice_owner{};

Source* source(ALuint id) {
    const auto found = g_sources.find(id);
    if (found == g_sources.end()) { g_error = AL_INVALID_NAME; return nullptr; }
    return found->second.get();
}
Buffer* buffer(ALuint id) {
    const auto found = g_buffers.find(id);
    return found == g_buffers.end() ? nullptr : found->second.get();
}

void release_voice(Source& src) {
    if (src.voice < 0) return;
    ndspChnWaveBufClear(src.voice);
    g_voice_owner[std::size_t(src.voice)] = 0;
    src.voice = -1;
    src.waves.clear();
}

// A voice whose every wave has played out is free again.
void settle(Source& src) {
    if (src.voice < 0 || src.state != AL_PLAYING) return;
    if (src.looping && src.queued.empty()) return;
    for (const auto& wave : src.waves)
        if (wave->status != NDSP_WBUF_DONE) return;
    if (!src.queued.empty()) {   // a stream that ran dry: keep the voice, AL calls it stopped
        src.state = AL_STOPPED;
        return;
    }
    release_voice(src);
    src.state = AL_STOPPED;
    src.offset = 0;
}

int take_voice(ALuint id) {
    for (int pass = 0; pass < 2; ++pass) {
        for (int v = 0; v < kVoices; ++v)
            if (!g_voice_owner[std::size_t(v)]) { g_voice_owner[std::size_t(v)] = id; return v; }
        for (auto& [other_id, other] : g_sources) settle(*other);
    }
    return -1;
}

void apply_mix(const Source& src) {
    if (src.voice < 0) return;
    float mix[12] = {};
    const float left = src.positioned ? std::min(1.f, 1.f - src.pan) : 1.f;
    const float right = src.positioned ? std::min(1.f, 1.f + src.pan) : 1.f;
    mix[0] = src.gain * left;
    mix[1] = src.gain * right;
    ndspChnSetMix(src.voice, mix);
}

std::unique_ptr<ndspWaveBuf> wave_of(const Buffer& buf, std::uint32_t from, bool loop) {
    auto wave = std::make_unique<ndspWaveBuf>();
    std::memset(wave.get(), 0, sizeof *wave);
    const std::uint32_t frame_bytes = (buf.stereo ? 2u : 1u) * (buf.pcm16 ? 2u : 1u);
    wave->data_vaddr = static_cast<const std::uint8_t*>(buf.data) + std::size_t(from) * frame_bytes;
    wave->nsamples = buf.frames - from;
    wave->looping = loop;
    return wave;
}

void setup_voice(const Source& src, const Buffer& buf) {
    ndspChnReset(src.voice);
    ndspChnSetInterp(src.voice, NDSP_INTERP_LINEAR);
    ndspChnSetRate(src.voice, float(buf.freq));
    ndspChnSetFormat(src.voice, buf.stereo ? (buf.pcm16 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_STEREO_PCM8)
                                           : (buf.pcm16 ? NDSP_FORMAT_MONO_PCM16 : NDSP_FORMAT_MONO_PCM8));
    apply_mix(src);
}

void play(ALuint id) {
    Source* src = source(id);
    if (!src || !g_open) return;
    settle(*src);
    if (src->state == AL_PAUSED && src->voice >= 0) {
        ndspChnSetPaused(src->voice, false);
        src->state = AL_PLAYING;
        return;
    }
    if (!src->queued.empty()) {   // a stream: its waves are already on the voice
        if (src->voice >= 0) src->state = AL_PLAYING;
        return;
    }
    const Buffer* buf = buffer(src->buffer);
    if (!buf || !buf->frames) { g_error = AL_INVALID_OPERATION; return; }
    release_voice(*src);
    src->voice = take_voice(id);
    if (src->voice < 0) { g_error = AL_OUT_OF_MEMORY; src->state = AL_STOPPED; return; }
    setup_voice(*src, *buf);
    const std::uint32_t from = src->offset < buf->frames ? src->offset : 0;
    src->lead_in = from > 0 && src->looping;
    if (src->lead_in) {
        src->waves.push_back(wave_of(*buf, from, false));
        src->waves.push_back(wave_of(*buf, 0, true));
    } else {
        src->waves.push_back(wave_of(*buf, from, src->looping));
    }
    src->offset = from;
    for (auto& wave : src->waves) ndspChnWaveBufAdd(src->voice, wave.get());
    src->state = AL_PLAYING;
}

void pause(ALuint id) {
    Source* src = source(id);
    if (!src) return;
    settle(*src);
    if (src->state != AL_PLAYING || src->voice < 0) return;
    ndspChnSetPaused(src->voice, true);
    src->state = AL_PAUSED;
}

}   // namespace

extern "C" {

ALCdevice* alcOpenDevice(const ALCchar*) {
    if (!g_open) {
        if (R_FAILED(ndspInit())) return nullptr;   // no DSP firmware dumped (sdmc:/3ds/dspfirm.cdc)
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        g_open = true;
    }
    return reinterpret_cast<ALCdevice*>(&g_open);
}
ALCboolean alcCloseDevice(ALCdevice*) {
    if (g_open) {
        g_sources.clear();
        g_buffers.clear();
        ndspExit();
        g_open = false;
    }
    return AL_TRUE;
}
ALCcontext* alcCreateContext(ALCdevice* device, const ALCint*) { return reinterpret_cast<ALCcontext*>(device); }
void alcDestroyContext(ALCcontext*) {}
ALCboolean alcMakeContextCurrent(ALCcontext*) { return AL_TRUE; }
const ALCchar* alcGetString(ALCdevice*, ALCenum) { return "Nintendo 3DS DSP"; }

ALenum alGetError(void) { const ALenum error = g_error; g_error = AL_NO_ERROR; return error; }
const ALchar* alGetString(ALenum param) {
    switch (param) {
    case AL_VENDOR: return "d2d";
    case AL_VERSION: return "1.1 (subset)";
    case AL_RENDERER: return "ndsp";
    default: return "";
    }
}
ALboolean alIsExtensionPresent(const ALchar* name) {
    return std::strcmp(name, "AL_SOFT_direct_channels") == 0 ? AL_TRUE : AL_FALSE;   // stereo is never spatialised here
}

void alGenBuffers(ALsizei n, ALuint* buffers) {
    for (ALsizei i = 0; i < n; ++i) {
        buffers[i] = g_next_id++;
        g_buffers.emplace(buffers[i], std::make_unique<Buffer>());
    }
}
void alDeleteBuffers(ALsizei n, const ALuint* buffers) {
    for (ALsizei i = 0; i < n; ++i) g_buffers.erase(buffers[i]);
}
void alBufferData(ALuint id, ALenum format, const ALvoid* data, ALsizei size, ALsizei freq) {
    Buffer* buf = buffer(id);
    if (!buf) { g_error = AL_INVALID_NAME; return; }
    if (buf->data) { linearFree(buf->data); buf->data = nullptr; buf->frames = 0; }
    buf->stereo = format == AL_FORMAT_STEREO8 || format == AL_FORMAT_STEREO16;
    buf->pcm16 = format == AL_FORMAT_MONO16 || format == AL_FORMAT_STEREO16;
    buf->freq = freq;
    if (size <= 0) return;
    buf->data = linearAlloc(std::size_t(size));
    if (!buf->data) { g_error = AL_OUT_OF_MEMORY; return; }
    if (buf->pcm16) {
        std::memcpy(buf->data, data, std::size_t(size));
    } else {   // AL's 8-bit is unsigned, the DSP's signed
        const auto* in = static_cast<const std::uint8_t*>(data);
        auto* out = static_cast<std::uint8_t*>(buf->data);
        for (ALsizei i = 0; i < size; ++i) out[i] = std::uint8_t(in[i] ^ 0x80);
    }
    DSP_FlushDataCache(buf->data, std::uint32_t(size));
    buf->frames = std::uint32_t(size) / ((buf->stereo ? 2u : 1u) * (buf->pcm16 ? 2u : 1u));
}

void alGenSources(ALsizei n, ALuint* sources) {
    for (ALsizei i = 0; i < n; ++i) {
        sources[i] = g_next_id++;
        g_sources.emplace(sources[i], std::make_unique<Source>());
    }
}
void alDeleteSources(ALsizei n, const ALuint* sources) {
    for (ALsizei i = 0; i < n; ++i) {
        const auto found = g_sources.find(sources[i]);
        if (found == g_sources.end()) continue;
        release_voice(*found->second);
        g_sources.erase(found);
    }
}

void alSourcei(ALuint id, ALenum param, ALint value) {
    Source* src = source(id);
    if (!src) return;
    switch (param) {
    case AL_BUFFER:
        release_voice(*src);
        src->buffer = ALuint(value);
        src->queued.clear();
        src->processed = 0;
        src->state = AL_INITIAL;
        src->offset = 0;
        break;
    case AL_LOOPING: src->looping = value != 0; break;
    case AL_SAMPLE_OFFSET:
        src->offset = std::uint32_t(std::max(0, value));
        if (src->voice >= 0 && src->queued.empty()) {   // a seek while it plays: start over from there
            const ALenum was = src->state;
            release_voice(*src);
            src->state = AL_INITIAL;
            if (was == AL_PLAYING || was == AL_PAUSED) play(id);
            if (was == AL_PAUSED) pause(id);
        }
        break;
    default: break;   // AL_SOURCE_RELATIVE, AL_DIRECT_CHANNELS_SOFT: how this always plays
    }
}
void alSourcef(ALuint id, ALenum param, ALfloat value) {
    Source* src = source(id);
    if (!src) return;
    if (param == AL_GAIN) { src->gain = value; apply_mix(*src); }
}
void alSource3f(ALuint id, ALenum param, ALfloat x, ALfloat, ALfloat) {
    Source* src = source(id);
    if (!src) return;
    if (param == AL_POSITION) { src->pan = std::clamp(x, -1.f, 1.f); src->positioned = true; apply_mix(*src); }
}
void alGetSourcei(ALuint id, ALenum param, ALint* value) {
    Source* src = source(id);
    if (!src) { *value = 0; return; }
    settle(*src);
    switch (param) {
    case AL_SOURCE_STATE: *value = src->state; break;
    case AL_BUFFER: *value = ALint(src->buffer); break;
    case AL_LOOPING: *value = src->looping; break;
    case AL_BUFFERS_QUEUED: *value = ALint(src->queued.size()); break;
    case AL_BUFFERS_PROCESSED: {
        ALint done = 0;
        for (const auto& wave : src->waves) {
            if (wave->status != NDSP_WBUF_DONE) break;
            ++done;
        }
        *value = done;
        break;
    }
    case AL_SAMPLE_OFFSET:
        if (src->voice < 0) { *value = ALint(src->offset); break; }
        if (src->lead_in && src->waves.front()->status == NDSP_WBUF_DONE) *value = ALint(ndspChnGetSamplePos(src->voice));
        else *value = ALint(src->offset + ndspChnGetSamplePos(src->voice));
        break;
    default: *value = 0; break;
    }
}
void alGetSourcef(ALuint id, ALenum param, ALfloat* value) {
    const Source* src = source(id);
    *value = src && param == AL_GAIN ? src->gain : 0.f;
}

void alSourcePlay(ALuint id) { play(id); }
void alSourcePlayv(ALsizei n, const ALuint* sources) { for (ALsizei i = 0; i < n; ++i) play(sources[i]); }
void alSourcePause(ALuint id) { pause(id); }
void alSourcePausev(ALsizei n, const ALuint* sources) { for (ALsizei i = 0; i < n; ++i) pause(sources[i]); }
void alSourceStop(ALuint id) {
    Source* src = source(id);
    if (!src) return;
    release_voice(*src);
    src->queued.clear();
    src->processed = 0;
    src->state = AL_STOPPED;
    src->offset = 0;
}

void alSourceQueueBuffers(ALuint id, ALsizei n, const ALuint* buffers) {
    Source* src = source(id);
    if (!src || !g_open) return;
    for (ALsizei i = 0; i < n; ++i) {
        const Buffer* buf = buffer(buffers[i]);
        if (!buf || !buf->frames) { g_error = AL_INVALID_NAME; continue; }
        if (src->voice < 0) {
            src->voice = take_voice(id);
            if (src->voice < 0) { g_error = AL_OUT_OF_MEMORY; return; }
            setup_voice(*src, *buf);
            ndspChnSetPaused(src->voice, true);   // until alSourcePlay
            src->state = AL_INITIAL;
        }
        src->queued.push_back(buffers[i]);
        src->waves.push_back(wave_of(*buf, 0, false));
        ndspChnWaveBufAdd(src->voice, src->waves.back().get());
    }
}
void alSourceUnqueueBuffers(ALuint id, ALsizei n, ALuint* buffers) {
    Source* src = source(id);
    if (!src) return;
    for (ALsizei i = 0; i < n; ++i) {
        if (src->queued.empty() || src->waves.empty() || src->waves.front()->status != NDSP_WBUF_DONE) {
            g_error = AL_INVALID_VALUE;
            return;
        }
        buffers[i] = src->queued.front();
        src->queued.pop_front();
        src->waves.pop_front();
    }
}

}   // extern "C"
