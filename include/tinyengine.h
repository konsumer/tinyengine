/**********************************************************************************************
 *
 *   tinyengine - A tiled game engine for pntr_app, targeting web, native and ESP32.
 *
 *   Maps are authored in Tiled and baked into C headers by tools/tiny_bake.py. Everything
 *   the baker emits is `static const`, so on an ESP32 it lands in .rodata and is drawn
 *   straight out of flash. The only heap the engine takes is one object array per map.
 *
 *   USAGE:
 *       #define PNTR_APP_IMPLEMENTATION
 *       #include "pntr_app.h"
 *
 *       #define TINY_ENGINE_IMPLEMENTATION
 *       #include "tinyengine.h"
 *       #include "generated/game_maps.h"
 *
 *   CONFIGURATION:
 *       TINY_ENGINE_IMPLEMENTATION  Emit the implementation in this translation unit.
 *       TINY_MAX_OBJECTS            Objects live at once, per map. Default 96.
 *       TINY_NO_STDIO               Never log through pntr_app_log.
 *
 *   LICENSE: zlib/libpng, same as pntr_app.
 *
 **********************************************************************************************/

#ifndef TINYENGINE_H__
#define TINYENGINE_H__

#include <stdbool.h>
#include <stdint.h>

#include "pntr_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TINY_API
#define TINY_API extern
#endif

#ifndef TINY_MAX_OBJECTS
/** Objects that can be live at once. Sets the size of the one heap allocation the engine makes. */
#define TINY_MAX_OBJECTS 96
#endif

/*--------------------------------------------------------------------------------------------
 * Baked data: everything below here is what tiny_bake.py emits, and it is all const.
 *------------------------------------------------------------------------------------------*/

/**
 * How a map is projected and what its physics are. Set with a `map_type` string in Tiled.
 *
 * There are only three, because most of what looks like a separate genre is really one of
 * these with a different map size or a different property:
 *
 *   - A one-screen `top` map is an arcade screen. The camera has nowhere to scroll, so it
 *     centres itself and stays there.
 *   - A set of one-screen `top` maps joined by portals is room-to-room exploration, the way
 *     the first Zelda works. Each room is its own map, so no room camera is needed.
 *   - A `plat` map with `gravity` set to 0 is a side-scrolling shooter rather than a
 *     platformer: nothing falls, and a jump has nothing to push against.
 *
 * The type picks the projection, the camera defaults and the collision rules. It does not
 * change how you write behaviour, so one game can mix all three and walk between them.
 */
typedef enum tiny_map_type {
  TINY_MAP_PLATFORMER = 0, /** Side-on. `plat`, `smb`. Gravity and one-way platforms. */
  TINY_MAP_TOPDOWN,        /** Top-down. `top`, `rpg`. No gravity. */
  TINY_MAP_ISOMETRIC,      /** Diamond projection, painter's algorithm. `iso`. */
  TINY_MAP_TYPE_LAST
} tiny_map_type;

typedef enum tiny_orientation {
  TINY_ORTHOGONAL = 0,
  TINY_ISOMETRIC = 1
} tiny_orientation;

typedef enum tiny_layer_kind {
  TINY_LAYER_TILE = 0,
  TINY_LAYER_OBJECT = 1
} tiny_layer_kind;

/**
 * Per-tile flags, from boolean custom properties on the tile in the Tiled tileset editor.
 *
 * A tile's flags are the OR of every layer that has a tile there, so a decorative layer
 * drawn over a wall does not stop the wall being solid.
 */
typedef enum tiny_tile_flag {
  TINY_TILE_SOLID = 1 << 0,    /** Blocks movement from every direction. Property: `solid`. */
  TINY_TILE_PLATFORM = 1 << 1, /** Blocks only downward movement. Property: `platform`. */
  TINY_TILE_LADDER = 1 << 2,   /** Property: `ladder`. The engine only reports it. */
  TINY_TILE_HAZARD = 1 << 3,   /** Property: `hazard`. The engine only reports it. */
  TINY_TILE_USER1 = 1 << 4,    /** Property: `user1`, through `user4`. Yours to define. */
  TINY_TILE_USER2 = 1 << 5,
  TINY_TILE_USER3 = 1 << 6,
  TINY_TILE_USER4 = 1 << 7
} tiny_tile_flag;

typedef enum tiny_object_shape {
  TINY_SHAPE_RECT = 0,
  TINY_SHAPE_POINT,
  TINY_SHAPE_ELLIPSE,
  TINY_SHAPE_POLYGON,
  TINY_SHAPE_TILE /** An object placed from a tileset, so it has a sprite. */
} tiny_object_shape;

typedef enum tiny_prop_type {
  TINY_PROP_INT = 0,
  TINY_PROP_FLOAT,
  TINY_PROP_BOOL,
  TINY_PROP_STRING,
  TINY_PROP_COLOR,
  TINY_PROP_OBJECT /** A Tiled object reference, stored as the target object's id. */
} tiny_prop_type;

/** One Tiled custom property. `hash` is FNV-1a of `name`, so lookups rarely reach strcmp. */
typedef struct tiny_prop {
  const char* name;
  uint32_t hash;
  uint8_t type;
  union {
    int32_t i;
    float f;
    const char* s;
  } as;
} tiny_prop;

/** A property bag, hanging off a map, a layer, an object, or a tile. */
typedef struct tiny_props {
  const tiny_prop* items;
  uint16_t count;
} tiny_props;

typedef struct tiny_anim_frame {
  uint16_t tile; /** Index into tiny_map::tiles. */
  uint16_t ms;   /** How long to hold this frame. */
} tiny_anim_frame;

/**
 * A named animation, from a tile's frames in the Tiled tileset editor.
 *
 * Give the first tile of the animation a string property `anim` to name it, then play it
 * on an object with tiny_play(). Animations without a name still run by themselves when
 * the tile is drawn as part of a tile layer, which is what Tiled shows you in the editor.
 */
typedef struct tiny_anim {
  const char* name; /** NULL when the animation is unnamed. */
  uint32_t hash;
  const tiny_anim_frame* frames;
  uint16_t count;
  uint16_t total_ms;
} tiny_anim;

/**
 * One baked tile: its pixels, what it does, and what it animates into.
 *
 * The baker slices the tileset, applies any flip the map asked for, and hashes the result,
 * so identical and flipped-identical tiles share one entry and unreferenced tiles cost
 * nothing. Index 0 is always the empty tile.
 */
typedef struct tiny_tile {
  pntr_image image; /** `image.data` points at flash. Read only: never draw *into* this. */
  uint8_t flags;    /** tiny_tile_flag bits. */
  uint16_t anim;    /** Index into tiny_map::anims, plus one. 0 means the tile is static. */
  int16_t offset_x; /** The tileset's Drawing Offset, which isometric tilesets lean on. */
  int16_t offset_y;
  tiny_props props;
} tiny_tile;

typedef struct tiny_layer {
  const char* name;
  uint32_t hash;
  uint8_t kind; /** tiny_layer_kind */
  bool visible;
  bool sort;      /** Object layer only: depth-sort before drawing. */
  bool collision; /** Tile layer only: contributes to tiny_solid(). Property: `collision`. */
  uint16_t width, height;
  float offset_x, offset_y;
  float parallax_x, parallax_y;
  const uint16_t* tiles; /** width*height indices into tiny_map::tiles. NULL for object layers. */
  tiny_props props;
} tiny_layer;

/**
 * A placed object, exactly as Tiled saved it.
 *
 * Positions are in world units: pixels on an orthogonal map, tiles on an isometric one.
 * The baker does that conversion so behaviour code never has to care which it is.
 */
typedef struct tiny_object_def {
  const char* name;
  const char* type; /** Tiled's "Class" field. */
  uint32_t name_hash;
  uint32_t type_hash;
  uint16_t id; /** Tiled's object id, which is what object-typed properties point at. */
  float x, y;  /** Top-left, in world units. Tile objects are converted from bottom-left. */
  float w, h;
  uint16_t tile; /** Index into tiny_map::tiles. 0 means the object has no sprite. */
  uint8_t shape; /** tiny_object_shape */
  uint8_t layer; /** Which layer placed it, so it draws in the right order. */
  bool visible;
  tiny_props props;
} tiny_object_def;

