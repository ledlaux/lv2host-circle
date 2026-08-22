#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

#define ORGAN_URI "http://www.joebutton.co.uk/software/pitracker/plugins/organ"

#define NUM_VOICES 6
#define VOICE_CLAMPER  ((float)1.0 / NUM_VOICES)

#define OUTPUT_LEFT 1
#define OUTPUT_RIGHT 2
#define MIDI_IN 3

#ifndef M_PI
#define M_PI 3.14159265
#endif

#define FIFTH_MULTIPLIER 1.49830707688f

enum voice_state { on, released, off };

typedef struct {
    enum voice_state state;
    uint32_t note_no;
    float freq;
    float env;
    uint32_t time;
    uint32_t released_time;
} voice;

typedef struct {
    double sample_rate;
    LV2_Atom_Sequence* midi_in;
    float *output_left;
    float *output_right;
    LV2_URID midi_Event;
    voice voices[NUM_VOICES];
} Plugin;

static float noteno2freq(uint32_t note_no) {
    float multipliers[12] = { 1.0f, 1.05946309436f, 1.12246204831f, 1.189207115f, 1.25992104989f,
                              1.33483985417f, 1.41421356237f, 1.49830707688f, 1.58740105197f,
                              1.68179283051f, 1.78179743628f, 1.88774862536f };
    uint32_t octave = note_no / 12;
    float multiplier = multipliers[note_no - 12 * octave];
    float freq = 440.0f / 32.0f;
    while (octave-- > 0) freq *= 2.0f;
    return (freq * multiplier) / 8.0f;
}

static void note_on(Plugin *plugin, uint32_t note_no) {
    uint32_t best_voice = 0;
    uint32_t best_voice_age = 0;
    for (uint32_t i = 0; i < NUM_VOICES; i++) {
        if (plugin->voices[i].state == off) {
            best_voice = i;
            break;
        }
        if (plugin->voices[i].time > best_voice_age) {
            best_voice_age = plugin->voices[i].time;
            best_voice = i;
        }
    }
    plugin->voices[best_voice].state = on;
    plugin->voices[best_voice].time = 0;
    plugin->voices[best_voice].note_no = note_no;
    plugin->voices[best_voice].freq = noteno2freq(note_no);
}

static void note_off(Plugin *plugin, uint32_t note_no) {
    for (uint32_t i = 0; i < NUM_VOICES; i++) {
        if (plugin->voices[i].note_no == note_no) {
            plugin->voices[i].state = released;
            plugin->voices[i].released_time = 0;
        }
    }
}

static float envelope(voice *vp) {
    float env;
    uint32_t attack_time = 200;
    float attack = 0.9f;
    uint32_t decay_time = 5000;
    float sustain = 0.8f;
    uint32_t release_time = 1000;
    
    if (vp->state == on) {
        if (vp->time < attack_time) env = attack * ((float)vp->time / attack_time);
        else if (vp->time < (attack_time + decay_time)) env = attack - (attack - sustain) * (((float)vp->time - attack_time) / decay_time);
        else env = sustain;
        vp->env = env;
    } else if (vp->state == released) {
        if (vp->released_time > release_time) {
            vp->state = off;
            env = 0.0f;
        } else {
            env = vp->env - (vp->env * ((float)vp->released_time / release_time));
        }
    } else {
        env = 0.0f; 
    }
    return env;
}

