# tinyengine

A tiled game engine for [pntr_app](https://github.com/robloach/pntr_app). One codebase runs
on the web, on the desktop, and on an ESP32.

Maps are drawn in [Tiled](https://www.mapeditor.org/) and baked into C headers at build
time. Everything the baker writes is `static const`, so on a microcontroller it lands in
`.rodata` — memory mapped for reads — and pntr draws every tile straight out of flash. No
image is decoded at runtime, no map is parsed at runtime, and the only heap the engine takes
is one object array.

A game is three things:

1. Maps in Tiled, saved as JSON.
2. `tiny_bake.py` run over them, once, at build time.
3. One function that says how objects behave.

The demo is 6 maps, 47 unique tiles and about 350 lines of behaviour. On an ESP32 it is
**22 KB of RAM and 339 KB of flash**, maps included.

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

  if (tiny_is(self, "portal") && tiny_overlaps(self, game->player)) {
    tiny_goto_map(game, tiny_obj_str(self, "destination", NULL),
                        tiny_obj_str(self, "spawn", NULL));
  }
}
```

That is the whole interface. `tiny_is` matches the object's **Class** in Tiled, and
`tiny_obj_*` reads its custom properties.

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
```

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
| `collision` | Whether this tile layer blocks movement. If no layer says, they all do. |
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

It writes three kinds of header into the output directory:

| File | What it holds |
| --- | --- |
| `game_tiles.h` | The shared tile table and every animation |
| `map_<name>.h` | One per map: layers, objects, properties |
| `game_maps.h` | Includes the rest and defines `TINY_MAPS[]` |

Include `game_maps.h` from exactly one translation unit — it defines data, not declarations.

Baking is **not** a build step, deliberately. Run it yourself when you change a map, the way
you would re-export anything else, and commit what it writes. That keeps Python out of the
build entirely: a checkout needs nothing but a C compiler, which matters most for the ESP32,
where the build already has enough moving parts.

Options: `--format argb` if your pntr is built for that pixel format (the baker `#error`s
at compile time if it does not match), `--flash-budget` to size the report against your
partition, `--strict` to fail on warnings.

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
  .draw = Overlay        // after the map, for the HUD
};

bool Init(pntr_app* app) {
  game = tiny_init(app, &GAME, &myState);
  return game != NULL;
}

bool Update(pntr_app* app, pntr_image* screen) {
  tiny_update(game, pntr_app_delta_time(app));
  tiny_draw(game, screen);
  return true;
}

// Required, even empty. tinyengine polls input through pntr_app_key_down(), which reads
// the keysDown[] array that only pntr_app_process_event() maintains -- and pntr_app's web
// backend returns early on `app->event == NULL`, before it gets there. Leave this unset and
// the game runs but never responds to a key press in the browser.
void Event(pntr_app* app, pntr_app_event* event) { (void)app; (void)event; }

pntr_app Main(int argc, char* argv[]) {
  return (pntr_app){ .width = 160, .height = 120, .title = "my game",
                     .init = Init, .update = Update, .event = Event, .fps = 60 };
}
```

`tiny_init` makes one allocation, of `TINY_MAX_OBJECTS` (96 by default) objects at about 64
bytes each. Nothing else in the engine touches the heap.

---

## Building

**Web** — needs [emscripten](https://emscripten.org).

```bash
emcmake cmake -S . -B build-web
cmake --build build-web
# build-web/examples/demo/demo.html, a single self-contained file
```

**Desktop** — fetches and builds raylib. The first configure takes about 20 seconds while
it downloads; after that it is cached.

```bash
cmake -S . -B build
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

The art and the `.tmj` files are generated by `examples/demo/assets/make_assets.py` so they
can be regenerated and tweaked, but what it writes are ordinary Tiled files: open them,
edit them, re-bake.

---

## Limits

- Orthogonal and isometric maps only. Staggered and hexagonal are rejected by the baker.
- Object rotation is ignored, and a polygon object collides as its bounding box.
- Tiled `class` properties are not flattened; the baker warns and skips them.
- Image layers are skipped. Convert them to a tile layer.
- Sound is up to you: pntr_app has no audio backend on the ESP32 yet.

## License

zlib/libpng, the same as pntr and pntr_app.