typedef struct tiny_map {
  const char* name;
  uint32_t hash;
  uint8_t type;        /** tiny_map_type */
  uint8_t orientation; /** tiny_orientation */
  uint16_t width, height;
  uint16_t tile_w, tile_h;
  pntr_color background;
  const tiny_layer* layers;
  uint16_t layer_count;
  const tiny_object_def* objects;
  uint16_t object_count;
  const tiny_tile* tiles; /** Shared by every map in the project. Index 0 is empty. */
  uint16_t tile_count;
  const tiny_anim* anims;
  uint16_t anim_count;
  tiny_props props;
} tiny_map;

/*--------------------------------------------------------------------------------------------
 * Runtime state: this is the part that lives in RAM.
 *------------------------------------------------------------------------------------------*/

/** Which sides of an object were blocked by the last tiny_move(). */
typedef enum tiny_hit {
  TINY_HIT_LEFT = 1 << 0,
  TINY_HIT_RIGHT = 1 << 1,
  TINY_HIT_UP = 1 << 2,
  TINY_HIT_DOWN = 1 << 3
} tiny_hit;

/**
 * A live object.
 *
 * `def` is the flash-resident Tiled data it came from and never changes. Everything else is
 * yours to move around. `state`, `timer` and `user` are untouched by the engine.
 */
typedef struct tiny_object {
  const tiny_object_def* def;

  float x, y;   /** Top-left, in world units. */
  float w, h;   /** Collision box, seeded from the Tiled object. */
  float vx, vy; /** Velocity, in world units per second. Only tiny_move() reads it. */

  uint16_t tile; /** Tile drawn this frame. 0 draws nothing. */
  uint16_t anim; /** Playing animation, plus one. 0 means the tile is held. */
  uint16_t frame;
  float frame_timer;

  bool active;    /** Cleared objects are skipped and their slot is reused. */
  bool visible;   /** Skips drawing, but behaviour still runs. */
  bool flip_x;    /** Mirror horizontally when drawing. */
  bool flip_y;    /** Mirror vertically when drawing. */
  bool on_ground; /** Set by tiny_move() when downward motion was blocked. */
  uint8_t hit;    /** tiny_hit bits from the last tiny_move(). */
  uint8_t layer;  /** Draw order. Seeded from def->layer. */

  int32_t state; /** Yours. */
  float timer;   /** Yours. */
  void* user;    /** Yours. */
} tiny_object;

typedef enum tiny_camera_mode {
  TINY_CAMERA_FOLLOW = 0, /** Tracks the player, eased, clamped to the map. */
  TINY_CAMERA_ROOM,       /** Snaps a screen at a time, the way the first Zelda does. */
  TINY_CAMERA_FIXED       /** Never moves. Set x/y yourself if you want it somewhere else. */
} tiny_camera_mode;

typedef struct tiny_camera {
  float x, y; /** Top-left of the view, in screen-projected pixels. */
  uint8_t mode;
  float ease;     /** 0 snaps, 1 never arrives. Around 0.15 feels right. */
  float deadzone; /** Fraction of the screen the player moves in before the camera does. */
  bool clamp;     /** Keep the view inside the map. */
} tiny_camera;

typedef struct tiny_game tiny_game;

/**
 * The game you are writing.
 *
 * Only `object` is required. It runs once per live object per frame, and inside it you can
 * look at any other object, the map, the camera and the tiles.
 */
typedef struct tiny_game_def {
  const tiny_map* const* maps;
  uint16_t map_count;
  const char* start_map; /** NULL starts on maps[0]. */

  /** Behaviour. Called for every active object, every frame. */
  void (*object)(tiny_game* game, tiny_object* self, float dt);

  void (*enter)(tiny_game* game);                    /** After a map's objects are spawned. */
  void (*leave)(tiny_game* game);                    /** Before a map is torn down. */
  void (*draw)(tiny_game* game, pntr_image* screen); /** After the map, for HUD and overlays. */
} tiny_game_def;

struct tiny_game {
  const tiny_game_def* def;
  const tiny_map* map;

  tiny_object* objects;
  uint16_t object_count; /** High-water mark of used slots, not the number alive. */
  uint16_t object_cap;
  tiny_object* player; /** The object whose class or name is "player". May be NULL. */

  tiny_camera camera;
  int screen_w, screen_h;

  float gravity;   /** World units per second squared. Seeded from the map's `gravity`. */
  float time;      /** Seconds since the map was entered. */
  float anim_time; /** Clock the tile animations run on, so identical tiles stay in step. */

  pntr_app* app; /** Set by tiny_init(), so behaviour code can read input. */
  void* user;    /** Yours. */

  const tiny_map* pending_map; /** tiny_goto_map() defers until the frame ends. */
  const char* pending_spawn;
};

/*--------------------------------------------------------------------------------------------
 * API
 *------------------------------------------------------------------------------------------*/

/**
 * Create a game. Allocates one object array. Returns NULL if that allocation fails.
 *
 * `user` becomes tiny_game::user before the first map loads, so def->enter can already see
 * it. The engine never touches what it points at.
 */
TINY_API tiny_game* tiny_init(pntr_app* app, const tiny_game_def* def, void* user);
TINY_API void tiny_unload(tiny_game* game);

/** Step behaviour, animation, the camera, and any pending map change. */
TINY_API void tiny_update(tiny_game* game, float dt);

/** Draw the map and its objects, then call def->draw for the overlay. */
TINY_API void tiny_draw(tiny_game* game, pntr_image* screen);

/* -- maps ------------------------------------------------------------------------------- */

TINY_API const tiny_map* tiny_find_map(tiny_game* game, const char* name);

/**
 * Switch maps at the end of this frame.
 *
 * `spawn` names an object on the destination map to place the player at, which is how a
 * portal lands you somewhere sensible. Pass NULL to use the destination's own player object.
 */
TINY_API bool tiny_goto_map(tiny_game* game, const char* name, const char* spawn);

/* -- objects ---------------------------------------------------------------------------- */

/**
 * How many object slots to walk. Some of them may be dead, so check `active`.
 *
 *     for (int i = 0; i < tiny_object_count(game); i++) {
 *       tiny_object* other = tiny_object_at_index(game, i);
 *       if (other->active) { ... }
 *     }
 */
TINY_API int tiny_object_count(tiny_game* game);

/** The object in one slot, or NULL when the index is out of range. */
TINY_API tiny_object* tiny_object_at_index(tiny_game* game, int index);

TINY_API tiny_object* tiny_find(tiny_game* game, const char* name);

/** Walk every object of a class. Pass NULL to start, then the previous result. */
TINY_API tiny_object* tiny_find_next(tiny_game* game, const char* type, tiny_object* after);

/** True when the object's Tiled class matches. */
TINY_API bool tiny_is(const tiny_object* obj, const char* type);

/** Add an object at runtime, copying an existing object's definition. Returns NULL when full. */
TINY_API tiny_object* tiny_spawn(tiny_game* game, const tiny_object_def* def, float x, float y);
TINY_API void tiny_kill(tiny_game* game, tiny_object* obj);

TINY_API bool tiny_overlaps(const tiny_object* a, const tiny_object* b);

/** The nearest overlapping object of a class, or NULL. Pass NULL for `type` to match any. */
TINY_API tiny_object* tiny_overlapping(tiny_game* game, const tiny_object* obj, const char* type);

/* -- animation -------------------------------------------------------------------------- */

/** Play a named animation, from a tile's `anim` property in the tileset. */
TINY_API bool tiny_play(tiny_game* game, tiny_object* obj, const char* name);

/**
 * Hold one frame of a named animation without running it.
 *
 * Frame 0 of a walk cycle is the standing pose, so this is how something stops animating
 * when it stops moving: tiny_play() while there is input, tiny_pose(..., 0) when there is not.
 */
TINY_API bool tiny_pose(tiny_game* game, tiny_object* obj, const char* name, int frame);

TINY_API void tiny_stop(tiny_object* obj);
TINY_API bool tiny_playing(tiny_game* game, const tiny_object* obj, const char* name);

