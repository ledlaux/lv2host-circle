# Architecture description of lv2host-circle 

**A bare-metal LV2 plugin host for Raspberry Pi using the Circle C++ framework.**

This project is a [port](https://github.com/Joeboy/pixperiments/tree/master/pitracker) of **Joe Button's** 2013 Raspberry Pi bare-metal LV2 plugin host to the [Circle](https://github.com/rsta2/circle) C++ framework.

The project provides a minimal, low-latency LV2 hosting environment that runs directly on Raspberry Pi hardware without Linux, ALSA, JACK, Lilv, Suil, or filesystem-based plugin discovery.

It connects three main systems:

* **LV2 plugin execution**
* **MIDI input and LV2 Atom event processing**
* **I2S DMA audio output**

The goal is to provide a small and deterministic LV2 runtime suitable for embedded synthesizers, effects processors, MIDI instruments, and other dedicated Raspberry Pi audio applications.

---

## Architecture

```text
                       Raspberry Pi
                            │
              ┌─────────────┴─────────────┐
              │                           │
          GPIO MIDI                  USB MIDI
              │                           │
              └─────────────┬─────────────┘
                            │
                            ▼
                   MIDI Processing
                  ProcessMidiByte()
                            │
                            ▼
                  MidiPacketReceived()
                            │
                            ▼
                    LV2 Atom Forge
                            │
                            ▼
                  Atom Sequence Buffer
                            │
                            ▼
                    ┌──────────────┐
                    │  LV2 Plugin  │
                    │              │
                    │ instantiate │
                    │ connect_port│
                    │     run()   │
                    └──────┬───────┘
                           │
                    Float Audio Output
                      ┌────┴────┐
                      │         │
                    Left      Right
                      │         │
                      └────┬────┘
                           │
                           ▼
                       I2S DMA
                           │
                           ▼
                    Audio Hardware
```

---

## How the LV2 Engine Works

The LV2 engine is a lightweight embedded host runtime. It implements the minimum host functionality required by an LV2 plugin while avoiding the infrastructure normally required on a Linux desktop system.

The engine is responsible for:

1. Initializing the LV2 environment.
2. Mapping LV2 URIs to numeric URIDs.
3. Providing host features to the plugin.
4. Instantiating statically linked LV2 plugins.
5. Creating and connecting plugin ports.
6. Receiving MIDI from GPIO and USB.
7. Converting MIDI messages into LV2 Atom events.
8. Executing the plugin's `run()` function.
9. Converting plugin floating-point output to 24-bit audio.
10. Sending the resulting audio to I2S through Circle's DMA audio interface.

The plugin itself remains responsible for its DSP processing.


---

## Real-Time Audio Processing

The real-time audio path is implemented by `GetChunk()` in `Kernel.cpp`.

Circle's `CI2SSoundBaseDevice` invokes this callback when the audio DMA system requires another block of samples.

The processing flow is:

```text
I2S DMA requests audio
        │
        ▼
     GetChunk()
        │
        ▼
split request into LV2 blocks
        │
        ▼
finalize MIDI Atom Sequence
        │
        ▼
descriptor->run()
        │
        ▼
plugin generates float audio
        │
        ▼
interleave L/R samples
        │
        ▼
NEON float → 24-bit conversion
        │
        ▼
I2S DMA buffer
```

### Fixed Audio Blocks

The host splits larger hardware requests into blocks no larger than:

```text
LV2_AUDIO_BUFFER_SIZE
```

This keeps processing compatible with the statically allocated LV2 audio buffers.

The plugin therefore always receives a predictable amount of audio data.

### LV2 `run()`

Once the MIDI Atom Sequence for the current block is finalized, the engine invokes:

```cpp
descriptor->run(instance, block_size);
```

The plugin reads its connected input/event ports and writes its audio output directly into the host-provided buffers.

There is no additional audio-server layer between the plugin and the hardware.

---

## Audio Buffer Management

The host uses pre-allocated buffers rather than dynamically allocating audio memory during real-time processing.

Typical ports are:

```text
Port 1  → Left Audio Output
Port 2  → Right Audio Output
Port 3  → MIDI Atom Sequence
```

Audio ports use fixed arrays of floating-point samples:

```text
float[LV2_AUDIO_BUFFER_SIZE]
```

The MIDI port uses a fixed-size Atom buffer:

```text
LV2_ATOM_BUFFER_SIZE
```

This design avoids memory allocation in the real-time callback and makes memory usage deterministic.

---

## Float to 24-Bit Audio Conversion

LV2 audio ports normally use 32-bit floating-point samples.

The I2S output path uses signed 24-bit audio, so the host converts each sample approximately as:

```text
float sample
     │
     ▼
sample × 8388607.0
     │
     ▼
signed integer
     │
     ▼
24-bit I2S sample
```

`8388607.0` is:

```text
2^23 - 1
```

which corresponds to the maximum positive value of a signed 24-bit integer.

### ARM NEON

The conversion is optimized using ARM NEON SIMD instructions.

Instead of processing one sample at a time, the implementation processes four floating-point samples simultaneously using operations such as:

```cpp
vld1q_f32()
vmulq_f32()
vcvtq_s32_f32()
```

Conceptually:

```text
Sample 0 ─┐
Sample 1 ─┼─► NEON vector processing ─► 4 integer samples
Sample 2 ─┤
Sample 3 ─┘
```

This reduces the CPU overhead of the conversion, which is important because it executes continuously in the audio path.

---

# MIDI Engine

The host supports two MIDI input paths simultaneously.

```text
                MIDI
                 │
        ┌────────┴────────┐
        │                 │
     GPIO MIDI         USB MIDI
        │                 │
        ▼                 ▼
ProcessMidiByte()   USB callback
        │                 │
        └────────┬────────┘
                 ▼
       MidiPacketReceived()
                 │
                 ▼
          LV2 Atom Forge
                 │
                 ▼
        LV2 MIDI Event
```

## Serial MIDI

GPIO serial MIDI is processed continuously from the foreground `Run()` loop.

The MIDI interface operates at the standard:

```text
31250 baud
```

`ProcessMidiByte()` implements the MIDI byte-level state machine.

It handles:

* Running status
* Channel messages
* Two-byte messages
* Three-byte messages
* Message completion

Once a complete MIDI packet has been assembled, it is passed to:

```cpp
MidiPacketReceived()
```

---

## USB MIDI

USB MIDI is handled through Circle's USB MIDI device support.

The USB MIDI callback can directly pass completed MIDI packets to:

```cpp
MidiPacketReceived()
```

This gives the host a common processing path regardless of the physical MIDI interface.

---

# MIDI to LV2 Atom Conversion

LV2 plugins do not normally receive MIDI as arbitrary raw bytes.

MIDI messages are represented as LV2 Atom events inside an Atom Sequence.

The engine therefore converts incoming MIDI packets into LV2 events.

```text
Raw MIDI

90 3C 7F
│
▼
MidiPacketReceived()
│
▼
lv2_atom_forge_frame_time()
│
▼
LV2_MIDI__MidiEvent URID
│
▼
LV2 Atom Sequence
│
▼
Plugin event input
```

The Atom Forge serializes the event directly into the statically allocated Atom buffer.

The plugin then sees a standard LV2 MIDI event regardless of whether the original MIDI data came from GPIO or USB.

---

# Embedded LV2 Lifecycle

The LV2 lifecycle is simplified specifically for the bare-metal environment.

## Initialization

`lv2_init()` initializes the embedded LV2 host infrastructure.

It establishes:

* URID mapping
* Atom Forge
* host feature structures
* global host state

The MIDI URID is mapped during initialization so that incoming MIDI events can be represented using the numeric LV2 URID expected by the plugin.

---

## URID Mapping

Standard LV2 hosts commonly use RDF metadata and host libraries to manage URI mappings.

This project uses a much smaller in-memory implementation.

The mapper maintains a linked list similar to:

```text
URI
 │
 ├── numeric URID
 │
 ▼
next entry
```

When the plugin requests a URI mapping:

```text
URI
 │
 ▼
urid_map_func()
 │
 ├── already mapped → return existing ID
 │
 └── new URI        → assign new ID
```

The mapping is performed entirely in RAM.

There is no:

* RDF database
* Turtle parser
* filesystem lookup
* external metadata database

This is sufficient for the limited set of LV2 extensions required by the embedded host.

---

# Host Features

The host provides the LV2 features required by the statically linked plugin through a fixed feature array.

The basic configuration contains:

```text
Slot 0 → LV2 URID map
Slot 1 → LV2 URID unmap
Slot 2 → NULL terminator
```

This replaces the dynamic feature discovery and negotiation commonly found in desktop LV2 hosts.

The features are known at firmware build time, so there is no reason to construct them dynamically.

---

# Atom Forge

The LV2 Atom Forge is initialized during host startup:

```cpp
lv2_atom_forge_init(&forge, &lv2_urid_map);
```

The Forge is connected directly to the embedded URID mapper.

This allows the host to construct Atom events directly in its pre-allocated MIDI buffer.

The Atom Forge is particularly important for MIDI because it provides the serialization required to transform raw MIDI packets into standard LV2 event structures.

---

# Plugin Loading

A major difference from a desktop LV2 host is plugin discovery.

A conventional Linux LV2 host might:

```text
scan LV2 directories
        │
        ▼
read manifest.ttl
        │
        ▼
parse RDF metadata
        │
        ▼
find plugin binary
        │
        ▼
load shared object
```

`lv2host-circle` instead uses static linking:

```text
LV2 plugin source
        │
        ▼
compiled into firmware
        │
        ▼
lv2_descriptor(0)
        │
        ▼
LV2 descriptor
        │
        ▼
instantiate()
```

This removes filesystem dependencies and makes startup deterministic.

The plugin is therefore part of the firmware image rather than a runtime-loaded shared object.

---

# Port Connection

After instantiation, the host connects the plugin's ports to its pre-allocated buffers.

Conceptually:

```text
LV2 Plugin
   │
   ├── Audio Out L ──────► m_pAudioOutL
   │
   ├── Audio Out R ──────► m_pAudioOutR
   │
   └── MIDI Input ───────► Atom Sequence Buffer
```

The port setup is handled by the embedded `new_lv2_port` structures and `ConnectPluginPorts()`.

This eliminates the need to inspect plugin metadata dynamically to determine buffer sizes.

The embedded host knows the port configuration it supports.

---

# Runtime Plugin Switching

The foreground `Run()` loop also handles hardware controls.

A button connected to:

```text
GPIO 17
```

is used to switch between registered LV2 plugins.

The basic flow is:

```text
Button press
     │
     ▼
debounce
     │
     ▼
select next plugin
     │
     ▼
ConnectPluginPorts()
     │
     ▼
new active LV2 instance
```

Plugins can therefore be selected without rebooting the Raspberry Pi.

The audio engine continues to use the same static buffer architecture while the active plugin changes.

---


# Source Structure

The main components are organized around the host engine and Circle hardware layer.

```text
lv2host-circle/
│
├── Kernel.cpp
│   ├── Real-time audio callback
│   ├── MIDI processing
│   ├── plugin selection
│   ├── I2S output
│   └── hardware control
│
├── lv2.c
│   ├── Embedded LV2 host support
│   ├── URID mapping
│   ├── Atom Forge setup
│   ├── feature handling
│   └── LV2 port management
│
└── LV2 plugin
    └── Statically linked plugin implementation
```

---

The important property is that **MIDI events and audio buffers meet inside the LV2 processing block**.

A MIDI event received before or during the block is encoded into the Atom Sequence, and the plugin processes that event when its `run()` function is called for the corresponding audio block.

This follows the normal LV2 event-processing model while remaining entirely within the embedded runtime.

---

# Design Goals

The project is intentionally small and specialized.

The main design goals are:

* **Low latency**
* **Predictable memory usage**
* **Minimal dependencies**
* **No filesystem requirement**
* **No operating system requirement**
* **Direct hardware access**
* **Standard LV2 plugin DSP interface**
* **Support for MIDI-driven LV2 instruments and effects**
* **Efficient ARM audio processing**

It is not intended to be a complete replacement for a general-purpose Linux LV2 host.

Instead, it provides the smallest practical LV2 environment needed to run selected plugins on dedicated Raspberry Pi hardware.
