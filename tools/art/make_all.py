# Regenerates every asset.   python3 make_all.py [root]   (root defaults to the repository root)
# Needs build/layout.json (build/pinball_layout > build/layout.json) or PINBALL_LAYOUT pointing at it.
import os
import sys

import gen_header
import make_icons
import make_sprites
import make_table
import make_ui
from common import ROOT

root = sys.argv[1] if len(sys.argv) > 1 else ROOT
make_sprites.main(os.path.join(root, "assets", "sprites"))
make_table.main(os.path.join(root, "assets", "table", "background.png"), os.path.join(root, "assets", "sprites"))   # bakes the unlit inserts from the sprites just written
make_ui.main(os.path.join(root, "assets", "ui"))
make_icons.main(root)
gen_header.main(os.path.join(root, "src", "generated", "ArtPalette.hpp"))