/* -- the view --------------------------------------------------------------------------- */

/** What the camera can see, in projected screen pixels. */
TINY_API pntr_rectangle tiny_view(tiny_game* game);

/** The rectangle of tiles the view touches. Useful for spawning just off screen. */
TINY_API pntr_rectangle tiny_view_tiles(tiny_game* game);

TINY_API bool tiny_on_screen(tiny_game* game, const tiny_object* obj);
TINY_API void tiny_world_to_screen(tiny_game* game, float wx, float wy, int* sx, int* sy);
TINY_API void tiny_screen_to_world(tiny_game* game, int sx, int sy, float* wx, float* wy);

/* -- tiles ------------------------------------------------------------------------------ */

/** Tile coordinates for a world position. Isometric world units already are tiles. */
TINY_API void tiny_world_to_tile(tiny_game* game, float wx, float wy, int* tx, int* ty);
TINY_API void tiny_tile_to_world(tiny_game* game, int tx, int ty, float* wx, float* wy);

/** The topmost non-empty tile index at a tile coordinate, across every visible layer. */
TINY_API uint16_t tiny_tile_at(tiny_game* game, int tx, int ty);

/** Every layer's flags OR'd together at a tile coordinate. Out of bounds reads as solid. */
TINY_API uint8_t tiny_flags_at(tiny_game* game, int tx, int ty);

TINY_API bool tiny_solid(tiny_game* game, float wx, float wy);
/**
 * Whether any solid tile overlaps a box, given in world units.
 *
 * Loose floats rather than a pntr_rectangle because world units are fractional: on an
 * isometric map they are tiles, so an object sits at something like (3.4, 7.2) and rounding
 * it to a whole tile would test the wrong cells.
 */
TINY_API bool tiny_solid_rect(tiny_game* game, float x, float y, float w, float h);

/**
 * Move an object against the collision layers, one axis at a time.
 *
 * Sets `hit`, and `on_ground` on a platformer map. One-way platforms are only solid when
 * the object is moving down and started above them.
 */
TINY_API void tiny_move(tiny_game* game, tiny_object* obj, float dx, float dy);

/** Integrate velocity for a frame, applying gravity first on a platformer map. */
TINY_API void tiny_step(tiny_game* game, tiny_object* obj, float dt);

/* -- properties ------------------------------------------------------------------------- */

TINY_API const tiny_prop* tiny_prop_find(const tiny_props* props, const char* name);
TINY_API int tiny_prop_int(const tiny_props* props, const char* name, int fallback);
TINY_API float tiny_prop_float(const tiny_props* props, const char* name, float fallback);
TINY_API bool tiny_prop_bool(const tiny_props* props, const char* name, bool fallback);
TINY_API const char* tiny_prop_str(const tiny_props* props, const char* name, const char* fallback);
TINY_API pntr_color tiny_prop_color(const tiny_props* props, const char* name, pntr_color fallback);

/** Read a property off an object's Tiled definition. */
#define tiny_obj_int(obj, name, fallback) tiny_prop_int(&(obj)->def->props, (name), (fallback))
#define tiny_obj_float(obj, name, fallback) tiny_prop_float(&(obj)->def->props, (name), (fallback))
#define tiny_obj_bool(obj, name, fallback) tiny_prop_bool(&(obj)->def->props, (name), (fallback))
#define tiny_obj_str(obj, name, fallback) tiny_prop_str(&(obj)->def->props, (name), (fallback))

/** Read a property off the current map. */
#define tiny_map_int(game, name, fallback) tiny_prop_int(&(game)->map->props, (name), (fallback))
#define tiny_map_float(game, name, fallback) tiny_prop_float(&(game)->map->props, (name), (fallback))
#define tiny_map_bool(game, name, fallback) tiny_prop_bool(&(game)->map->props, (name), (fallback))
#define tiny_map_str(game, name, fallback) tiny_prop_str(&(game)->map->props, (name), (fallback))

/** FNV-1a, the hash the baker precomputes for names. Exposed so you can precompute too. */
TINY_API uint32_t tiny_hash(const char* text);

#ifdef __cplusplus
}
#endif

#endif  // TINYENGINE_H__

/**********************************************************************************************
 * Implementation
 **********************************************************************************************/

#ifdef TINY_ENGINE_IMPLEMENTATION
#ifndef TINY_ENGINE_IMPLEMENTATION_ONCE
#define TINY_ENGINE_IMPLEMENTATION_ONCE

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TINY_MEMCPY
#include <string.h>
#define TINY_MEMCPY(dst, src, n) memcpy((dst), (src), (n))
#define TINY_MEMSET(dst, v, n) memset((dst), (v), (n))
#define TINY_STRCMP(a, b) strcmp((a), (b))
#endif

#include <math.h>

#ifndef TINY_NO_STDIO
#include <stdarg.h>
#include <stdio.h>

/*
 * pntr_app's own formatting logger only exists under PNTR_ENABLE_VARGS, and the engine
 * should not force an application to turn that on, so it formats its own line and hands
 * the finished string to the logger that is always there.
 */
static void _tiny_log(pntr_app_log_type type, const char* format, ...) {
  char message[160];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  pntr_app_log(type, message);
}
#endif

#define TINY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define TINY_MAX(a, b) ((a) > (b) ? (a) : (b))

/* How many tiles past the view to still consider, for images that overhang their own cell. */
#define TINY_CULL_MARGIN 4

uint32_t tiny_hash(const char* text) {
  uint32_t hash = 2166136261u;
  if (text == NULL) {
    return 0;
  }
  while (*text) {
    hash ^= (uint32_t)(unsigned char)*text++;
    hash *= 16777619u;
  }
  return hash;
}

/*-- properties -----------------------------------------------------------------------------*/

const tiny_prop* tiny_prop_find(const tiny_props* props, const char* name) {
  if (props == NULL || props->items == NULL || name == NULL) {
    return NULL;
  }
  uint32_t hash = tiny_hash(name);
  for (uint16_t i = 0; i < props->count; i++) {
    const tiny_prop* prop = &props->items[i];
    /* The hash is precomputed at bake time, so this almost always settles it. */
    if (prop->hash == hash && TINY_STRCMP(prop->name, name) == 0) {
      return prop;
    }
  }
  return NULL;
}

int tiny_prop_int(const tiny_props* props, const char* name, int fallback) {
  const tiny_prop* prop = tiny_prop_find(props, name);
  if (prop == NULL) {
    return fallback;
  }
  if (prop->type == TINY_PROP_FLOAT) {
    return (int)prop->as.f;
  }
  return (int)prop->as.i;
}

float tiny_prop_float(const tiny_props* props, const char* name, float fallback) {
  const tiny_prop* prop = tiny_prop_find(props, name);
  if (prop == NULL) {
    return fallback;
  }
  if (prop->type == TINY_PROP_FLOAT) {
    return prop->as.f;
  }
  return (float)prop->as.i;
}

bool tiny_prop_bool(const tiny_props* props, const char* name, bool fallback) {
  const tiny_prop* prop = tiny_prop_find(props, name);
  if (prop == NULL) {
    return fallback;
  }
  if (prop->type == TINY_PROP_FLOAT) {
    return prop->as.f != 0.0f;
  }
  return prop->as.i != 0;
}

const char* tiny_prop_str(const tiny_props* props, const char* name, const char* fallback) {
  const tiny_prop* prop = tiny_prop_find(props, name);
  if (prop == NULL || (prop->type != TINY_PROP_STRING && prop->type != TINY_PROP_COLOR)) {
    return fallback;
  }
  return prop->as.s;
}

pntr_color tiny_prop_color(const tiny_props* props, const char* name, pntr_color fallback) {
  const tiny_prop* prop = tiny_prop_find(props, name);
  if (prop == NULL || prop->type != TINY_PROP_COLOR) {
    return fallback;
  }
  /* The baker stores colours already packed for the active pntr pixel format. */
  return pntr_get_color((uint32_t)prop->as.i);
}

/*-- projection -----------------------------------------------------------------------------*/

