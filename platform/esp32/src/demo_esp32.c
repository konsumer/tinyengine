/**
 * The ESP32 entry point: the demo, unchanged.
 *
 * examples/demo/demo.c is the same file the desktop and web builds compile. The board needs
 * no port of it because nothing in the game loads anything at runtime -- every tile is a
 * `static const pntr_color[]` that tiny_bake.py put in flash.
 */
#include "demo.c"
