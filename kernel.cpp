/*
 * ============================================================================
 * [WIP] Circle LV2 Host on Raspberry Pi Zero 2w bare metal 
 * ============================================================================
 *
 * PROJECT OVERVIEW:
 * This bare-metal Raspberry Pi project runs an LV2 plugin host built
 * on top of the Circle C++ bare-metal framework and Circle-Stdlib.
 *
 * SYSTEM ARCHITECTURE:
 *  - USB & Input Subsystem:
 *      * Uses Circle's USB host stack (`CUSBHC`) to enumerate USB MIDI controllers.
 *      * Incoming raw MIDI messages are wrapped into LV2 Atom Sequences and passed
 *        directly into the plugin's `run()` processing loop.
 *  - Audio Engine Subsystem:
 *      * Interfaces with Pi hardware audio drivers (I2S) via Circle Sound lib.
 *  - Plugin Core:
 *      * Standard LV2 C ABI (`lv2.h`) handling URID mapping, port connections.
 *
 * ACKNOWLEDGEMENTS & CREDITS:
 *  - Original LV2 plugin host architecture by Joe Button (Joeboy):
 *    https://github.com/Joeboy/pixperiments/tree/master/pitracker
 *  - Circle bare-metal framework for Raspberry Pi by Rene Stange.
      https://github.com/rsta2/circle
    - C and C++ standard library support for Circle
      https://github.com/smuehlst/circle-stdlib
 * ============================================================================
*/

#include "kernel.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <circle/timer.h>
#include <circle/gpiopin.h>
#include <circle/serial.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

extern "C" {
#include "lv2.h"
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

// Assuming forge and lv2_urid_map were made global in lv2.c
extern LV2_Atom_Forge forge;
extern LV2_URID_Map lv2_urid_map;

const LV2_Descriptor* lv2_descriptor(uint32_t index);
}

#define SAMPLE_RATE     44100
#define CHANNELS        2
#define BUTTON_PIN      17

CKernel *CKernel::s_pThis = nullptr;

// Mapped URID for MIDI events used by the interrupt handler
static LV2_URID g_MidiEventURID = 0;

CKernel::CKernel(void)
:  
  CStdlibAppStdio("audiokernel"),
  CI2SSoundBaseDevice(&mInterrupt, SAMPLE_RATE, 2048, FALSE, 0, 0, CI2SSoundBaseDevice::DeviceModeTXOnly, 2),
  m_pUSBMIDI(nullptr),
  m_Button(BUTTON_PIN, GPIOModeInputPullUp),
  m_pLv2World(nullptr),
  m_pCurrentPlugin(nullptr),
  m_pAtomPort(nullptr),
  m_pAudioOutL(nullptr),
  m_pAudioOutR(nullptr)
{
    s_pThis = this;
}

CKernel::~CKernel(void)
{
    if (m_pLv2World)
    {
        for (const Lv2Plugin *plug = m_pLv2World->plugin_list; plug != nullptr; plug = plug->next)
        {
            if (plug->descriptor && plug->handle)
            {
                if (plug->descriptor->deactivate)
                {
                    plug->descriptor->deactivate(plug->handle);
                }
                if (plug->descriptor->cleanup)
                {
                    plug->descriptor->cleanup(plug->handle);
                }
            }
        }
        free(m_pLv2World);
        m_pLv2World = nullptr;
    }

    if (m_pAtomPort) free(m_pAtomPort);
    if (m_pAudioOutL) free(m_pAudioOutL);
    if (m_pAudioOutR) free(m_pAudioOutR);
}

void CKernel::ConnectPluginPorts(const Lv2Plugin *plugin)
{
    if (!plugin || !plugin->descriptor) return;

    plugin->descriptor->connect_port(plugin->handle, m_pAudioOutL->id, m_pAudioOutL->buffer);
    plugin->descriptor->connect_port(plugin->handle, m_pAudioOutR->id, m_pAudioOutR->buffer);
    plugin->descriptor->connect_port(plugin->handle, m_pAtomPort->id,  m_pAtomPort->buffer);

    if (plugin->descriptor->activate)
    {
        plugin->descriptor->activate(plugin->handle);
    }
}

void CKernel::ProcessMidiByte(u8 byte)
{
    static u8 status = 0;
    static u8 data1 = 0;
    static u8 byteIndex = 0;

    if (byte >= 0xF8) return;

    if (byte & 0x80)
    {
        status = byte;
        byteIndex = 0;
        return;
    }

    if (status == 0) return;

    u8 cmd = status & 0xF0;
    u8 packet[3] = { status, 0, 0 };

    if (byteIndex == 0)
    {
        data1 = byte;
        byteIndex++;

        if (cmd == 0xC0 || cmd == 0xD0)
        {
            packet[1] = data1;
            MidiPacketReceived(0, packet, 2);
            byteIndex = 0;
        }
    }
    else if (byteIndex == 1)
    {
        u8 data2 = byte;
        packet[1] = data1;
        packet[2] = data2;
        MidiPacketReceived(0, packet, 3);
        byteIndex = 0;
    }
}