/* Isometric maps put the origin so that the leftmost tile column starts at screen x 0. */
static float _tiny_iso_origin(const tiny_map* map) {
  return (float)map->height * (float)map->tile_w * 0.5f;
}

/* World units to unscrolled screen pixels. Orthogonal world units already are pixels. */
static void _tiny_project(const tiny_map* map, float wx, float wy, float* px, float* py) {
  if (map->orientation == TINY_ISOMETRIC) {
    *px = (wx - wy) * (float)map->tile_w * 0.5f + _tiny_iso_origin(map);
    *py = (wx + wy) * (float)map->tile_h * 0.5f;
  } else {
    *px = wx;
    *py = wy;
  }
}

static void _tiny_unproject(const tiny_map* map, float px, float py, float* wx, float* wy) {
  if (map->orientation == TINY_ISOMETRIC) {
    float dx = (px - _tiny_iso_origin(map)) / (float)map->tile_w;
    float dy = py / (float)map->tile_h;
    *wx = dy + dx;
    *wy = dy - dx;
  } else {
    *wx = px;
    *wy = py;
  }
}

/* The full map, in projected screen pixels. */
static void _tiny_map_pixel_size(const tiny_map* map, float* w, float* h) {
  if (map->orientation == TINY_ISOMETRIC) {
    *w = (float)(map->width + map->height) * (float)map->tile_w * 0.5f;
    *h = (float)(map->width + map->height) * (float)map->tile_h * 0.5f;
  } else {
    *w = (float)(map->width * map->tile_w);
    *h = (float)(map->height * map->tile_h);
  }
}

void tiny_world_to_screen(tiny_game* game, float wx, float wy, int* sx, int* sy) {
  float px, py;
  _tiny_project(game->map, wx, wy, &px, &py);
  if (sx != NULL) {
    *sx = (int)floorf(px - game->camera.x);
  }
  if (sy != NULL) {
    *sy = (int)floorf(py - game->camera.y);
  }
}

void tiny_screen_to_world(tiny_game* game, int sx, int sy, float* wx, float* wy) {
  _tiny_unproject(game->map, (float)sx + game->camera.x, (float)sy + game->camera.y, wx, wy);
}

void tiny_world_to_tile(tiny_game* game, float wx, float wy, int* tx, int* ty) {
  const tiny_map* map = game->map;
  if (map->orientation == TINY_ISOMETRIC) {
    /* Isometric world units are tiles already. */
    if (tx != NULL) {
      *tx = (int)floorf(wx);
    }
    if (ty != NULL) {
      *ty = (int)floorf(wy);
    }
    return;
  }
  if (tx != NULL) {
    *tx = (int)floorf(wx / (float)map->tile_w);
  }
  if (ty != NULL) {
    *ty = (int)floorf(wy / (float)map->tile_h);
  }
}

void tiny_tile_to_world(tiny_game* game, int tx, int ty, float* wx, float* wy) {
  const tiny_map* map = game->map;
  float scale_x = map->orientation == TINY_ISOMETRIC ? 1.0f : (float)map->tile_w;
  float scale_y = map->orientation == TINY_ISOMETRIC ? 1.0f : (float)map->tile_h;
  if (wx != NULL) {
    *wx = (float)tx * scale_x;
  }
  if (wy != NULL) {
    *wy = (float)ty * scale_y;
  }
}

/*-- tiles ----------------------------------------------------------------------------------*/

static uint16_t _tiny_layer_tile(const tiny_layer* layer, int tx, int ty) {
  if (layer->tiles == NULL || tx < 0 || ty < 0 || tx >= (int)layer->width || ty >= (int)layer->height) {
    return 0;
  }
  return layer->tiles[(size_t)ty * layer->width + tx];
}

uint16_t tiny_tile_at(tiny_game* game, int tx, int ty) {
  const tiny_map* map = game->map;
  /* Topmost wins, so walk the layers backwards. */
  for (int i = (int)map->layer_count - 1; i >= 0; i--) {
    const tiny_layer* layer = &map->layers[i];
    if (layer->kind != TINY_LAYER_TILE || !layer->visible) {
      continue;
    }
    uint16_t tile = _tiny_layer_tile(layer, tx, ty);
    if (tile != 0) {
      return tile;
    }
  }
  return 0;
}

uint8_t tiny_flags_at(tiny_game* game, int tx, int ty) {
  const tiny_map* map = game->map;
  if (tx < 0 || ty < 0 || tx >= (int)map->width || ty >= (int)map->height) {
    /* Outside the map is a wall, which keeps everything on screen without extra checks. */
    return TINY_TILE_SOLID;
  }
  uint8_t flags = 0;
  for (uint16_t i = 0; i < map->layer_count; i++) {
    const tiny_layer* layer = &map->layers[i];
    if (layer->kind != TINY_LAYER_TILE || !layer->collision) {
      continue;
    }
    uint16_t tile = _tiny_layer_tile(layer, tx, ty);
    if (tile != 0) {
      flags |= map->tiles[tile].flags;
    }
  }
  return flags;
}

bool tiny_solid(tiny_game* game, float wx, float wy) {
  int tx, ty;
  tiny_world_to_tile(game, wx, wy, &tx, &ty);
  return (tiny_flags_at(game, tx, ty) & TINY_TILE_SOLID) != 0;
}

/* Sample every tile the box touches. Boxes are small, so the loop is a handful of steps. */
static bool _tiny_box_flags(tiny_game* game, float x, float y, float w, float h, uint8_t mask) {
  int x0, y0, x1, y1;
  /* A hair inside the far edge, so a box resting exactly on a boundary is not inside it. */
  tiny_world_to_tile(game, x, y, &x0, &y0);
  tiny_world_to_tile(game, x + w - 0.001f, y + h - 0.001f, &x1, &y1);
  for (int ty = y0; ty <= y1; ty++) {
    for (int tx = x0; tx <= x1; tx++) {
      if (tiny_flags_at(game, tx, ty) & mask) {
        return true;
      }
    }
  }
  return false;
}

bool tiny_solid_rect(tiny_game* game, float x, float y, float w, float h) {
  return _tiny_box_flags(game, x, y, w, h, TINY_TILE_SOLID);
}

/*-- movement -------------------------------------------------------------------------------*/

/*
 * One-way platforms are only solid to something falling onto them, so they are checked
 * against where the object was rather than where it is going.
 */
static bool _tiny_blocked(tiny_game* game, float x, float y, float w, float h, float dy, float from_bottom) {
  if (_tiny_box_flags(game, x, y, w, h, TINY_TILE_SOLID)) {
    return true;
  }
  if (dy <= 0.0f || game->map->type != TINY_MAP_PLATFORMER) {
    return false;
  }

  int x0, y0, x1, y1;
  tiny_world_to_tile(game, x, y, &x0, &y0);
  tiny_world_to_tile(game, x + w - 0.001f, y + h - 0.001f, &x1, &y1);
  for (int tx = x0; tx <= x1; tx++) {
    if ((tiny_flags_at(game, tx, y1) & TINY_TILE_PLATFORM) == 0) {
      continue;
    }
    float top;
    tiny_tile_to_world(game, tx, y1, NULL, &top);
    /* Only catch it if the object's feet were above the platform before this step. */
    if (from_bottom <= top + 0.001f) {
      return true;
    }
  }
  return false;
}

/*
 * Slide as far along one axis as the map allows. Returns true if something stopped it, and
 * writes how far it actually got.
 *
 * The coarse loop creeps forward a pixel at a time, then a short bisection closes the last
 * fraction of a pixel. That final step matters more than it looks: stopping on a whole-pixel
 * boundary leaves a falling object resting up to a pixel above the ground, so gravity
 * re-accumulates every frame and the sprite visibly shivers where it stands.
 */
