#pragma once

#include <string_view>

// The notices shown on the About screen, in sections: this game's copyright
// and licence, then the licences of the code built into it: Box2D, SDL, and
// yuv2rgb, which SDL carries under its own licence. Each paragraph is one
// line here and is wrapped to the dialog when drawn; the display's font is
// upper case only, so the text is shown in capitals. The game's notice names
// the licence in LICENSE and gives its address, as that licence asks; the
// full terms are in LICENSE. The library notices are the words of
// THIRD_PARTY_NOTICES.md and of the licence files of the pinned Box2D and
// SDL sources. tools/check_notices.py checks all of it, sections and order.
namespace pinball::notices {

inline constexpr std::string_view kText =
    "MULTIBALL ACADEMY: Copyright (c) 2026 Ryan Merck. Licensed under the PolyForm "
    "Noncommercial License 1.0.0.\n"
    "\n"
    "https://polyformproject.org/licenses/noncommercial/1.0.0\n"
    "\n"
    "BOX2D: MIT LICENSE\n"
    "\n"
    "Copyright (c) 2022 Erin Catto\n"
    "\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy of this "
    "software and associated documentation files (the \"Software\"), to deal in the Software "
    "without restriction, including without limitation the rights to use, copy, modify, merge, "
    "publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons "
    "to whom the Software is furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in all copies or "
    "substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, "
    "INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR "
    "PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE "
    "FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR "
    "OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER "
    "DEALINGS IN THE SOFTWARE.\n"
    "\n"
    "SDL (SIMPLE DIRECTMEDIA LAYER): ZLIB LICENSE\n"
    "\n"
    "Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>\n"
    "\n"
    "This software is provided 'as-is', without any express or implied warranty. In no event "
    "will the authors be held liable for any damages arising from the use of this software.\n"
    "\n"
    "Permission is granted to anyone to use this software for any purpose, including commercial "
    "applications, and to alter it and redistribute it freely, subject to the following "
    "restrictions:\n"
    "\n"
    "1. The origin of this software must not be misrepresented; you must not claim that you "
    "wrote the original software. If you use this software in a product, an acknowledgment in "
    "the product documentation would be appreciated but is not required.\n"
    "\n"
    "2. Altered source versions must be plainly marked as such, and must not be misrepresented "
    "as being the original software.\n"
    "\n"
    "3. This notice may not be removed or altered from any source distribution.\n"
    "\n"
    "YUV2RGB (PART OF SDL): BSD 3-CLAUSE LICENSE\n"
    "\n"
    "Copyright (c) 2016, Adrien Descamps\n"
    "All rights reserved.\n"
    "\n"
    "Redistribution and use in source and binary forms, with or without modification, are "
    "permitted provided that the following conditions are met:\n"
    "\n"
    "* Redistributions of source code must retain the above copyright notice, this list of "
    "conditions and the following disclaimer.\n"
    "\n"
    "* Redistributions in binary form must reproduce the above copyright notice, this list of "
    "conditions and the following disclaimer in the documentation and/or other materials "
    "provided with the distribution.\n"
    "\n"
    "* Neither the name of yuv2rgb nor the names of its contributors may be used to endorse or "
    "promote products derived from this software without specific prior written permission.\n"
    "\n"
    "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY "
    "EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF "
    "MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE "
    "COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, "
    "EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF "
    "SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) "
    "HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR "
    "TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS "
    "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.";

}  // namespace pinball::notices
