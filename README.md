# [WIP] lv2host-circle

A first test of LV2 plugin host for the Raspberry Pi, ported to [Circle](https://github.com/rsta2/circle) bare-metal environment and [circle-stdlib](https://github.com/smuehlst/circle-stdlib). 

This project is directly derived from and inspired by the original Raspberry Pi bare-metal LV2 host developed by **Joe Button (Joeboy)** in 2013 ([pixperiments/pitracker](https://github.com/Joeboy/pixperiments/tree/master/pitracker)).

All hardware specific code was removed from the original library and only lv2 loading engine code is used.

No full project description is available yet. Tested with included lv2 plugins from the Joeboy repo which where adjusted to compile on Raspberry Pi Zero 2w with Circle 32bit config. 


## Directory Layout:

```text
circle-stdlib/
├── Config.mk
├── Rules.mk
└── samples/
    └── lv2host-circle/
        ├── main.cpp
        ├── kernel.cpp
        ├── includes/
        └── plugins/
            ├── organ.lv2/
            ├── piano.lv2/
            └── wavplayer.lv2/
```

## Compilation 

Build commands are executed directly inside each plugin's subfolder (e.g., `plugins/organ.lv2`).

```bash
cd plugins/organ.lv2
make clean
make
```

After that copy compiled kernel8-32.img to the SD card together with other nesesary files (config.txt, bootcode.bin, start.elf, fixup.dat, bcm2710-rpi-zero-2-w.dtb). 


## I2S Audio

**config.txt**

arm\_64bit=0 \
kernel=kernel8-32.img \
dtparam=i2s=on \
enable\_uart=1

Use standart i2s [PCM5102](https://user-images.githubusercontent.com/2480569/166105580-da11481c-8fc7-4375-8ab1-3031ab5c6ad0.png) dac pins for RPi zero 2w. 


## What is not implemented

* dynamic plugin loading
* effects plugins
* presets


## AI disclosure

Port developed using Gemini AI and manually tested. Expect experimental code—things might break!


## Licence

In general, this software is subject to the following terms:

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

This repository contains some files by people other than me. Those files are
subject to the terms specified therein.