static bool _tiny_axis(tiny_game* game, tiny_object* obj, bool horizontal, float delta, float unit,
                       float from_bottom, float* moved) {
  float ox = horizontal ? delta : 0.0f;
  float oy = horizontal ? 0.0f : delta;
  /* Only vertical travel can land on a one-way platform. */
  float fall = horizontal ? 0.0f : delta;

  if (!_tiny_blocked(game, obj->x + ox, obj->y + oy, obj->w, obj->h, fall, from_bottom)) {
    *moved = delta;
    return false;
  }

  float step = delta > 0.0f ? unit : -unit;
  float clear = 0.0f;
  while (fabsf(clear + step) <= fabsf(delta)) {
    float probe = clear + step;
    ox = horizontal ? probe : 0.0f;
    oy = horizontal ? 0.0f : probe;
    if (_tiny_blocked(game, obj->x + ox, obj->y + oy, obj->w, obj->h, fall, from_bottom)) {
      break;
    }
    clear = probe;
  }

  /* `clear` is known free and `delta` is known blocked, so the contact is between them. */
  float stuck = delta;
  for (int i = 0; i < 4; i++) {
    float mid = (clear + stuck) * 0.5f;
    ox = horizontal ? mid : 0.0f;
    oy = horizontal ? 0.0f : mid;
    if (_tiny_blocked(game, obj->x + ox, obj->y + oy, obj->w, obj->h, fall, from_bottom)) {
      stuck = mid;
    } else {
      clear = mid;
    }
  }

  *moved = clear;
  return true;
}

void tiny_move(tiny_game* game, tiny_object* obj, float dx, float dy) {
  obj->hit = 0;
  if (game->map == NULL) {
    obj->x += dx;
    obj->y += dy;
    return;
  }

  /*
   * A step is one screen pixel expressed in world units. That is 1.0 on an orthogonal map,
   * where world units are pixels, but on an isometric map a world unit is a whole tile, so
   * stepping by 1.0 there would mean an object either clears a wall completely or never
   * budges against it.
   */
  float unit = game->map->orientation == TINY_ISOMETRIC ? 1.0f / (float)game->map->tile_h : 1.0f;
  float bottom_before = obj->y + obj->h;
  float moved;

  if (dx != 0.0f) {
    if (_tiny_axis(game, obj, true, dx, unit, bottom_before, &moved)) {
      obj->hit |= dx > 0.0f ? TINY_HIT_RIGHT : TINY_HIT_LEFT;
      obj->vx = 0.0f;
    }
    obj->x += moved;
  }

  if (dy != 0.0f) {
    if (_tiny_axis(game, obj, false, dy, unit, bottom_before, &moved)) {
      obj->hit |= dy > 0.0f ? TINY_HIT_DOWN : TINY_HIT_UP;
      obj->vy = 0.0f;
    }
    obj->y += moved;
  }

  /*
   * Only a move that actually had vertical travel can say anything about standing on
   * something. Otherwise a game that resolves its axes in two calls, or nudges an object
   * sideways, would drop `on_ground` for a frame and lose the jump.
   */
  if (game->map->type == TINY_MAP_PLATFORMER && dy != 0.0f) {
    obj->on_ground = (obj->hit & TINY_HIT_DOWN) != 0;
  }
}

void tiny_step(tiny_game* game, tiny_object* obj, float dt) {
  if (game->map != NULL && game->map->type == TINY_MAP_PLATFORMER) {
    obj->vy += game->gravity * dt;
  }
  tiny_move(game, obj, obj->vx * dt, obj->vy * dt);
}

/*-- objects --------------------------------------------------------------------------------*/

int tiny_object_count(tiny_game* game) {
  return game == NULL ? 0 : (int)game->object_count;
}

tiny_object* tiny_object_at_index(tiny_game* game, int index) {
  if (index < 0 || index >= (int)game->object_count) {
    return NULL;
  }
  return &game->objects[index];
}

bool tiny_is(const tiny_object* obj, const char* type) {
  if (obj == NULL || obj->def == NULL || obj->def->type == NULL || type == NULL) {
    return false;
  }
  return obj->def->type_hash == tiny_hash(type) && TINY_STRCMP(obj->def->type, type) == 0;
}

tiny_object* tiny_find(tiny_game* game, const char* name) {
  if (name == NULL) {
    return NULL;
  }
  uint32_t hash = tiny_hash(name);
  for (uint16_t i = 0; i < game->object_count; i++) {
    tiny_object* obj = &game->objects[i];
    if (!obj->active || obj->def == NULL || obj->def->name == NULL) {
      continue;
    }
    if (obj->def->name_hash == hash && TINY_STRCMP(obj->def->name, name) == 0) {
      return obj;
    }
  }
  return NULL;
}

tiny_object* tiny_find_next(tiny_game* game, const char* type, tiny_object* after) {
  uint16_t start = 0;
  if (after != NULL) {
    start = (uint16_t)(after - game->objects) + 1;
  }
  for (uint16_t i = start; i < game->object_count; i++) {
    tiny_object* obj = &game->objects[i];
    if (obj->active && (type == NULL || tiny_is(obj, type))) {
      return obj;
    }
  }
  return NULL;
}

bool tiny_overlaps(const tiny_object* a, const tiny_object* b) {
  if (a == NULL || b == NULL || a == b || !a->active || !b->active) {
    return false;
  }
  return a->x < b->x + b->w && a->x + a->w > b->x && a->y < b->y + b->h && a->y + a->h > b->y;
}

tiny_object* tiny_overlapping(tiny_game* game, const tiny_object* obj, const char* type) {
  if (obj == NULL) {
    return NULL;
  }
  for (uint16_t i = 0; i < game->object_count; i++) {
    tiny_object* other = &game->objects[i];
    if (other == obj || !other->active) {
      continue;
    }
    if (type != NULL && !tiny_is(other, type)) {
      continue;
    }
    if (tiny_overlaps(obj, other)) {
      return other;
    }
  }
  return NULL;
}

static void _tiny_init_object(tiny_game* game, tiny_object* obj, const tiny_object_def* def, float x, float y) {
  TINY_MEMSET(obj, 0, sizeof(tiny_object));
  obj->def = def;
  obj->x = x;
  obj->y = y;
  obj->active = true;
  obj->visible = def != NULL ? def->visible : true;

  if (def != NULL) {
    obj->w = def->w;
    obj->h = def->h;
    obj->tile = def->tile;
    obj->layer = def->layer;
  }

  /* A point object has no size, so give it a one-unit box to collide and overlap with. */
  if (obj->w <= 0.0f || obj->h <= 0.0f) {
    const tiny_map* map = game->map;
    bool iso = map != NULL && map->orientation == TINY_ISOMETRIC;
    obj->w = obj->w > 0.0f ? obj->w : (iso ? 1.0f : (float)(map != NULL ? map->tile_w : 1));
    obj->h = obj->h > 0.0f ? obj->h : (iso ? 1.0f : (float)(map != NULL ? map->tile_h : 1));
  }
}

tiny_object* tiny_spawn(tiny_game* game, const tiny_object_def* def, float x, float y) {
  for (uint16_t i = 0; i < game->object_cap; i++) {
    if (game->objects[i].active) {
      continue;
    }
    _tiny_init_object(game, &game->objects[i], def, x, y);
    if (i >= game->object_count) {
      game->object_count = (uint16_t)(i + 1);
    }
    return &game->objects[i];
  }
#ifndef TINY_NO_STDIO
  _tiny_log(PNTR_APP_LOG_WARNING, "tinyengine: out of object slots, raise TINY_MAX_OBJECTS");
#endif
  return NULL;
}

void tiny_kill(tiny_game* game, tiny_object* obj) {
  if (obj == NULL) {
    return;
  }
  obj->active = false;
  obj->visible = false;
  if (game->player == obj) {
    game->player = NULL;
  }
}

/*-- animation ------------------------------------------------------------------------------*/

static const tiny_anim* _tiny_anim_find(tiny_game* game, const char* name, uint16_t* out_index) {
  const tiny_map* map = game->map;
  if (map == NULL || name == NULL) {
    return NULL;
  }
  uint32_t hash = tiny_hash(name);
  for (uint16_t i = 0; i < map->anim_count; i++) {
    const tiny_anim* anim = &map->anims[i];
    if (anim->name != NULL && anim->hash == hash && TINY_STRCMP(anim->name, name) == 0) {
      if (out_index != NULL) {
        *out_index = (uint16_t)(i + 1);
      }
      return anim;
    }
  }
  return NULL;
}

