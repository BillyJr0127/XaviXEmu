# Third-party notices

XaviXEmu contains original project code and code whose hardware behaviour was
adapted from separately licensed upstream sources. The XaviXEmu first-party
code is covered by the repository's BSD 3-Clause `LICENSE` file.

## MAME-derived hardware behaviour

The CPU, bus, video, audio, timer, DMA, EEPROM, mathematics, and XaviX 2
behaviour in XaviXEmu was studied from, adapted from, or independently ported
from individual MAME source files marked `BSD-3-Clause`.

Upstream repository: <https://github.com/mamedev/mame>

Reference revision:
`cf2d7a9be259552a3b493156e50e9149b6856448`

| MAME source file | Copyright holder(s) named by the file |
| --- | --- |
| `src/devices/cpu/m6502/m6502.cpp` | Olivier Galibert |
| `src/devices/cpu/m6502/m6502.h` | Olivier Galibert |
| `src/devices/cpu/m6502/om6502.lst` | Olivier Galibert |
| `src/devices/cpu/m6502/xavix.cpp` | David Haywood |
| `src/devices/cpu/m6502/xavix.h` | David Haywood |
| `src/devices/cpu/m6502/oxavix.lst` | David Haywood |
| `src/devices/cpu/m6502/xavix2000.cpp` | David Haywood |
| `src/devices/cpu/m6502/xavix2000.h` | David Haywood |
| `src/devices/cpu/m6502/oxavix2000.lst` | David Haywood |
| `src/devices/cpu/xavix2/xavix2.cpp` | Olivier Galibert, Nathan Gilbert |
| `src/devices/cpu/xavix2/xavix2.h` | Olivier Galibert, Nathan Gilbert |
| `src/devices/cpu/xavix2/xavix2d.cpp` | Olivier Galibert, Nathan Gilbert |
| `src/devices/cpu/xavix2/xavix2d.h` | Olivier Galibert, Nathan Gilbert |
| `src/devices/machine/i2cmem.cpp` | smf |
| `src/devices/machine/i2cmem.h` | smf |
| `src/mame/tvgames/xavix.cpp` | David Haywood, Angelo Salese |
| `src/mame/tvgames/xavix.h` | David Haywood |
| `src/mame/tvgames/xavix_m.cpp` | David Haywood |
| `src/mame/tvgames/xavix_v.cpp` | David Haywood |
| `src/mame/tvgames/xavix_sound.cpp` | ramacat, David Haywood |
| `src/mame/tvgames/xavix_sound.h` | ramacat, David Haywood |
| `src/mame/tvgames/xavix_2000.cpp` | David Haywood |
| `src/mame/tvgames/xavix_2000.h` | David Haywood |
| `src/mame/tvgames/xavix_math.cpp` | David Haywood |
| `src/mame/tvgames/xavix_math.h` | David Haywood |
| `src/mame/tvgames/xavix_adc.cpp` | David Haywood |
| `src/mame/tvgames/xavix_adc.h` | David Haywood |
| `src/mame/tvgames/xavix_anport.cpp` | David Haywood |
| `src/mame/tvgames/xavix_anport.h` | David Haywood |
| `src/mame/tvgames/xavix_io.cpp` | David Haywood |
| `src/mame/tvgames/xavix_io.h` | David Haywood |
| `src/mame/tvgames/xavix2.cpp` | David Haywood |

The MAME project as a whole has its own distribution terms. The files listed
above individually carry the following BSD 3-Clause terms. Their original
copyright holders have not been replaced by the XaviXEmu copyright notice.

Copyright (c) 1997-2026 MAMEdev and contributors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

XaviXEmu is not endorsed by MAMEdev. MAME is a registered trademark of
Gregory Ember. No MAME executable, framework, artwork, or ROM data is included
in this repository.

## miniz 2.2.0

XaviXEmu vendors the single-file miniz 2.2.0 ZIP reader in
`third_party/miniz`. Archive-writing and zlib compatibility APIs are disabled
by the build.

Copyright 2013-2014 RAD Game Tools and Valve Software

Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

Copyright 2016 Martin Raiber

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Trademarks and non-endorsement

XaviXEmu is an independently developed, unofficial emulator. It is not
sponsored, approved, endorsed by, or affiliated with any game publisher,
developer, hardware manufacturer, MAMEdev, or other rights holder. Product
names and trademarks belong to their respective owners.
