// Band: play songs with up to 4 instruments at the same time on a micro:bit V2.
//
// A small synthesizer feeds its own channel of the micro:bit's sound mixer,
// so the band plays alongside normal tones, sound effects and melodies.
// Each part (melody, harmony, bass, drums) has its own list of notes and loops by itself.

#ifndef BAND_HOST
#include "pxt.h"
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BAND_VOICES 4
// 44100 = the mixer's own rate. (At other rates the mixer reads 16-bit samples from the
// wrong place, which made the notes sound rusty and buzzy.)
#define BAND_RATE 44100
#ifndef BAND_FILTER
#define BAND_FILTER 90        // low-pass filter strength (bigger = brighter)
#endif
#ifndef BAND_GAIN
#define BAND_GAIN 8           // loudness boost in quarters (8 = x2)
#endif
#define BAND_BUF 256          // samples made each time the mixer asks for more

// Instruments
#define INST_SQUARE   0       // beep: classic 8-bit square wave
#define INST_TRIANGLE 1       // flute: soft
#define INST_SAW      2       // brass: buzzy
#define INST_SINE     3       // whistle: very smooth
#define INST_PULSE    4       // chip: thin 8-bit lead
#define INST_PLUCK    5       // guitar: fades after each note
#define INST_BELL     6       // bell: smooth and fades slowly
#define INST_BASS     7       // bass: round and fades a little
#define INST_COUNT    8

// Drum "notes" (in place of a frequency)
#define DRUM_KICK  1
#define DRUM_SNARE 2
#define DRUM_HAT   3