static inline float waveform(voice v, double sample_rate) {
    float r = sinf((M_PI * 2.0f * v.freq * 2.0f * v.time) / sample_rate);
    r += 0.05f * sinf((M_PI * 2.0f * FIFTH_MULTIPLIER * v.freq * 2.0f * v.time) / sample_rate);
    r += 0.3f * sinf((M_PI * 2.0f * 4.0f * v.freq * 2.0f * v.time) / sample_rate);
    return r / 2.0f;
}

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
                               double s_rate, const char *path, const LV2_Feature * const* features) {
    
    Plugin *plugin = (Plugin *)malloc(sizeof(Plugin));
    if (!plugin) {
        printf("Error: Failed to allocate memory for organ plugin!\n");
        return NULL;
    }

    // Force zeroing out the memory (calloc alternative)
    memset(plugin, 0, sizeof(Plugin));
    
    plugin->sample_rate = s_rate;
    for (uint32_t i = 0; i < NUM_VOICES; i++) {
        plugin->voices[i].state = off;
    }

    LV2_URID_Map *map = NULL;
    if (features) {
        for (int i = 0; features[i]; i++) {
            if (features[i]->URI && strcmp(features[i]->URI, LV2_URID__map) == 0) {
                map = (LV2_URID_Map*)features[i]->data;
                break;
            }
        }
    }

    if (map == NULL) {
        printf("Error: Host does not support URID map feature!\n");
    } else {
        plugin->midi_Event = map->map(map->handle, LV2_MIDI__MidiEvent);
    }

    return (LV2_Handle)plugin;
}

static void cleanup(LV2_Handle instance) {
    if (instance) free(instance);
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data) {
    Plugin *plugin = (Plugin *)instance;
    if (!plugin) return;

    switch (port) {
    case MIDI_IN:
        plugin->midi_in = (LV2_Atom_Sequence*)data;
        break;
    case OUTPUT_LEFT:
        plugin->output_left = (float*)data;
        break;
    case OUTPUT_RIGHT:
        plugin->output_right = (float*)data;
        break;
    }
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    Plugin *plugin = (Plugin *)instance;
    if (!plugin || !plugin->output_left || !plugin->output_right) return;

    if (plugin->midi_in && plugin->midi_Event != 0) {
        LV2_ATOM_SEQUENCE_FOREACH(plugin->midi_in, ev) {
            if (ev->body.type == plugin->midi_Event) {
                const uint8_t* msg = (const uint8_t*)LV2_ATOM_CONTENTS(LV2_Atom_Event, ev);
                uint8_t status = lv2_midi_message_type(msg);

                if (status == LV2_MIDI_MSG_NOTE_ON) {
                    if (msg[2] > 0) note_on(plugin, msg[1]);
                    else note_off(plugin, msg[1]);
                } else if (status == LV2_MIDI_MSG_NOTE_OFF) {
                    note_off(plugin, msg[1]);
                }
            }
        }
    }

    for (uint32_t i = 0; i < sample_count; i++) {
        float out = 0.0f;
        for (uint32_t v = 0; v < NUM_VOICES; v++) {
            if (plugin->voices[v].state == off) continue;
            out += VOICE_CLAMPER * envelope(&(plugin->voices[v])) * waveform(plugin->voices[v], plugin->sample_rate);
            
            plugin->voices[v].time++;
            if (plugin->voices[v].state == released) {
                plugin->voices[v].released_time++;
            }
        }
        plugin->output_left[i] = out;
        plugin->output_right[i] = out;
    }
}

// ---------------------------------------------------------
// BARE-METAL SAFE DESCRIPTOR REGISTRATION
// ---------------------------------------------------------

static LV2_Descriptor synthDescriptor;
static int descriptor_initialized = 0;

#ifdef __cplusplus
extern "C" {
#endif

LV2_SYMBOL_EXPORT
const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    if (index != 0) return NULL;

    // Build the struct at runtime into a static variable.
    // This bypasses .rodata missing initialization bugs and avoids malloc.
    if (!descriptor_initialized) {
        synthDescriptor.URI = ORGAN_URI;
        synthDescriptor.instantiate = instantiate;
        synthDescriptor.connect_port = connect_port;
        synthDescriptor.activate = NULL;
        synthDescriptor.run = run;
        synthDescriptor.deactivate = NULL;
        synthDescriptor.cleanup = cleanup;
        synthDescriptor.extension_data = NULL;
        descriptor_initialized = 1;
    }

    return &synthDescriptor;
}

#ifdef __cplusplus
}
#endif