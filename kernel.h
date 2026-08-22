#ifndef _kernel_h
#define _kernel_h

#include <circle_stdlib_app.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/devicenameservice.h>
#include <circle/usb/usbmidi.h>
#include <circle/timer.h>
#include <circle/gpiopin.h>
#include <circle/serial.h>
#include <vector>
#include <string>

extern "C" {
#include "lv2.h"
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
}

class CKernel : public CStdlibAppStdio, public CI2SSoundBaseDevice
{
public:
    CKernel (void);
    ~CKernel (void);

    boolean Initialize (void) override;
    TShutdownMode Run (void) override;

private:
    unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override;

    static void MidiPacketReceived (unsigned nCable, u8 *pPacket, unsigned nLength);
    void ProcessMidiByte (u8 byte);
    void ConnectPluginPorts (const Lv2Plugin *plugin);

private:
    static CKernel            *s_pThis;

    CUSBMIDIDevice            *m_pUSBMIDI;
    CGPIOPin                   m_Button;

    Lv2World                  *m_pLv2World;
    const Lv2Plugin           *m_pCurrentPlugin;
    lv2_port                  *m_pAtomPort;
    lv2_port                  *m_pAudioOutL;
    lv2_port                  *m_pAudioOutR;

    LV2_Atom_Forge             forge;
    LV2_Atom_Forge_Frame       m_MidiFrame;

    float                      m_TempFloatBuffer[2048];
};

#endif