namespace band_core {

struct Voice {
    uint16_t *ev;       // pairs: frequency (0 rest, 1-3 drums), length in steps
    int len;            // number of events
    int pos;            // next event
    int32_t left;       // samples left in this event
    int32_t gate;       // samples left before the note is let go
    uint32_t phase;
    uint32_t inc;
    uint32_t incMin;
    uint32_t env;       // 0..65535
    int kind;           // 0 silent, 1 tone, 2 kick, 3 snare, 4 hat
    int inst;
    int amp;            // 0..255
    int32_t attack;     // samples into the note (for a soft start)
};

static Voice voices[BAND_VOICES];
static int32_t stepSamples = BAND_RATE * 150 / 1000;
static uint32_t noise = 0x12345678;
static int32_t lastNoise = 0;
static int32_t smooth = 0;
static int32_t gain = BAND_GAIN;   // in quarters: 8 = x2, more = louder (boost)      // the whole band goes through a gentle filter to take off the harsh edge
static int16_t sineTab[64];
static bool ready = false;

static void init() {
    if (ready) return;
    ready = true;
    for (int i = 0; i < 64; i++)
        sineTab[i] = (int16_t)(32767.0f * sinf(i * 6.2831853f / 64.0f));
    for (int v = 0; v < BAND_VOICES; v++) {
        memset(&voices[v], 0, sizeof(Voice));
    }
    voices[0].inst = INST_TRIANGLE; voices[0].amp = 200;   // melody
    voices[1].inst = INST_SINE;     voices[1].amp = 110;   // harmony
    voices[2].inst = INST_BASS;     voices[2].amp = 190;   // bass
    voices[3].inst = INST_SQUARE;   voices[3].amp = 150;   // drums
}

static uint32_t hz(uint32_t f) {
    return (uint32_t)(((uint64_t)f << 32) / BAND_RATE);
}

static inline int32_t nextNoise() {
    noise ^= noise << 13;
    noise ^= noise >> 17;
    noise ^= noise << 5;
    return (int32_t)(int16_t)(noise >> 8);
}

static inline int32_t tri(uint32_t phase) {
    int32_t t = (int32_t)(phase >> 16);          // 0..65535
    if (t < 32768) return t * 2 - 32768;
    return 32767 - (t - 32768) * 2;
}

static inline int32_t wave(int inst, uint32_t phase) {
    switch (inst) {
        case INST_TRIANGLE:
            return tri(phase);
        case INST_SAW:
            return (int32_t)(phase >> 16) - 32768;
        case INST_BASS:
            return tri(phase);
        case INST_SINE:
        case INST_BELL:
            return sineTab[phase >> 26];
        case INST_PULSE:
            return phase < 0x40000000u ? 32767 : -32768;
        default:
            return phase < 0x80000000u ? 32767 : -32768;
    }
}

// how fast each instrument fades (bigger = slower), 0 = no fade
static inline int fadeShift(int inst) {
    switch (inst) {
        case INST_PLUCK: return 14;
        case INST_BELL: return 15;
        case INST_BASS: return 15;
        default: return 0;
    }
}

static void startNext(Voice &v) {
    uint32_t f = v.ev[v.pos * 2];
    int32_t steps = v.ev[v.pos * 2 + 1];
    v.pos += 1;
    if (v.pos >= v.len) v.pos = 0;
    int32_t total = steps * stepSamples;
    if (total <= 0) total = 1;
    v.left = total;
    v.env = 65535;
    if (f == 0) {
        v.kind = 0;
    } else if (f == DRUM_KICK) {
        v.kind = 2;
        v.phase = 0;
        v.inc = hz(330);       // starts higher than a real kick so the small speaker can play it
        v.incMin = hz(80);
    } else if (f == DRUM_SNARE) {
        v.kind = 3;
        v.inc = hz(190);
    } else if (f == DRUM_HAT) {
        v.kind = 4;
    } else {
        v.kind = 1;
        v.inc = hz(f);
        int32_t gap = total / 6;
        if (gap > BAND_RATE / 40) gap = BAND_RATE / 40;
        v.gate = total - gap;
        v.attack = 0;
    }
}

// Makes n samples (signed, about -511..511) of the whole band.
void render(int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        int32_t mix = 0;
        for (int k = 0; k < BAND_VOICES; k++) {
            Voice &v = voices[k];
            if (v.ev == NULL || v.len == 0) continue;
            if (v.left <= 0) startNext(v);
            v.left--;
            if (v.kind == 0) continue;
            int32_t s;
            int32_t a = v.amp;
            if (v.kind == 1) {
                s = wave(v.inst, v.phase);
                v.phase += v.inc;
                int fs = fadeShift(v.inst);
                if (fs) {
                    a = (a * (int32_t)v.env) >> 16;
                    v.env -= v.env >> fs;
                }
                if (v.gate <= 0) {
                    v.kind = 0;
                    continue;
                }
                if (v.gate < 256) a = (a * v.gate) >> 8;
                v.gate--;
                if (v.attack < 256) {
                    a = (a * v.attack) >> 8;     // about 6 ms fade in: no clicks
                    v.attack++;
                }
            } else if (v.kind == 2) {
                // kick: a punchy thump that drops in pitch, with a little click at the start
                s = tri(v.phase) * 2;
                if (s > 32767) s = 32767;
                if (s < -32767) s = -32767;
                if (v.env > 60000) s = (s + nextNoise()) / 2;
                v.phase += v.inc;
                if (v.inc > v.incMin) v.inc -= v.inc >> 10;
                a = (a * 3 / 2 * (int32_t)v.env) >> 16;
                v.env -= v.env >> 12;
            } else if (v.kind == 3) {
                // snare: noise with a little tone
                s = (nextNoise() + tri(v.phase)) / 2;
                v.phase += v.inc;
                a = (a * (int32_t)v.env) >> 16;
                v.env -= v.env >> 12;
            } else {
                // hi-hat: short bright noise
                int32_t nz = nextNoise();
                s = (nz - lastNoise) / 2;
                lastNoise = nz;
                a = (a * 2 / 3 * (int32_t)v.env) >> 16;
                v.env -= v.env >> 10;
            }
            if (v.env < 64) v.kind = 0;
            mix += (s * a) >> 15;
        }
        // gentle low-pass filter (about 2 kHz): rounds off the buzzy, metallic edges
        smooth += ((mix - smooth) * BAND_FILTER) >> 8;
        // make it louder (the speaker is small), squeezing the loudest bits softly instead of clipping
        mix = smooth * gain / 4;
        if (mix > 320) mix = 320 + (mix - 320) / 4;
        if (mix < -320) mix = -320 + (mix + 320) / 4;
        if (mix > 511) mix = 511;
        if (mix < -511) mix = -511;
        out[i] = (int16_t)mix;
    }
}

// Replaces a part's notes. Returns the old list (to free).
uint16_t *setEvents(int k, uint16_t *ev, int len) {
    Voice &v = voices[k];
    uint16_t *old = v.ev;
    v.ev = ev;
    v.len = len;
    v.pos = 0;
    v.left = 0;
    v.kind = 0;
    return old;
}

