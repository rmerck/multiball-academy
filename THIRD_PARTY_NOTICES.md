# Third-party notices

Multiball Academy is Copyright (c) 2026 Ryan Merck and is licensed under the
PolyForm Noncommercial License 1.0.0 (SPDX: `PolyForm-Noncommercial-1.0.0`);
the terms are in [LICENSE](LICENSE).

Earlier releases of Multiball Academy, including the v1.0.0 macOS release and
the commits before the change to PolyForm Noncommercial, were published under
the MIT licence and remain under the terms they were released with.

## Code built into the game

The game links the libraries below. Each remains under its own licence,
reproduced here in full and shown on the game's About screen, in this order.
Those licences apply to those components only, not to the rest of the game.
`tools/check_notices.py` (ctest `layout_notices`) checks that the About screen
says what each block below says, and, in a build of the pinned sources, what
their own licence files say.

### Box2D

Version 3.2.0 (pinned in `cmake/PinnedDeps.cmake`). MIT licence (SPDX: `MIT`).

```text
MIT License

Copyright (c) 2022 Erin Catto

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### SDL (Simple DirectMedia Layer)

Version 3.4.20 (pinned in `cmake/PinnedDeps.cmake`). zlib licence
(SPDX: `Zlib`).

```text
Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

### yuv2rgb (part of SDL)

SDL's YUV to RGB conversion, `src/video/yuv2rgb` in the SDL source, is linked
into the game and carries its own licence there: BSD 3-Clause
(SPDX: `BSD-3-Clause`).

```text
Copyright (c) 2016, Adrien Descamps
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of yuv2rgb nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Other code inside SDL

Checked against SDL 3.4.20's sources and the symbols of the linked game. None
of these asks for a notice in a binary:

- Code by other authors under SDL's own zlib licence (Valve's controller
  code, the SDL_gfx rotation code by Andreas Schiffler, and others) is covered
  by the SDL notice above; zlib asks for no acknowledgement in a product.
- HIDAPI (`src/hidapi`, Copyright 2009 Alan Ott, Signal 11 Software) offers a
  choice of licences; the game uses it under the original HIDAPI licence,
  which asks only that the copyright notice in the source files stays intact.
- stb_image (`src/video/stb_image.h`) and miniz (`src/video/miniz.h`) are
  used under their public-domain dedications.
- SDL's own maths library (`src/libm`, from Sun Microsystems' fdlibm) is not
  linked: SDL uses the system's maths library where there is one.
- OpenVR and the Khronos and DirectX headers are not part of the game's
  builds.

### Linux builds only

A Linux build links three more files of SDL's X11 and Wayland video code.
They are compiled only with those video drivers, so they are not in the
macOS or iOS builds, the only builds that are released, and the About screen
does not carry them. Anyone who gives out a Linux build passes these notices
on with it.

`src/events/imKStoUCS.c` (X11 keysym to Unicode table), an MIT-style
licence:

```notice
Copyright (C) 2003-2006,2008 Jamey Sharp, Josh Triplett
Copyright © 2009 Red Hat, Inc.
Copyright 1990-1992,1999,2000,2004,2009,2010 Oracle and/or its affiliates.
All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

`src/video/x11/xsettings-client.c` (XSETTINGS client), a permission notice
in the style of the Historical Permission Notice and Disclaimer:

```notice
Copyright © 2001, 2007 Red Hat, Inc.
Copyright 2024 Igalia S.L.

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation, and that the name of Red Hat not be used in advertising or
publicity pertaining to distribution of the software without specific,
written prior permission.  Red Hat makes no representations about the
suitability of this software for any purpose.  It is provided "as is"
without express or implied warranty.

RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL RED HAT
BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

Author:  Owen Taylor, Red Hat, Inc.
```

`src/video/x11/edid-parse.c` (EDID parser), an MIT-style licence:

```notice
Copyright 2007 Red Hat, Inc.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Fonts used to draw the art

`tools/art/sources/fonts` holds four fonts under the SIL Open Font License,
Version 1.1 (SPDX: `OFL-1.1`), each with its licence text beside it. They
remain under that licence. They are used only to draw the art: outlines from
them end up in the generated images, and no font file is shipped in the game.
The OFL does not cover documents or images made with a font, and asks for no
attribution in them; they are listed here for the record.

- Audiowide: Copyright (c) 2012, Brian J. Bonislawsky DBA Astigmatic (AOETI),
  with Reserved Font Names "Audiowide" (`OFL-audiowide.txt`)
- Russo One: Copyright (c) 2011-2012, Jovanny Lemonad, with Reserved Font Name
  "Russo" (`OFL-russoone.txt`)
- Saira Stencil One: Copyright 2019 The Saira Stencil Project Authors
  (`OFL-sairastencilone.txt`)
- Silkscreen: Copyright 2001 The Silkscreen Project Authors
  (`OFL-silkscreen.txt`)