void CKernel::MidiPacketReceived(unsigned nCable, u8 *pPacket, unsigned nLength)
{
    if (nLength >= 2 && s_pThis && s_pThis->m_pAtomPort && g_MidiEventURID != 0)
    {
        u8 status = pPacket[0] & 0xF0;
        u8 channel = pPacket[0] & 0x0F;
        
        if (status == 0x90 && nLength >= 3) // Note On
        {
            u8 note = pPacket[1];
            u8 velocity = pPacket[2];
            if (velocity > 0)
            {
                printf("MIDI Note On:  Note=%u, Vel=%u, Ch=%u\n", note, velocity, channel + 1);
            }
            else
            {
                printf("MIDI Note Off: Note=%u, Ch=%u (Vel=0)\n", note, channel + 1);
            }
        }
        else if (status == 0x80 && nLength >= 3) // Note Off
        {
            u8 note = pPacket[1];
            u8 velocity = pPacket[2];
            printf("MIDI Note Off: Note=%u, Vel=%u, Ch=%u\n", note, velocity, channel + 1);
        }

        // 1. Time stamp the event using the singleton instance pointer
        lv2_atom_forge_frame_time(&s_pThis->forge, 0); 
        
        // 2. Wrap the payload in an LV2 Atom typed as a MidiEvent! 
        lv2_atom_forge_atom(&s_pThis->forge, nLength, g_MidiEventURID);
        
        // 3. Write the actual bytes
        lv2_atom_forge_write(&s_pThis->forge, pPacket, nLength);
    }
}

boolean CKernel::Initialize(void)
{
    if (!CStdlibAppStdio::Initialize())
    {
        return FALSE;
    }

    printf("--- KERNEL INITIALIZATION START ---\n");

    if (mSerial.Initialize(31250))
    {
        printf("Serial MIDI initialized on GPIO 14/15 (31250 Baud).\n");
    }

    printf("Initializing LV2 Host...\n");
    m_pLv2World = lv2_init(SAMPLE_RATE);
    if (!m_pLv2World)
    {
        printf("ERROR: Failed to initialize LV2 World!\n");
        return FALSE;
    }

    // Map the MidiEvent URID
    g_MidiEventURID = lv2_urid_map.map(lv2_urid_map.handle, LV2_MIDI__MidiEvent);

    // Explicitly register and instantiate the lv2 plugin before audio starts
    const LV2_Descriptor *desc = lv2_descriptor(0);
    if (desc)
    {
        LV2_Handle handle = desc->instantiate(desc, SAMPLE_RATE, nullptr, m_pLv2World->lv2_features);
        if (handle)
        {
            Lv2Plugin *plug = (Lv2Plugin *)malloc(sizeof(Lv2Plugin));
            plug->descriptor = (LV2_Descriptor *)desc;
            plug->handle = handle;
            plug->next = nullptr;

            m_pLv2World->plugin_list = plug;
            m_pLv2World->num_plugins = 1;

            printf("Successfully registered plugin: %s\n", desc->URI);
        }
        else
        {
            printf("ERROR: Plugin instantiate failed for %s!\n", desc->URI);
            return FALSE;
        }
    }
    else
    {
        printf("ERROR: lv2_descriptor(0) returned NULL!\n");
        return FALSE;
    }

    m_pAudioOutL = new_lv2_port(lv2_audio_port, 1);
    m_pAudioOutR = new_lv2_port(lv2_audio_port, 2);
    m_pAtomPort  = new_lv2_port(lv2_atom_port, 3);

    memset(m_pAudioOutL->buffer, 0, sizeof(float) * LV2_AUDIO_BUFFER_SIZE);
    memset(m_pAudioOutR->buffer, 0, sizeof(float) * LV2_AUDIO_BUFFER_SIZE);

    m_pCurrentPlugin = m_pLv2World->plugin_list;
    ConnectPluginPorts(m_pCurrentPlugin);

    // Initial sequence head setup
    lv2_atom_forge_set_buffer(&forge, (uint8_t *)m_pAtomPort->buffer, LV2_ATOM_BUFFER_SIZE);
    lv2_atom_forge_sequence_head(&forge, &m_MidiFrame, 0);

    printf("Loaded %u LV2 plugin(s). Active URI: %s\n", m_pLv2World->num_plugins, m_pCurrentPlugin->descriptor->URI);

    SetWriteFormat(SoundFormatSigned24_32, CHANNELS);

    if (!Start())
    {
        printf("ERROR: Failed to start I2S sound device\n");
        return FALSE;
    }

    printf("Audio engine ready. Looking for USB & Serial MIDI inputs...\n");
    return TRUE;
}

