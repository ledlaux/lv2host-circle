#include <string.h>
#include <stdlib.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>

#include "lv2.h"

LV2_Atom_Forge forge;

typedef struct urid_map_entry {
    char* uri;
    uint32_t id;
    struct urid_map_entry* next;
} urid_map_entry;

static urid_map_entry* urid_table = NULL;

static LV2_URID urid_map_func(LV2_URID_Map_Handle handle, const char* uri) {
    static LV2_URID id = 1;
    urid_map_entry* next;

    if (urid_table == NULL) {
        urid_table = (urid_map_entry*)malloc(sizeof(urid_map_entry));
        if (urid_table) {
            urid_table->uri = strdup("http://www.joebutton.co.uk/pitracker/urid-map-error");
            urid_table->id = 0;
            urid_table->next = NULL;
        }
    }

    urid_map_entry* cur = urid_table;

    while (cur) {
        if (cur->uri && !strcmp(cur->uri, uri)) {
            return cur->id;
        }
        if (cur->next == NULL) {
            next = (urid_map_entry*)malloc(sizeof(urid_map_entry));
            if (!next) return 0;
            next->uri = strdup(uri);
            next->id = id++;
            next->next = NULL;
            cur->next = next;
            return next->id;
        }
        cur = cur->next;
    }
    return 0;
}

static const char *urid_unmap_func(LV2_URID_Unmap_Handle handle, LV2_URID urid) {
    urid_map_entry* cur = urid_table;
    while (cur) {
        if (cur->id == urid) {
            return cur->uri;
        }
        cur = cur->next;
    }
    return NULL;
}

LV2_URID_Map lv2_urid_map = {NULL, urid_map_func};
static LV2_URID_Unmap lv2_urid_unmap = {NULL, urid_unmap_func};

static LV2_Feature map_feature   = { LV2_URID__map, &lv2_urid_map };
static LV2_Feature unmap_feature = { LV2_URID__unmap, &lv2_urid_unmap };

Lv2World *lv2_init(uint32_t sample_rate) {
    Lv2World *world = (Lv2World*)malloc(sizeof(Lv2World));
    if (!world) return NULL;
    
    world->sample_rate = sample_rate;
    world->num_plugins = 0;
    world->plugin_list = NULL;
    
    urid_table = NULL;
    world->lv2_features[0] = &map_feature;
    world->lv2_features[1] = &unmap_feature;
    world->lv2_features[2] = NULL;
    
    lv2_atom_forge_init(&forge, &lv2_urid_map);

    return world;
}

lv2_port *new_lv2_port(enum lv2_port_type type, uint32_t id) {
    lv2_port *port = (lv2_port*)malloc(sizeof(lv2_port));
    if (!port) return NULL;
    
    port->type = type;
    port->id = id;
    if (type == lv2_audio_port) {
        port->buffer = malloc(sizeof(float) * LV2_AUDIO_BUFFER_SIZE);
        port->buffer_sz = LV2_AUDIO_BUFFER_SIZE;
    } else if (type == lv2_atom_port) {
        port->buffer = malloc(sizeof(uint8_t) * LV2_ATOM_BUFFER_SIZE);
        port->buffer_sz = LV2_ATOM_BUFFER_SIZE;
    }
    return port;
}