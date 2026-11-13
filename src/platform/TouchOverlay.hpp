#pragma once

// The on-screen controls of the touch idiom: the buttons and the flipper
// pads, drawn from the rectangles in screen::Layout. They are drawn by
// TableRenderer::drawTouchOverlay (platform/TableRenderer.hpp), which owns
// the textures; what that needs beyond its own class is declared here.
//
// Nothing is declared yet: the overlay is not drawn.