bool tiny_play(tiny_game* game, tiny_object* obj, const char* name) {
  uint16_t index = 0;
  const tiny_anim* anim = _tiny_anim_find(game, name, &index);
  if (anim == NULL || anim->count == 0) {
    return false;
  }
  if (obj->anim == index) {
    return true; /* Already running: do not restart it mid-stride. */
  }
  obj->anim = index;
  obj->frame = 0;
  obj->frame_timer = 0.0f;
  obj->tile = anim->frames[0].tile;
  return true;
}

bool tiny_pose(tiny_game* game, tiny_object* obj, const char* name, int frame) {
  const tiny_anim* anim = _tiny_anim_find(game, name, NULL);
  if (anim == NULL || anim->count == 0) {
    return false;
  }
  /* anim stays 0 so _tiny_advance_anim leaves the frame alone. */
  obj->anim = 0;
  obj->frame = (uint16_t)((frame < 0 ? 0 : frame) % anim->count);
  obj->frame_timer = 0.0f;
  obj->tile = anim->frames[obj->frame].tile;
  return true;
}

void tiny_stop(tiny_object* obj) {
  obj->anim = 0;
  obj->frame = 0;
  obj->frame_timer = 0.0f;
}

bool tiny_playing(tiny_game* game, const tiny_object* obj, const char* name) {
  uint16_t index = 0;
  if (obj->anim == 0 || _tiny_anim_find(game, name, &index) == NULL) {
    return false;
  }
  return obj->anim == index;
}

static void _tiny_advance_anim(tiny_game* game, tiny_object* obj, float dt) {
  if (obj->anim == 0 || obj->anim > game->map->anim_count) {
    return;
  }
  const tiny_anim* anim = &game->map->anims[obj->anim - 1];
  if (anim->count == 0) {
    return;
  }
  obj->frame_timer += dt * 1000.0f;
  uint16_t guard = 0;
  while (obj->frame_timer >= (float)anim->frames[obj->frame].ms && guard++ < 64) {
    obj->frame_timer -= (float)anim->frames[obj->frame].ms;
    obj->frame = (uint16_t)((obj->frame + 1) % anim->count);
  }
  obj->tile = anim->frames[obj->frame].tile;
}

/* Tile-layer animations run on one shared clock, so every copy of a tile stays in step. */
static uint16_t _tiny_resolve_tile(tiny_game* game, uint16_t tile) {
  const tiny_map* map = game->map;
  if (tile == 0 || tile >= map->tile_count) {
    return 0;
  }
  uint16_t anim_index = map->tiles[tile].anim;
  if (anim_index == 0 || anim_index > map->anim_count) {
    return tile;
  }
  const tiny_anim* anim = &map->anims[anim_index - 1];
  if (anim->count == 0 || anim->total_ms == 0) {
    return tile;
  }
  uint32_t now = (uint32_t)(game->anim_time * 1000.0f) % anim->total_ms;
  for (uint16_t i = 0; i < anim->count; i++) {
    if (now < anim->frames[i].ms) {
      return anim->frames[i].tile;
    }
    now -= anim->frames[i].ms;
  }
  return anim->frames[anim->count - 1].tile;
}

/*-- the view -------------------------------------------------------------------------------*/

pntr_rectangle tiny_view(tiny_game* game) {
  pntr_rectangle view = {(int)floorf(game->camera.x), (int)floorf(game->camera.y),
                         game->screen_w, game->screen_h};
  return view;
}

pntr_rectangle tiny_view_tiles(tiny_game* game) {
  const tiny_map* map = game->map;
  /* Work from the camera in floats, so a half-pixel scroll cannot lose an edge tile. */
  float left = game->camera.x;
  float top = game->camera.y;
  float right = left + (float)game->screen_w;
  float bottom = top + (float)game->screen_h;

  if (map->orientation != TINY_ISOMETRIC) {
    int x0 = (int)floorf(left / (float)map->tile_w);
    int y0 = (int)floorf(top / (float)map->tile_h);
    int x1 = (int)ceilf(right / (float)map->tile_w);
    int y1 = (int)ceilf(bottom / (float)map->tile_h);
    pntr_rectangle out = {x0, y0, x1 - x0, y1 - y0};
    return out;
  }

  /* Un-project the four corners: in tile space the view is a diamond, so take its bounds. */
  float xs[4], ys[4];
  _tiny_unproject(map, left, top, &xs[0], &ys[0]);
  _tiny_unproject(map, right, top, &xs[1], &ys[1]);
  _tiny_unproject(map, left, bottom, &xs[2], &ys[2]);
  _tiny_unproject(map, right, bottom, &xs[3], &ys[3]);

  float min_x = xs[0], max_x = xs[0], min_y = ys[0], max_y = ys[0];
  for (int i = 1; i < 4; i++) {
    min_x = TINY_MIN(min_x, xs[i]);
    max_x = TINY_MAX(max_x, xs[i]);
    min_y = TINY_MIN(min_y, ys[i]);
    max_y = TINY_MAX(max_y, ys[i]);
  }
  pntr_rectangle out = {(int)floorf(min_x), (int)floorf(min_y),
                        (int)(ceilf(max_x) - floorf(min_x)), (int)(ceilf(max_y) - floorf(min_y))};
  return out;
}

bool tiny_on_screen(tiny_game* game, const tiny_object* obj) {
  float px, py, qx, qy;
  _tiny_project(game->map, obj->x, obj->y, &px, &py);
  _tiny_project(game->map, obj->x + obj->w, obj->y + obj->h, &qx, &qy);

  float margin = (float)(TINY_CULL_MARGIN * game->map->tile_w);
  float left = TINY_MIN(px, qx) - margin;
  float right = TINY_MAX(px, qx) + margin;
  float top = TINY_MIN(py, qy) - margin;
  float bottom = TINY_MAX(py, qy) + margin;

  return right >= game->camera.x && left <= game->camera.x + (float)game->screen_w &&
         bottom >= game->camera.y && top <= game->camera.y + (float)game->screen_h;
}

/*-- camera ---------------------------------------------------------------------------------*/

static void _tiny_camera_clamp(tiny_game* game) {
  if (!game->camera.clamp) {
    return;
  }
  float map_w, map_h;
  _tiny_map_pixel_size(game->map, &map_w, &map_h);

  float max_x = map_w - (float)game->screen_w;
  float max_y = map_h - (float)game->screen_h;
  /* A map smaller than the screen is centred rather than pinned to the corner. */
  game->camera.x = max_x <= 0.0f ? max_x * 0.5f : TINY_MIN(TINY_MAX(game->camera.x, 0.0f), max_x);
  game->camera.y = max_y <= 0.0f ? max_y * 0.5f : TINY_MIN(TINY_MAX(game->camera.y, 0.0f), max_y);
}

static void _tiny_camera_update(tiny_game* game, float dt) {
  tiny_camera* cam = &game->camera;
  if (cam->mode == TINY_CAMERA_FIXED || game->player == NULL) {
    _tiny_camera_clamp(game);
    return;
  }

  float px, py;
  _tiny_project(game->map, game->player->x + game->player->w * 0.5f,
                game->player->y + game->player->h * 0.5f, &px, &py);

  if (cam->mode == TINY_CAMERA_ROOM) {
    /* Whole screens at a time, the way the first Zelda scrolls between rooms. */
    cam->x = floorf(px / (float)game->screen_w) * (float)game->screen_w;
    cam->y = floorf(py / (float)game->screen_h) * (float)game->screen_h;
    _tiny_camera_clamp(game);
    return;
  }

  float want_x = px - (float)game->screen_w * 0.5f;
  float want_y = py - (float)game->screen_h * 0.5f;
  float target_x = want_x;
  float target_y = want_y;

  /*
   * The deadzone is a band the camera is allowed to lag within, and it tracks the player at
   * the band's edge once they reach it. Freezing the camera until the player escapes and
   * then catching up in one go is what makes a side-scroller look like it is stuttering.
   */
  if (cam->deadzone > 0.0f) {
    float slack_x = (float)game->screen_w * cam->deadzone * 0.5f;
    float slack_y = (float)game->screen_h * cam->deadzone * 0.5f;
    target_x = TINY_MIN(TINY_MAX(cam->x, want_x - slack_x), want_x + slack_x);
    target_y = TINY_MIN(TINY_MAX(cam->y, want_y - slack_y), want_y + slack_y);
  }

  if (cam->ease <= 0.0f) {
    cam->x = target_x;
    cam->y = target_y;
  } else {
    /* Frame-rate independent easing, so the feel does not change with fps. */
    float t = 1.0f - powf(cam->ease, dt * 60.0f);
    cam->x += (target_x - cam->x) * t;
    cam->y += (target_y - cam->y) * t;
  }
  _tiny_camera_clamp(game);
}