void restartAll() {
    for (int k = 0; k < BAND_VOICES; k++) {
        voices[k].pos = 0;
        voices[k].left = 0;
        voices[k].kind = 0;
    }
}

void setStepMs(int ms) {
    if (ms < 20) ms = 20;
    if (ms > 2000) ms = 2000;
    stepSamples = ms * BAND_RATE / 1000;
}

void setInstrument(int k, int inst) {
    if (inst < 0 || inst >= INST_COUNT) inst = INST_SQUARE;
    voices[k].inst = inst;
}

// Extra loudness: 0 = normal, 1-3 = louder and louder (the loudest parts get squeezed softly)
void setBoost(int level) {
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    static const int32_t g[4] = {BAND_GAIN, BAND_GAIN * 3 / 2, BAND_GAIN * 2, BAND_GAIN * 3};
    gain = g[level];
}

void setAmp(int k, int amp) {
    if (amp < 0) amp = 0;
    if (amp > 255) amp = 255;
    voices[k].amp = amp;
}

} // namespace band_core

#ifndef BAND_HOST

using namespace pxt;

namespace band {

#if MICROBIT_CODAL

class BandSource : public DataSource {
  public:
    DataSink *sink;
    volatile bool active;

    BandSource() : sink(NULL), active(false) {}

    virtual ManagedBuffer pull() {
        if (!active)
            return ManagedBuffer();      // nothing: the mixer stops asking until we start again
        ManagedBuffer b(BAND_BUF * 2);
        band_core::render((int16_t *)b.getBytes(), BAND_BUF);
        if (sink)
            sink->pullRequest();         // another buffer is always ready
        return b;
    }
    virtual void connect(DataSink &s) { sink = &s; }
    virtual bool isConnected() { return sink != NULL; }
    virtual void disconnect() { sink = NULL; }
    virtual int getFormat() { return DATASTREAM_FORMAT_16BIT_SIGNED; }
    virtual int setFormat(int format) { return DEVICE_NOT_SUPPORTED; }
    virtual float getSampleRate() { return BAND_RATE; }
};

static BandSource *source = NULL;

static void ensure() {
    if (source) return;
    band_core::init();
    source = new BandSource();
    uBit.audio.enable();
    uBit.audio.mixer.addChannel(*source, BAND_RATE, 1023);
}

static int part(int p) {
    if (p < 0) return 0;
    if (p >= BAND_VOICES) return BAND_VOICES - 1;
    return p;
}

//%
void setTrack(int voice, Buffer data) {
    ensure();
    int len = data ? data->length / 4 : 0;
    uint16_t *ev = NULL;
    if (len > 0) {
        ev = (uint16_t *)malloc(len * 4);
        memcpy(ev, data->data, len * 4);
    }
    target_disable_irq();
    uint16_t *old = band_core::setEvents(part(voice), ev, len);
    target_enable_irq();
    if (old) free(old);
}

//%
void setInstrument(int voice, int instrument) {
    ensure();
    band_core::setInstrument(part(voice), instrument);
}

//%
void setPartVolume(int voice, int volume) {
    ensure();
    band_core::setAmp(part(voice), volume * 255 / 100);
}

//%
void setBoost(int level) {
    ensure();
    band_core::setBoost(level);
}

//%
void setStepMs(int ms) {
    ensure();
    band_core::setStepMs(ms);
}

//%
void start() {
    ensure();
    target_disable_irq();
    band_core::restartAll();
    bool was = source->active;
    source->active = true;
    target_enable_irq();
    if (!was && source->sink)
        source->sink->pullRequest();
}

//%
void stop() {
    if (source) source->active = false;
}

//%
bool isPlaying() {
    return source != NULL && source->active;
}

#else
// micro:bit V1 has no sound mixer: the band does nothing.
//%
void setTrack(int voice, Buffer data) {}
//%
void setInstrument(int voice, int instrument) {}
//%
void setPartVolume(int voice, int volume) {}
//%
void setStepMs(int ms) {}
//%
void setBoost(int level) {}
//%
void start() {}
//%
void stop() {}
//%
bool isPlaying() { return false; }
#endif

} // namespace band

#endif
