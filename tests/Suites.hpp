#pragma once

// Every test suite. Each is defined in its own file, which includes this
// header, and is called from main.cpp. A declared suite with no definition
// fails to link.
void testArgs();
void testAssetName();
void testBumpers();
void testFixedTimestep();
void testFlippers();
void testGame();
void testPhysics();
void testPlunger();
void testPngHeader();