/*-- map loading ----------------------------------------------------------------------------*/

/* Per-type defaults. A map can override any of them with its own custom properties. */
static void _tiny_apply_map_type(tiny_game* game) {
  const tiny_map* map = game->map;
  tiny_camera* cam = &game->camera;

  cam->clamp = true;
  cam->ease = 0.0f;
  cam->deadzone = 0.0f;
  game->gravity = 0.0f;

  switch (map->type) {
    case TINY_MAP_PLATFORMER:
      cam->mode = TINY_CAMERA_FOLLOW;
      cam->ease = 0.35f;
      cam->deadzone = 0.25f;
      /* Set `gravity` to 0 on the map for a side-scrolling shooter instead. */
      game->gravity = 600.0f;
      break;
    case TINY_MAP_ISOMETRIC:
      cam->mode = TINY_CAMERA_FOLLOW;
      cam->ease = 0.4f;
      break;
    case TINY_MAP_TOPDOWN:
    default:
      cam->mode = TINY_CAMERA_FOLLOW;
      cam->ease = 0.25f;
      break;
  }

  /*
   * A map no bigger than the screen has nowhere to scroll, so the clamp centres it and it
   * behaves as a fixed arcade screen without needing to be told.
   */

  const char* mode = tiny_prop_str(&map->props, "camera", NULL);
  if (mode != NULL) {
    if (TINY_STRCMP(mode, "room") == 0) {
      cam->mode = TINY_CAMERA_ROOM;
    } else if (TINY_STRCMP(mode, "fixed") == 0) {
      cam->mode = TINY_CAMERA_FIXED;
    } else if (TINY_STRCMP(mode, "follow") == 0) {
      cam->mode = TINY_CAMERA_FOLLOW;
    }
  }
  game->gravity = tiny_prop_float(&map->props, "gravity", game->gravity);
  cam->ease = tiny_prop_float(&map->props, "camera_ease", cam->ease);
  cam->deadzone = tiny_prop_float(&map->props, "camera_deadzone", cam->deadzone);
  cam->clamp = tiny_prop_bool(&map->props, "camera_clamp", cam->clamp);
}

static void _tiny_load_map(tiny_game* game, const tiny_map* map, const char* spawn) {
  if (game->def->leave != NULL && game->map != NULL) {
    game->def->leave(game);
  }

  game->map = map;
  game->player = NULL;
  game->object_count = 0;
  game->time = 0.0f;
  TINY_MEMSET(game->objects, 0, sizeof(tiny_object) * game->object_cap);

  _tiny_apply_map_type(game);

  uint16_t wanted = map->object_count;
  if (wanted > game->object_cap) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_WARNING, "tinyengine: %s has %d objects but only %d slots",
              map->name, (int)wanted, (int)game->object_cap);
#endif
    wanted = game->object_cap;
  }

  for (uint16_t i = 0; i < wanted; i++) {
    const tiny_object_def* def = &map->objects[i];
    tiny_object* obj = &game->objects[i];
    _tiny_init_object(game, obj, def, def->x, def->y);

    /* An animated sprite starts playing on its own, so an idle loop needs no code. */
    if (obj->tile != 0 && map->tiles[obj->tile].anim != 0) {
      obj->anim = map->tiles[obj->tile].anim;
    }
    if (game->player == NULL && (tiny_is(obj, "player") || (def->name != NULL && TINY_STRCMP(def->name, "player") == 0))) {
      game->player = obj;
    }
  }
  game->object_count = wanted;

  /* A portal asked to land the player somewhere specific. */
  if (spawn != NULL && game->player != NULL) {
    tiny_object* point = tiny_find(game, spawn);
    if (point != NULL) {
      game->player->x = point->x;
      game->player->y = point->y;
      game->player->vx = 0.0f;
      game->player->vy = 0.0f;
    }
#ifndef TINY_NO_STDIO
    else {
      _tiny_log(PNTR_APP_LOG_WARNING, "tinyengine: no spawn point '%s' on map '%s'", spawn, map->name);
    }
#endif
  }

  /* Put the camera where it belongs before the first frame, rather than easing in from 0. */
  float ease = game->camera.ease;
  game->camera.ease = 0.0f;
  _tiny_camera_update(game, 0.0f);
  game->camera.ease = ease;

  if (game->def->enter != NULL) {
    game->def->enter(game);
  }
}

const tiny_map* tiny_find_map(tiny_game* game, const char* name) {
  if (name == NULL) {
    return NULL;
  }
  uint32_t hash = tiny_hash(name);
  for (uint16_t i = 0; i < game->def->map_count; i++) {
    const tiny_map* map = game->def->maps[i];
    if (map->hash == hash && TINY_STRCMP(map->name, name) == 0) {
      return map;
    }
  }
  return NULL;
}

bool tiny_goto_map(tiny_game* game, const char* name, const char* spawn) {
  const tiny_map* map = tiny_find_map(game, name);
  if (map == NULL) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_ERROR, "tinyengine: no map named '%s'", name);
#endif
    return false;
  }
  /* Deferred, so the rest of this frame's objects still see a coherent world. */
  game->pending_map = map;
  game->pending_spawn = spawn;
  return true;
}

/*-- lifecycle ------------------------------------------------------------------------------*/

tiny_game* tiny_init(pntr_app* app, const tiny_game_def* def, void* user) {
  if (def == NULL || def->maps == NULL || def->map_count == 0) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_ERROR, "tinyengine: the game definition has no maps");
#endif
    return NULL;
  }

  tiny_game* game = (tiny_game*)pntr_load_memory(sizeof(tiny_game));
  if (game == NULL) {
    return NULL;
  }
  TINY_MEMSET(game, 0, sizeof(tiny_game));

  game->object_cap = TINY_MAX_OBJECTS;
  game->objects = (tiny_object*)pntr_load_memory(sizeof(tiny_object) * game->object_cap);
  if (game->objects == NULL) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_ERROR, "tinyengine: could not allocate %d objects", (int)game->object_cap);
#endif
    pntr_unload_memory(game);
    return NULL;
  }

  game->def = def;
  game->app = app;
  game->user = user;
  game->screen_w = pntr_app_width(app);
  game->screen_h = pntr_app_height(app);

  const tiny_map* start = def->start_map != NULL ? tiny_find_map(game, def->start_map) : NULL;
  _tiny_load_map(game, start != NULL ? start : def->maps[0], NULL);
  return game;
}

void tiny_unload(tiny_game* game) {
  if (game == NULL) {
    return;
  }
  if (game->def != NULL && game->def->leave != NULL && game->map != NULL) {
    game->def->leave(game);
  }
  pntr_unload_memory(game->objects);
  pntr_unload_memory(game);
}

void tiny_update(tiny_game* game, float dt) {
  if (game == NULL || game->map == NULL) {
    return;
  }
  game->time += dt;
  game->anim_time += dt;

  if (game->def->object != NULL) {
    /*
     * object_count is read once: anything spawned this frame first runs next frame, which
     * keeps a spawn loop from running away inside a single update.
     */
    uint16_t count = game->object_count;
    for (uint16_t i = 0; i < count; i++) {
      tiny_object* obj = &game->objects[i];
      if (obj->active) {
        game->def->object(game, obj, dt);
      }
    }
  }

  for (uint16_t i = 0; i < game->object_count; i++) {
    tiny_object* obj = &game->objects[i];
    if (obj->active) {
      _tiny_advance_anim(game, obj, dt);
    }
  }

  _tiny_camera_update(game, dt);

  if (game->pending_map != NULL) {
    const tiny_map* map = game->pending_map;
    const char* spawn = game->pending_spawn;
    game->pending_map = NULL;
    game->pending_spawn = NULL;
    _tiny_load_map(game, map, spawn);
  }
}

