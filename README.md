# tinyengine

A tiled game engine for [pntr_app](https://github.com/robloach/pntr_app). One codebase runs
on the web, on the desktop, and on an ESP32.

Maps are drawn in [Tiled](https://www.mapeditor.org/) and baked into C headers at build
time. Everything the baker writes is `static const`, so on a microcontroller it lands in
`.rodata` — memory mapped for reads — and pntr draws every tile straight out of flash. No
image is decoded at runtime, no map is parsed at runtime, and the only heap the engine takes
is one object array, plus each sound effect the first time it plays.

A game is three things:

1. Maps in Tiled, saved as JSON.
2. `tiny_bake.py` run over them, once, at build time.
3. One function that says how objects behave.

The demo is 6 maps, 47 unique tiles and one 340 line C file. On an ESP32 it is **25 KB of
static RAM and 384 KB of flash**, maps included. Sound is baked the same way maps are — a
MOD for the music and 104 byte [rFXGen](https://raylibtech.itch.io/rfxgen) files for every
effect — and mixed by the engine.

---

## Three kinds of map, one runtime

Each map carries a `map_type` string property. There are only three, because most of what
looks like a separate genre turns out to be one of these with a different map size or a
different property.

| `map_type` | Also accepts | Projection | Physics |
| --- | --- | --- | --- |
| `plat` | `smb`, `platformer`, `sidescroller`, `shooter` | Side-on | Gravity, one-way platforms |
| `top` | `rpg`, `zelda`, `pokemon`, `arcade`, `pacman`, `1943` | Top-down | Solid tiles, no gravity |
| `iso` | `isometric`, `qbert`, `rc-pro-am` | Diamond, painter's algorithm | Solid tiles, no gravity |

What the other genres actually are:

- **Arcade screen** — a `top` map no bigger than the screen. The camera has nowhere to
  scroll, so it centres itself and stays put. Nothing to configure.
- **Room-to-room exploration**, the way the first Zelda works — a set of screen-sized `top`
  maps joined by portals. Each room is its own map, so there is no room camera to write.
  (There is still a `camera = room` property if you would rather snap across one big map.)
- **Side-scrolling shooter** — a `plat` map with `gravity` set to `0`. Nothing falls, so a
  jump has nothing to push against and up/down simply fly. The demo's `sky` map is this, and
  it shares every line of code with the platformer.
- **Top-down shooter** like 1943 — a `top` map. It was never a separate mode.

The type picks the projection, the camera defaults and the collision rules. It does not
change how you write behaviour, so one game can mix all three and walk between them.

---

## The behaviour function

One function, called once per live object per frame. Inside it you can look at any other
object, the map, the camera and the tiles.

```c
void Behaviour(tiny_game* game, tiny_object* self, float dt) {
  if (tiny_is(self, "player")) {
    // Reads the map's gravity, slides along walls, sets self->on_ground.
    tiny_step(game, self, dt);
    tiny_play(game, self, "player_side");
    return;
  }

  if (tiny_is(self, "coin")) {
    if (tiny_overlaps(self, game->player)) {
      score += tiny_obj_int(self, "value", 10);   // a Tiled custom property
      tiny_kill(game, self);
    }
    return;
  }

  if (tiny_is(self, "npc") && tiny_overlaps(self, game->player)) {
    tiny_say(game, tiny_obj_str(self, "says", "..."), 2.5f);  // a box along the bottom
  }
}
```

That is the whole interface. `tiny_is` matches the object's **Class** in Tiled, and
`tiny_obj_*` reads its custom properties.

Portals need no code. An object of class `portal` sends the player to the map in its
`destination` property, lands them on the object named by `spawn`, and plays the sound
named by `sound`. One you arrive standing on waits until you step off it.

### What you can ask

```c
tiny_object* p = game->player;                   // whoever has class or name "player"
pntr_rectangle view = tiny_view(game);           // what the camera can see, in pixels
pntr_rectangle tiles = tiny_view_tiles(game);    // the same, in tiles

for (int i = 0; i < tiny_object_count(game); i++) {
  tiny_object* other = tiny_object_at_index(game, i);
}
tiny_object* door = tiny_find(game, "cave_door");             // by Tiled name
for (tiny_object* e = tiny_find_next(game, "enemy", NULL);    // by Tiled class
     e != NULL; e = tiny_find_next(game, "enemy", e)) { }

tiny_play(game, self, "walk");            // a named animation from the tileset
tiny_pose(game, self, "walk", 0);         // hold frame 0: the standing pose

bool wall = tiny_solid(game, x, y);
bool blocked = tiny_solid_rect(game, x, y, w, h);
uint8_t flags = tiny_flags_at(game, tx, ty);     // SOLID PLATFORM LADDER HAZARD USER1..4
tiny_move(game, self, dx, dy);                   // axis-separated, sets self->hit

float dx = tiny_axis_x(game);                    // -1, 0, 1 from arrows and d-pad
bool jump = tiny_held(game, PNTR_APP_KEY_SPACE, PNTR_APP_GAMEPAD_BUTTON_A);
tiny_say(game, "ouch!", 2.5f);                   // word-wrapped message box
```

**Touch controls.** Build with `TINY_TOUCH_CONTROLS` and the engine draws a translucent d-pad
in the bottom left and A/B buttons in the bottom right. They work by touch or with the mouse,
and they hold the matching gamepad buttons, so anything read through `tiny_held()` and
`tiny_axis_x/y()` follows them with no game code. On the web they stay hidden until the
screen is first touched, so a desktop browser never sees them, and several fingers work at
once: hold right and press A. Everywhere else they show from the start. The demo turns them
on for the web and for the CYD.

The CYD's resistive panel only ever reports one point, so there you cannot hold the d-pad
and press a button at the same moment.

The engine reuses pntr's own types wherever one fits: tiles are `pntr_image`, colours are
`pntr_color`, and the view queries return `pntr_rectangle`. The box helpers take loose
floats rather than a `pntr_rectangle` because world units are fractional -- on an isometric
map they are tiles, so an object sits at something like (3.4, 7.2) and rounding it to a
whole tile would test the wrong cells.

---

## Authoring in Tiled

Everything the engine knows comes from Tiled, not from a config file.

**On the map**

| Property | Meaning |
| --- | --- |
| `map_type` | Which mode to play in. Required. See the table above. |
| `name` | What `tiny_goto_map()` calls it. Defaults to the filename. |
| `gravity` | Overrides the platformer default of 600 units/s². |
| `camera` | `follow`, `room`, or `fixed`, overriding the type's default. |
| `camera_ease`, `camera_deadzone`, `camera_clamp` | Fine tuning. |

**On a tile, in the tileset editor** — booleans that become collision flags:
`solid`, `platform` (one-way, only solid falling onto it), `ladder`, `hazard`,
and `user1` through `user4`, which are yours.

**On a tile with frames** — a string `anim` property names the animation so
`tiny_play(game, obj, "player_walk")` can find it. Animations without a name still play by
themselves when the tile is part of a tile layer, exactly as Tiled previews them.

**On a layer**

| Property | Meaning |
| --- | --- |
| `collision` | Whether this tile layer blocks movement, and whether `tiny_flags_at` reads its tiles. If no layer says, they all do. |
| `sort` | Object layers: depth-sort before drawing. On by default except for platformers. |

Layers draw in the order Tiled lists them, **objects included**. That is how a foreground
layer ends up in front of the player: put the tile layer after the object layer. It is also
what the editor shows you, so what you see there is what you get.

Tiled's per-layer parallax and offsets work. So do horizontal, vertical and diagonal tile
flips — the baker bakes the flipped pixels as their own tile, so there is no flip logic at
runtime at all.

---

## Baking

```bash
python3 tools/tiny_bake.py maps/*.tmj -o generated/
```

Give it **every map at once**. That is the only way it can know which tiles are reachable.
It slices each tileset, applies whatever flip the map asked for, hashes the pixels, and
deduplicates. A tile no map uses is never emitted; a tile that happens to look like another
one is emitted once. On the demo that turns 5,377 tile references into 44 tiles:

```
tiny_bake: 5 maps, 5377 tile slices baked down to 44 unique tiles
tiny_bake: 20480 bytes of tile pixels + 19776 bytes of layer data = 40256 bytes of flash (3.1% of a 1310720 byte partition)
tiny_bake: 7 animations, 6 of them named and playable with tiny_play()
```

It writes these headers into the output directory:

| File | What it holds |
| --- | --- |
| `game_tiles.h` | The shared tile table and every animation |
| `game_sounds.h` | The shared sound bank |
| `map_<name>.h` | One per map: layers, objects, properties |
| `game_maps.h` | Includes the rest and defines `TINY_MAPS[]` and `TINY_SOUNDS[]` |

Include `game_maps.h` from exactly one translation unit — it defines data, not declarations.

Baking is **not** a build step, deliberately. Run it yourself when you change a map, the way
you would re-export anything else, and commit what it writes. That keeps Python out of the
build entirely: a checkout needs nothing but a C compiler, which matters most for the ESP32,
where the build already has enough moving parts.

Options: `--sounds FILE...` to bake audio (globs allowed), `--sound-rate HZ` to pick the
rate WAV audio is decoded to (default 22050, which is what the mixer plays), `--format argb`
if your pntr is built for that pixel format (the baker `#error`s at compile time if it does
not match), `--flash-budget` to size the report against your partition, `--strict` to fail on
warnings.

---

## Wiring it up

```c
#define PNTR_APP_IMPLEMENTATION
#include "pntr_app.h"

#define TINY_ENGINE_IMPLEMENTATION
#include "tinyengine.h"
#include "generated/game_maps.h"

static const tiny_game_def GAME = {
  .maps = TINY_MAPS,
  .map_count = TINY_MAP_COUNT,
  .start_map = "town",
  .object = Behaviour,   // the only required one
  .enter = Enter,        // after a map's objects are spawned
  .draw = Hud            // after the map, for the HUD
};

bool Init(pntr_app* app) {
  game = tiny_init(app, &GAME, &myState);  // also opens the audio device
  return game != NULL;
}

bool Update(pntr_app* app, pntr_image* screen) {
  tiny_update(game, pntr_app_delta_time(app));
  tiny_draw(game, screen);
  return true;
}

void Close(pntr_app* app) {
  tiny_unload(game);
}

pntr_app Main(int argc, char* argv[]) {
  return (pntr_app){ .width = 160, .height = 120, .title = "my game",
                     .init = Init, .update = Update, .close = Close, .fps = 60 };
}
```

pntr_app's web backend drops key presses when the app has no `event` hook, so `tiny_init`
installs an empty one if you did not set your own.

`tiny_init` makes one allocation, of `TINY_MAX_OBJECTS` (96 by default) objects at about 64
bytes each, and loads pntr's default font for `tiny_say`. The only other heap is the sound
effect cache, below.

---

## Sound

Audio is baked the same way art is: decoded at build time, `static const` in flash, and read
straight out of `.rodata` at runtime. Bake it with `--sounds`:

```bash
python3 tools/tiny_bake.py maps/*.tmj -o generated/ \
    --sounds 'sounds/*.wav' 'sounds/*.mod' 'sounds/*.rfx'
```

| Extension | Baked as | Played by |
| --- | --- | --- |
| `.wav` | Mono PCM, decoded and downmixed at bake time | the engine's mixer, directly |
| `.mod` | The module bytes, verbatim | [micromod](https://github.com/martincameron/micromod), vendored in `include/` |
| `.rfx` | The 96 byte `SfxParams` payload | [`pntr_app_sfx`](https://github.com/konsumer/pntr_app_sfx), synthesised on first play |

WAV audio is resampled to `--sound-rate` (22050 by default) so the mixer never has to
interpolate, and a stereo source is folded to mono — the CYD's speaker is mono anyway, and
half the bytes is half the flash. Nothing is compressed.

**One mixer, a swappable sink.** The engine mixes mono `int16` at `TINY_AUDIO_RATE`:

```c
tiny_sound_play(game, "jump", false);   // a WAV or SFX one-shot, or a looping MOD
tiny_sound_stop(game, "theme");
bool playing = tiny_sound_playing(game, "theme");

int16_t block[512];                     // only with TINY_NO_AUDIO_OUTPUT: your sink calls this
tiny_audio_render(game, block, 512);
```

A PCM or SFX sound takes one of `TINY_MAX_VOICES` (8 by default) mixer voices, so several
sound at once. A MOD takes the single music slot, because micromod is a whole-song player:
starting a second module replaces the first. The engine brings a sink for each pntr_app
backend it knows — a raylib `AudioStream` fed from the game loop, a web
`ScriptProcessorNode`, or the DAC on an ESP32 (`TINY_SPEAKER_PIN`, 26 by default) — and
opens it in `tiny_init`. Some boards only power their speaker amplifier while another pin is
held at a set level: define `TINY_SPEAKER_ENABLE_PIN` (and `TINY_SPEAKER_ENABLE_LEVEL`, low by
default) and the engine sets it before any sound plays. The single USB-C revision of the CYD
needs GPIO4 low; the demo's `platformio.ini` does that. Define `TINY_NO_AUDIO_OUTPUT` to bring your own and call
`tiny_audio_render` from it; the game does not change.

**Prefer `.rfx` for effects.** An `.rfx` is an [rFXGen](https://raylibtech.itch.io/rfxgen)
file: 104 bytes whatever the sound, against several KB for even a short WAV, and you change
it by opening it in rFXGen rather than re-recording anything. Every effect in the demo is
one. The engine synthesises an effect the first time it plays, keeps it at 22050 Hz, and
replays it from there, so it costs RAM for as long as the sound is (a tenth of a second is
about 4 KB) and nothing after the first play. `TINY_MAX_SFX` (16 by default) caps how many
different effects it keeps.

`pntr_app_sfx` is optional. `#define PNTR_APP_SFX_IMPLEMENTATION` and include
`pntr_app_sfx.h` **before** `tinyengine.h`, and an `.rfx` sound is synthesised on first play;
skip it and those sounds are simply skipped. `.wav` and `.mod` need nothing extra.

The vendored micromod allocates nothing — all of its state is static — so a MOD costs no
heap, just the bytes in flash.

---

## Building

**Web** — needs [emscripten](https://emscripten.org).

```bash
emcmake cmake -B build-web
cmake --build build-web
# build-web/examples/demo/demo.html, a single self-contained file
```

**Desktop** — fetches and builds raylib. The first configure takes about 20 seconds while
it downloads; after that it is cached.

```bash
cmake -B build
cmake --build build
./build/examples/demo/demo
```

To skip raylib entirely and build only the headless renderer and the tests, which needs no
window system and configures in a couple of seconds:

```bash
cmake -S . -B build -DTINYENGINE_BUILD_EXAMPLE_RAYLIB=OFF
```

**ESP32** — needs [PlatformIO](https://platformio.org). The default board profile is the
ESP32-2432S028R "Cheap Yellow Display": a 320x240 ILI9341 panel.

```bash
cd platform/esp32
pio run -e cyd -t upload -t monitor
```

The demo's screen is 160x120 and `pntr_app_esp32` upscales it 2x, because pntr allocates the
screen as one contiguous RGBA8888 buffer and 320x240 would need 307,200 bytes that a
WROOM-32 with no PSRAM cannot give out in one block. For another board, set
`PNTR_APP_ESP32_BOARD_CUSTOM` and the pins; see
[`pntr_app_esp32.h`](https://github.com/konsumer/pntr_app/blob/esp32/include/pntr_app_esp32.h).

Because nothing is decoded at runtime, the ESP32 build can drop pntr's image loader
entirely with `-D PNTR_NO_LOAD_IMAGE`, which is most of the flash an application like this
would otherwise spend.

The CYD's speaker port is a plain GPIO DAC, so the demo bit-bangs it on GPIO 26 from its own
task (`TINY_SPEAKER_PIN` in `demo.c`). That is the whole ESP32 audio backend, and it is
deliberately the same one-function seam as the other targets: point it at I2S, another pin,
or a different board and nothing in the engine changes.

---

## Checking what it draws

`tools/tiny_preview.c` runs the demo with no window and no platform backend at all — the
engine only asks pntr_app for its size, its input state and a logger, so those are stubbed
and the game is driven straight from `main()`. It renders to PNG, which means every map type
can be checked in CI on a machine with no display.

```bash
cmake --build build
./build/examples/demo/tiny_preview --map pyramid --frames 60 --hold up --out iso.png
ctest --test-dir build
```

Each case asserts the frame is not blank, that sprites were actually on screen, and that the
player did not come to rest **inside a solid tile**. That last one is what catches collision
regressions: the suite holds each direction against each map's geometry for 400 frames, which
is how you find something clipping through a wall. A screenshot will not tell you.

It also drives the mixer: it plays a looping MOD and two synthesised one-shot SFX,
renders real buffers, and checks the output is not silent, that the module is still looping
and that the one-shot stopped. That is a real signal, not a mock — the mixer runs for real,
only the audio device is absent.

---

## The demo

`examples/demo` is one game across all five map types, linked by portals.

| Map | Type | Shows |
| --- | --- | --- |
| `town` | top | Eased camera, a roof layer that draws over the player, an NPC, four doors |
| `cave` | top | `camera = room`, snapping across a 2x2 grid of rooms, chasing enemies, a hazard |
| `ledge` | plat | Gravity, jumping, one-way platforms, ledge-aware patrols, lava |
| `arena` | top | One screen, so the camera pins itself: an arcade screen with no extra setting |
| `sky` | plat | The same mode as `ledge` with `gravity = 0`, which makes it a side shooter |
| `pyramid` | iso | Diamond projection, painter's algorithm, blocks that occlude correctly |

Doors are one tile, and the spawn you return to sits one tile **beyond** the door on the
side you re-enter from. So walking through a door never drops you on top of one: you come
out already past it, still heading the way you were, and can keep walking. A portal also has
to see the player standing clear of it before it will fire again, which is the belt to that
braces.

Everything in `examples/demo/assets` is made in its own editor: open the `.tmj` maps and
`.tsj` tilesets in Tiled and the `.rfx` effects in [rFXGen](https://raylibtech.itch.io/rfxgen),
edit them, and re-bake with `npm run gen:maps`.

The music is `assets/theme.mod`, *Japanese Toycar* by Sami Jarvinen (Groo), kept as a real
ProTracker module to exercise micromod. Check its license before shipping the demo anywhere
you care about.

---

## Limits

- Orthogonal and isometric maps only. Staggered and hexagonal are rejected by the baker.
- Object rotation is ignored, and a polygon object collides as its bounding box.
- Tiled `class` properties are not flattened; the baker warns and skips them.
- Image layers are skipped. Convert them to a tile layer.
- One MOD plays at a time, because micromod is a single-song player.
- WAV is baked to mono PCM, uncompressed: a long track is a lot of flash. Prefer a MOD for
  music and `.rfx` for effects.

## License

zlib/libpng, the same as pntr and pntr_app.