unsigned CKernel::GetChunk(u32 *pBuffer, unsigned nChunkSize)
{
    if (!m_pCurrentPlugin || !m_pCurrentPlugin->descriptor)
    {
        memset(pBuffer, 0, nChunkSize * sizeof(u32));
        return nChunkSize;
    }

    u32 *pDest = pBuffer;
    unsigned nSamplesLeft = nChunkSize;
    const unsigned nMaxBufferFrames = LV2_AUDIO_BUFFER_SIZE;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const float32x4_t vMin   = vdupq_n_f32(-1.0f);
    const float32x4_t vMax   = vdupq_n_f32(1.0f);
    const float32x4_t vScale = vdupq_n_f32(8388607.0f);
#endif

    while (nSamplesLeft > 0)
    {
        unsigned nFramesToRender = nSamplesLeft / 2;
        if (nFramesToRender > nMaxBufferFrames)
        {
            nFramesToRender = nMaxBufferFrames;
        }

        // Finalize atom sequence for this block
        lv2_atom_forge_pop(&forge, &m_MidiFrame);

        // Run LV2 Plugin DSP
        if (m_pCurrentPlugin->descriptor->run)
        {
            m_pCurrentPlugin->descriptor->run(m_pCurrentPlugin->handle, nFramesToRender);
        }

        // Reset atom buffer for the NEXT block
        lv2_atom_forge_set_buffer(&forge, (uint8_t *)m_pAtomPort->buffer, LV2_ATOM_BUFFER_SIZE);
        lv2_atom_forge_sequence_head(&forge, &m_MidiFrame, 0);

        float *pOutL = (float *)m_pAudioOutL->buffer;
        float *pOutR = (float *)m_pAudioOutR->buffer;

        unsigned nSamplesRendered = nFramesToRender * 2;
        
        for (unsigned f = 0, i = 0; f < nFramesToRender; f++, i += 2)
        {
            m_TempFloatBuffer[i]     = pOutL[f];
            m_TempFloatBuffer[i + 1] = pOutR[f];
        }

        const float *pSrc = m_TempFloatBuffer;
        unsigned i = 0;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        for (; i + 3 < nSamplesRendered; i += 4)
        {
            float32x4_t v = vld1q_f32(pSrc + i);
            v = vminq_f32(vmaxq_f32(v, vMin), vMax);
            int32x4_t vInt = vcvtq_s32_f32(vmulq_f32(v, vScale));
            vst1q_s32((int32_t *)(pDest + i), vInt);
        }
#endif

        for (; i < nSamplesRendered; i++)
        {
            float fSample = fminf(fmaxf(pSrc[i], -1.0f), 1.0f);
            pDest[i] = (u32)(s32)(fSample * 8388607.0f);
        }

        pDest += nSamplesRendered;
        nSamplesLeft -= nSamplesRendered;

        memset(m_pAudioOutL->buffer, 0, sizeof(float) * LV2_AUDIO_BUFFER_SIZE);
        memset(m_pAudioOutR->buffer, 0, sizeof(float) * LV2_AUDIO_BUFFER_SIZE);
    }

    return nChunkSize;
}

CStdlibApp::TShutdownMode CKernel::Run(void)
{
    printf("Entering main event loop...\n");

    CDeviceNameService *pDNS = CDeviceNameService::Get();
    CUSBMIDIDevice *pMIDIDev = (CUSBMIDIDevice *)pDNS->GetDevice("umidi1", FALSE);

    if (pMIDIDev != nullptr)
    {
        pMIDIDev->RegisterPacketHandler(MidiPacketReceived);
        printf(">>> USB MIDI keyboard detected! <<<\n");
    }

    bool bLastButtonState = true;

    while (1)
    {
        u8 rxByte;
        while (mSerial.Read(&rxByte, 1) > 0)
        {
            ProcessMidiByte(rxByte);
        }

        bool bCurrentButtonState = m_Button.Read();
        if (bLastButtonState && !bCurrentButtonState)
        {
            if (m_pCurrentPlugin && m_pCurrentPlugin->next)
            {
                m_pCurrentPlugin = m_pCurrentPlugin->next;
            }
            else if (m_pLv2World)
            {
                m_pCurrentPlugin = m_pLv2World->plugin_list;
            }

            if (m_pCurrentPlugin && m_pCurrentPlugin->descriptor)
            {
                ConnectPluginPorts(m_pCurrentPlugin);
                printf("Switched to LV2 Plugin: %s\n", m_pCurrentPlugin->descriptor->URI);
            }

            CTimer::SimpleMsDelay(300);
        }
        bLastButtonState = bCurrentButtonState;

        CTimer::SimpleMsDelay(2);
    }

    return ShutdownHalt;
}