/*-- drawing --------------------------------------------------------------------------------*/

/* Where a tile's image goes, given that iso images hang off the bottom of their diamond. */
static void _tiny_tile_pixel(const tiny_map* map, const tiny_tile* tile, int tx, int ty, float* px, float* py) {
  const pntr_image* image = &tile->image;
  if (map->orientation == TINY_ISOMETRIC) {
    *px = (float)(tx - ty) * (float)map->tile_w * 0.5f + _tiny_iso_origin(map) - (float)image->width * 0.5f;
    *py = (float)(tx + ty) * (float)map->tile_h * 0.5f + (float)map->tile_h - (float)image->height;
  } else {
    /* Orthogonal tiles taller than the grid also hang off the bottom, as Tiled draws them. */
    *px = (float)(tx * map->tile_w);
    *py = (float)(ty * map->tile_h) + (float)map->tile_h - (float)image->height;
  }
  *px += (float)tile->offset_x;
  *py += (float)tile->offset_y;
}

static void _tiny_draw_tile_layer(tiny_game* game, pntr_image* screen, const tiny_layer* layer) {
  const tiny_map* map = game->map;

  /* Parallax shifts the camera for this layer only. */
  float cam_x = game->camera.x;
  float cam_y = game->camera.y;
  game->camera.x = cam_x * layer->parallax_x - layer->offset_x;
  game->camera.y = cam_y * layer->parallax_y - layer->offset_y;

  /*
   * A tile image taller than the grid hangs upwards out of its own cell, so cells past the
   * bottom and right of the view can still reach into it. Isometric tilesets do this by
   * design, and orthogonal ones do it for trees and walls. Reaching a few cells wide is far
   * cheaper than measuring the tallest tile, and the draw itself clips.
   */
  pntr_rectangle range = tiny_view_tiles(game);
  int x0 = TINY_MAX(range.x - TINY_CULL_MARGIN, 0);
  int y0 = TINY_MAX(range.y - TINY_CULL_MARGIN, 0);
  int x1 = TINY_MIN(range.x + range.width + TINY_CULL_MARGIN, (int)layer->width - 1);
  int y1 = TINY_MIN(range.y + range.height + TINY_CULL_MARGIN, (int)layer->height - 1);

  /*
   * Both orientations draw top-to-bottom, left-to-right in tile space, which for an
   * isometric map is exactly back-to-front. That is the painter's algorithm, for free.
   */
  for (int ty = y0; ty <= y1; ty++) {
    for (int tx = x0; tx <= x1; tx++) {
      uint16_t index = _tiny_resolve_tile(game, layer->tiles[(size_t)ty * layer->width + tx]);
      if (index == 0) {
        continue;
      }
      const tiny_tile* tile = &map->tiles[index];
      if (tile->image.data == NULL) {
        continue;
      }
      float px, py;
      _tiny_tile_pixel(map, tile, tx, ty, &px, &py);
      /* Casting away const is safe: pntr only ever reads the source image. */
      pntr_draw_image(screen, (pntr_image*)&tile->image, (int)floorf(px - game->camera.x),
                      (int)floorf(py - game->camera.y));
    }
  }

  game->camera.x = cam_x;
  game->camera.y = cam_y;
}

/*
 * Where an object's sprite is pinned: horizontally at the centre of its footprint, and
 * vertically at the footprint's lowest point on screen.
 *
 * For an orthogonal map that is just bottom-centre of the box. For an isometric one the
 * footprint is a diamond, whose centre and bottom vertex come from different world points,
 * and pinning to those puts a 1x1 object exactly where Tiled draws the tile it stands on.
 * Either way a sprite taller than its collision box grows upwards, so the feet stay put.
 */
static void _tiny_sprite_anchor(const tiny_map* map, const tiny_object* obj, float* px, float* py) {
  float cx, cy, bx, by;
  _tiny_project(map, obj->x + obj->w * 0.5f, obj->y + obj->h * 0.5f, &cx, &cy);
  _tiny_project(map, obj->x + obj->w, obj->y + obj->h, &bx, &by);
  (void)cy;
  (void)bx;
  *px = cx;
  *py = by;
}

static void _tiny_draw_object(tiny_game* game, pntr_image* screen, const tiny_object* obj) {
  const tiny_map* map = game->map;
  uint16_t index = _tiny_resolve_tile(game, obj->tile);
  if (index == 0) {
    return;
  }
  const tiny_tile* tile = &map->tiles[index];
  const pntr_image* image = &tile->image;
  if (image->data == NULL) {
    return;
  }

  float px, py;
  _tiny_sprite_anchor(map, obj, &px, &py);
  int x = (int)floorf(px - (float)image->width * 0.5f - game->camera.x) + tile->offset_x;
  int y = (int)floorf(py - (float)image->height - game->camera.y) + tile->offset_y;

  if (obj->flip_x || obj->flip_y) {
    pntr_draw_image_flipped(screen, (pntr_image*)image, x, y, obj->flip_x, obj->flip_y, false);
  } else {
    pntr_draw_image(screen, (pntr_image*)image, x, y);
  }
}

/* Depth for sorting: how far down the screen the object's feet land. */
static float _tiny_depth(const tiny_map* map, const tiny_object* obj) {
  float px, py;
  _tiny_sprite_anchor(map, obj, &px, &py);
  (void)px;
  return py;
}

static void _tiny_draw_object_layer(tiny_game* game, pntr_image* screen, uint8_t layer_index, bool sort) {
  /*
   * Insertion sort over an index list. With TINY_MAX_OBJECTS in the tens and a list that is
   * already almost ordered from the frame before, this stays near linear.
   */
  uint16_t order[TINY_MAX_OBJECTS];
  uint16_t count = 0;

  for (uint16_t i = 0; i < game->object_count && count < TINY_MAX_OBJECTS; i++) {
    tiny_object* obj = &game->objects[i];
    if (!obj->active || !obj->visible || obj->tile == 0 || obj->layer != layer_index) {
      continue;
    }
    if (!tiny_on_screen(game, obj)) {
      continue;
    }
    order[count++] = i;
  }

  if (sort) {
    for (uint16_t i = 1; i < count; i++) {
      uint16_t index = order[i];
      float depth = _tiny_depth(game->map, &game->objects[index]);
      int j = (int)i - 1;
      while (j >= 0 && _tiny_depth(game->map, &game->objects[order[j]]) > depth) {
        order[j + 1] = order[j];
        j--;
      }
      order[j + 1] = index;
    }
  }

  for (uint16_t i = 0; i < count; i++) {
    _tiny_draw_object(game, screen, &game->objects[order[i]]);
  }
}

void tiny_draw(tiny_game* game, pntr_image* screen) {
  if (game == NULL || game->map == NULL || screen == NULL) {
    return;
  }
  /* The screen can change size between frames on a resizable window. */
  game->screen_w = screen->width;
  game->screen_h = screen->height;

  pntr_clear_background(screen, game->map->background);

  /*
   * Layers draw in the order Tiled lists them, objects included. That is what the editor
   * shows you, and it is how a foreground layer ends up in front of the player: put the
   * tile layer after the object layer.
   */
  for (uint16_t i = 0; i < game->map->layer_count; i++) {
    const tiny_layer* layer = &game->map->layers[i];
    if (!layer->visible) {
      continue;
    }
    if (layer->kind == TINY_LAYER_TILE) {
      _tiny_draw_tile_layer(game, screen, layer);
    } else {
      _tiny_draw_object_layer(game, screen, (uint8_t)i, layer->sort);
    }
  }

  if (game->def->draw != NULL) {
    game->def->draw(game, screen);
  }
}

#ifdef __cplusplus
}
#endif

#endif  // TINY_ENGINE_IMPLEMENTATION_ONCE
#endif  // TINY_ENGINE_IMPLEMENTATION
