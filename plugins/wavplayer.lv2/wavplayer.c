#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <math.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

#define NUM_VOICES 6
#define VOICE_CLAMPER  (float)1/NUM_VOICES

#define OUTPUT_LEFT 1
#define OUTPUT_RIGHT 2
#define MIDI_IN 3

// extern uint8_t _binary_sample_wav_start;

extern uint8_t _binary_plugins_wavplayer_lv2_sample_wav_start;  // Updated for circle

static LV2_Descriptor *synthDescriptor = NULL;

enum voice_state {on, off};

typedef struct  WAV_HEADER {
     uint8_t       RIFF[4];        /* RIFF Header      */ //Magic header
     uint32_t      ChunkSize;      /* RIFF Chunk Size  */
     uint8_t       WAVE[4];        /* WAVE Header      */
     uint8_t       fmt[4];         /* FMT header       */
     uint32_t      Subchunk1Size;  /* Size of the fmt chunk                                */
     uint16_t      AudioFormat;    /* Audio format 1=PCM,6=mulaw,7=alaw, 257=IBM Mu-Law, 258=IBM A-Law, 259=ADPCM */
     uint16_t      NumOfChan;      /* Number of channels 1=Mono 2=Sterio                   */
     uint32_t      SamplesPerSec;  /* Sampling Frequency in Hz                             */
     uint32_t      bytesPerSec;    /* bytes per second */
     uint16_t      blockAlign;     /* 2=16-bit mono, 4=16-bit stereo */
     uint16_t      bitsPerSample;  /* Number of bits per sample      */
     uint8_t       Subchunk2ID[4]; /* "data"  string   */
     uint32_t      Subchunk2Size;  /* Sampled data length    */
}wav_hdr;

typedef struct {
    enum voice_state state;
    uint32_t note_no;
    float freq;
    uint32_t time;
} voice;


static voice voices[NUM_VOICES];

typedef struct {
    double sample_rate;
    LV2_Atom_Sequence* midi_in;
    float *output_left;
    float *output_right;
    LV2_URID midi_Event;
    int16_t *audiodata;
    uint32_t audiodata_len;
    voice voices[NUM_VOICES]; // <--- Moved here
} Plugin;


static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
        double s_rate, const char *path, const LV2_Feature * const* features) {

    Plugin *plugin = (Plugin *)malloc(sizeof(Plugin));
    memset(plugin, 0, sizeof(Plugin)); // Clean slate
    
    plugin->sample_rate = s_rate;
    for (int i = 0; i < NUM_VOICES; i++) {
        plugin->voices[i].state = off;
    }
    
    LV2_URID_Map *map = NULL;
    if (features) {
        for (int i = 0; features[i]; i++) {
            if (features[i]->URI && strcmp(features[i]->URI, LV2_URID__map) == 0) {
                map = (LV2_URID_Map*)features[i]->data;
            }
        }
    }
    if (map == NULL) {
        printf("Error: Host does not support map feature\r\n");
    } else {
        plugin->midi_Event = map->map(map->handle, LV2_MIDI__MidiEvent);
    }

    wav_hdr *hdr = (wav_hdr*)(void*)&_binary_plugins_wavplayer_lv2_sample_wav_start;
    plugin->audiodata = (int16_t*)(hdr + 1);
    plugin->audiodata_len = hdr->Subchunk2Size / sizeof(int16_t);

    return (LV2_Handle)plugin;
}

static void cleanup(LV2_Handle instance) {
    free(instance);
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data) {
    Plugin *plugin = (Plugin *)instance;

    switch (port) {
    case MIDI_IN:
        plugin->midi_in = data;
        break;
    case OUTPUT_LEFT:
        plugin->output_left = data;
        break;
    case OUTPUT_RIGHT:
        plugin->output_right = data;
        break;
    default:
        break;
    }
}

static float noteno2freq(uint32_t note_no) {
    float multipliers[12] = { 1.0, 1.05946309436, 1.12246204831, 1.189207115, 1.25992104989,
                              1.33483985417, 1.41421356237, 1.49830707688, 1.58740105197,
                              1.68179283051, 1.78179743628, 1.88774862536 };
    float multiplier, freq;
    int octave = note_no/12;
    multiplier = multipliers[note_no - 12*octave];
    freq = 440.0 / 32.0;
    while (octave-- > 0) freq *= 2;
    return freq * multiplier / 8;
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
        if (plugin->voices[i].note_no == note_no && plugin->voices[i].state == on) {
            plugin->voices[i].state = off;
        }
    }
}

static float waveform(Plugin *plugin, voice *v) {
    float base_freq = 110.0f; 
    float time_shift = v->freq / base_freq;
    uint32_t index = (uint32_t)(time_shift * (float)v->time);
    
    // Strict bounds check: if we hit the end of the sample, kill the voice and return 0
    if (index >= plugin->audiodata_len) {
        v->state = off;
        return 0.0f;
    } 
    return (float)plugin->audiodata[index] / 32768.0f / 4.0f;
}


static void run(LV2_Handle instance, uint32_t sample_count) {
    Plugin *plugin = (Plugin *)instance;
    if (!plugin || !plugin->output_left || !plugin->output_right || !plugin->midi_in) return;

    LV2_ATOM_SEQUENCE_FOREACH(plugin->midi_in, ev) {
        if (ev->body.type == plugin->midi_Event) {
            const uint8_t* msg = (const uint8_t*)LV2_ATOM_CONTENTS(LV2_Atom_Event, ev);
            uint8_t status = lv2_midi_message_type(msg);

            if (status == LV2_MIDI_MSG_NOTE_ON) {
                if (msg[2] > 0) {
                    note_on(plugin, msg[1]); // Pass plugin pointer first
                } else {
                    note_off(plugin, msg[1]); // Pass plugin pointer first
                }
            } else if (status == LV2_MIDI_MSG_NOTE_OFF) {
                note_off(plugin, msg[1]); // Pass plugin pointer first
            }
        }
    }

    for (uint32_t i = 0; i < sample_count; i++) {
        float out = 0.0f;
        for (int v = 0; v < NUM_VOICES; v++) {
            if (plugin->voices[v].state == off) continue;
            
            // waveform only expects 2 arguments now (plugin, voice pointer)
            out += VOICE_CLAMPER * waveform(plugin, &plugin->voices[v]);
            
            plugin->voices[v].time++;
        }
        plugin->output_left[i] = out;
        plugin->output_right[i] = out;
    }
}


LV2_SYMBOL_EXPORT
const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    if (!synthDescriptor) {
        synthDescriptor = (LV2_Descriptor *)malloc(sizeof(LV2_Descriptor));

        synthDescriptor->URI = "http://www.joebutton.co.uk/software/pitracker/plugins/wavplayer";
        synthDescriptor->activate = NULL;
        synthDescriptor->cleanup = cleanup;
        synthDescriptor->connect_port = connect_port;
        synthDescriptor->deactivate = NULL;
        synthDescriptor->instantiate = instantiate;
        synthDescriptor->run = run;
        synthDescriptor->extension_data = NULL;
    }

    if (index == 0) return synthDescriptor;
    return NULL;
}

