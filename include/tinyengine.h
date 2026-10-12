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
 *       TINY_NO_AUDIO_OUTPUT        Do not open an audio device. Call tiny_audio_render()
 *                                   from your own sink instead.
 *       TINY_TOUCH_CONTROLS         Draw an on-screen d-pad and A/B buttons, worked by touch
 *                                   or the mouse. They hold the matching gamepad buttons in
 *                                   tiny_held() and tiny_axis_x/y(), so games need no code.
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

#ifndef TINY_AUDIO_RATE
/**
 * The mixer's sample rate, and what the platform's audio sink must be opened at.
 *
 * Bake WAV audio with `tiny_bake.py --sound-rate` set to the same number, so the mixer
 * reads samples at their own rate and never has to resample.
 */
#define TINY_AUDIO_RATE 22050
#endif

#ifndef TINY_MAX_VOICES
/** PCM and SFX sounds that can play at once. A MOD uses the one music slot, not a voice. */
#define TINY_MAX_VOICES 8
#endif

#ifndef TINY_MAX_TOUCHES
/** Fingers the touch controls follow at once, where the screen can report more than one. */
#define TINY_MAX_TOUCHES 4
#endif

#ifndef TINY_MAX_SFX
/** Different SFX sounds kept synthesised at once. Each costs its own length in RAM. */
#define TINY_MAX_SFX 16
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

/** How a baked sound is played. */
typedef enum tiny_sound_kind {
  TINY_SOUND_PCM = 0, /** Raw mono PCM from a WAV. The mixer reads it straight out of flash. */
  TINY_SOUND_MOD,     /** ProTracker module bytes, played by the vendored micromod. */
  TINY_SOUND_SFX      /** A 96 byte SfxParams, synthesised once by pntr_app_sfx. */
} tiny_sound_kind;

/**
 * One baked sound. Everything it points at is `static const` flash, so the engine copies
 * nothing and decodes nothing: a PCM sound is mixed out of .rodata as it stands.
 */
typedef struct tiny_sound {
  const char* name;
  uint32_t hash;
  uint8_t kind;     /** tiny_sound_kind */
  uint8_t bits;     /** PCM: 8 or 16 bits per sample. 0 for MOD and SFX. */
  uint16_t rate;    /** PCM: samples per second. 0 for MOD and SFX, which have their own. */
  const void* data; /** PCM samples, module bytes, or SfxParams bytes. */
  uint32_t size;    /** Bytes in `data`. */
} tiny_sound;

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

  uint8_t portal; /** The engine's: whether a portal is armed. */
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
 * One mixer voice: a PCM or SFX sound being played. A MOD uses the single music slot
 * instead, because micromod is a whole-song player rather than a sample.
 */
typedef struct tiny_voice {
  const tiny_sound* sound;
  const void* samples; /** PCM source: flash for a WAV, the SFX cache for an SFX. */
  uint32_t count;      /** Samples in `samples`. */
  uint32_t position;   /** Read cursor, 16.16 fixed point, so a differing rate resamples. */
  uint32_t step;       /** 16.16 advance per output frame. */
  uint8_t bits;
  bool loop;
  bool active;
} tiny_voice;

/** An SFX sound, synthesised the first time it plays and kept for the next. */
typedef struct tiny_sfx {
  const tiny_sound* sound;
  int16_t* samples; /** At TINY_SFX_RATE. */
  uint32_t count;
} tiny_sfx;

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

  const tiny_sound* sounds; /** The baked sound bank. NULL when the game has no sound. */
  uint16_t sound_count;

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

  const tiny_sound* sounds; /** Copied from the game definition. */
  uint16_t sound_count;
  tiny_voice voices[TINY_MAX_VOICES];
  const tiny_sound* music; /** The playing MOD, or NULL. micromod holds only one at a time. */
  bool music_loop;
  uint32_t music_played; /** Output frames since the module started. */
  uint32_t music_length; /** Samples in one pass of the module, so a non-loop can stop. */
  tiny_sfx sfx[TINY_MAX_SFX];

  pntr_app* app; /** Set by tiny_init(), so behaviour code can read input. */
  void* user;    /** Yours. */

  uint32_t touch_buttons; /** Gamepad buttons the touch controls hold: bit `1 << button`. */
  bool touch_visible;     /** On the web, set by the first touch. Everywhere else, from the start. */
  uint8_t touch_count;
  float touch_x[TINY_MAX_TOUCHES];
  float touch_y[TINY_MAX_TOUCHES];

  pntr_font* font;     /** Draws tiny_say() messages. The default font unless you swap it. */
  const char* message; /** What tiny_say() is showing, or NULL. */
  float message_timer;

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

/**
 * Step behaviour, animation, the camera, portals, and any pending map change.
 *
 * `dt` is capped at 1/20s, so a long stall cannot carry anything through a wall.
 */
TINY_API void tiny_update(tiny_game* game, float dt);

/** Draw the map and its objects, call def->draw for the overlay, then any tiny_say() box. */
TINY_API void tiny_draw(tiny_game* game, pntr_image* screen);

/* -- maps ------------------------------------------------------------------------------- */

TINY_API const tiny_map* tiny_find_map(tiny_game* game, const char* name);

/**
 * Switch maps at the end of this frame.
 *
 * `spawn` names an object on the destination map to place the player at, which is how a
 * portal lands you somewhere sensible. Pass NULL to use the destination's own player object.
 *
 * Objects of class `portal` do this on their own: when the player walks into one, the
 * engine goes to its `destination` property, lands on its `spawn`, and plays the sound
 * named by `sound`. A portal you arrive standing on waits until you step off it, so a door
 * does not throw you straight back through.
 */
TINY_API bool tiny_goto_map(tiny_game* game, const char* name, const char* spawn);

/* -- input ------------------------------------------------------------------------------ */

/** Whether a key, or that button on the first gamepad, is down. */
TINY_API bool tiny_held(tiny_game* game, pntr_app_key key, pntr_app_gamepad_button button);

/** -1, 0 or 1 from the arrow keys and the d-pad. Up is -1. */
TINY_API float tiny_axis_x(tiny_game* game);
TINY_API float tiny_axis_y(tiny_game* game);

/* -- messages --------------------------------------------------------------------------- */

/**
 * Show text in a box along the bottom of the screen for `seconds`, word-wrapped to fit.
 *
 * The text is not copied, so pass a string that outlives the message: a literal, or a
 * property read off a map.
 */
TINY_API void tiny_say(tiny_game* game, const char* text, float seconds);

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

/* -- sound ------------------------------------------------------------------------------ */

/**
 * Start a baked sound. Returns false when there is no such sound.
 *
 * A PCM (WAV) or SFX sound takes a mixer voice, so several can sound at once. A MOD takes
 * the one music slot: starting a second module replaces the first. `loop` keeps it going.
 */
TINY_API bool tiny_sound_play(tiny_game* game, const char* name, bool loop);

/** Stop a sound: its voice, or the music slot. Does nothing when it is not playing. */
TINY_API void tiny_sound_stop(tiny_game* game, const char* name);

/** Whether a sound is playing right now. */
TINY_API bool tiny_sound_playing(tiny_game* game, const char* name);

/**
 * Fill `frames` mono int16 samples at TINY_AUDIO_RATE. The platform's audio sink calls this.
 *
 * The engine opens a sink for the raylib, web and ESP32 backends itself and feeds it from
 * tiny_update(). Define TINY_NO_AUDIO_OUTPUT to bring your own and call this from it.
 */
TINY_API void tiny_audio_render(tiny_game* game, int16_t* samples, int frames);

/** Whether anything is playing, so a sink can push silence instead of mixing. */
TINY_API bool tiny_audio_active(tiny_game* game);

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

#ifndef TINY_NO_MOD
/*
 * micromod, vendored. Including the .c here is what keeps tinyengine a single header: the
 * consumer compiles one translation unit and gets the MOD player with it. It allocates
 * nothing, so it costs no heap -- just the module it reads out of flash.
 */
#include "micromod.c"
#endif

#ifndef PNTR_FREE
#define PNTR_FREE(ptr) free(ptr)
#include <stdlib.h>
#endif

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
#define TINY_BUTTON_BIT(button) (1u << (button))
#define TINY_MAX(a, b) ((a) > (b) ? (a) : (b))

/* How many tiles past the view to still consider, for images that overhang their own cell. */
#define TINY_CULL_MARGIN 4

/* Output frames mixed at a time, so the mixer's accumulator stays off the deep end of the stack. */
#define TINY_AUDIO_CHUNK 256

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

/*-- sound ----------------------------------------------------------------------------------*/

static const tiny_sound* _tiny_find_sound(tiny_game* game, const char* name) {
  if (game == NULL || name == NULL || game->sounds == NULL) {
    return NULL;
  }
  uint32_t hash = tiny_hash(name);
  for (uint16_t i = 0; i < game->sound_count; i++) {
    const tiny_sound* sound = &game->sounds[i];
    if (sound->hash == hash && TINY_STRCMP(sound->name, name) == 0) {
      return sound;
    }
  }
  return NULL;
}

static int16_t _tiny_voice_sample(const tiny_voice* voice, uint32_t index) {
  if (index >= voice->count) {
    return 0;
  }
  if (voice->bits == 8) {
    /* Baked 8 bit audio is unsigned, the way a WAV stores it. */
    return (int16_t)(((int16_t)((const uint8_t*)voice->samples)[index] - 128) << 8);
  }
  return ((const int16_t*)voice->samples)[index];
}

/* A generated SFX is 44100Hz mono int16, whatever the mixer runs at; resample on the fly. */
static uint32_t _tiny_voice_step(uint16_t rate) {
  if (rate == 0) {
    rate = TINY_AUDIO_RATE;
  }
  return (uint32_t)(((uint64_t)rate << 16) / (uint64_t)TINY_AUDIO_RATE);
}

static void _tiny_voice_stop(tiny_voice* voice) {
  TINY_MEMSET(voice, 0, sizeof(tiny_voice));
}

#ifdef PNTR_APP_SFX_H__
/* pntr_app_sfx always synthesises at 44100. The cache keeps half that, which is plenty. */
#define TINY_SFX_RATE 22050

/*
 * Synthesise an SFX once and keep it. Rendering on every play would cost a synth pass and an
 * allocation each time, which an ESP32 notices.
 *
 * pntr_app_sfx sizes its buffer as sampleRate * maxDuration and uses the rate for nothing
 * else, so asking for `length` per second, for one second, gives a buffer exactly as long as
 * the sound's envelope rather than the usual several seconds of it.
 */
static const tiny_sfx* _tiny_sfx_render(tiny_game* game, const tiny_sound* sound) {
  tiny_sfx* slot = NULL;
  for (int i = 0; i < TINY_MAX_SFX; i++) {
    if (game->sfx[i].sound == sound) {
      return &game->sfx[i];
    }
    if (slot == NULL && game->sfx[i].sound == NULL) {
      slot = &game->sfx[i];
    }
  }
  if (slot == NULL) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_WARNING, "tinyengine: no room to keep '%s', raise TINY_MAX_SFX", sound->name);
#endif
    return NULL;
  }

  SfxParams params;
  TINY_MEMSET(&params, 0, sizeof(params));
  TINY_MEMCPY(&params, sound->data, sound->size < sizeof(params) ? sound->size : sizeof(params));
  int length = 4;
  length += (int)(params.attackTime * params.attackTime * 100000.0f);
  length += (int)(params.sustainTime * params.sustainTime * 100000.0f);
  length += (int)(params.decayTime * params.decayTime * 100000.0f);

  SfxSynth* synth = pntr_app_sfx_alloc_synth(SFX_I16, length, 1);
  if (synth == NULL) {
    return NULL;
  }
  int count = pntr_app_sfx_generate_wave(game->app, synth, &params) / 2;
  int16_t* samples = count > 0 ? (int16_t*)pntr_load_memory(sizeof(int16_t) * (size_t)count) : NULL;
  if (samples != NULL) {
    const int16_t* full = synth->samples.i16;
    for (int i = 0; i < count; i++) {
      samples[i] = (int16_t)(((int32_t)full[i * 2] + full[i * 2 + 1]) / 2);
    }
    slot->sound = sound;
    slot->samples = samples;
    slot->count = (uint32_t)count;
  }
  PNTR_FREE(synth);
  return samples != NULL ? slot : NULL;
}
#endif

static bool _tiny_start_voice(tiny_game* game, const tiny_sound* sound, bool loop) {
  tiny_voice* voice = NULL;
  for (int i = 0; i < TINY_MAX_VOICES; i++) {
    if (!game->voices[i].active) {
      voice = &game->voices[i];
      break;
    }
  }
  if (voice == NULL) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_WARNING, "tinyengine: no free voice for '%s', raise TINY_MAX_VOICES", sound->name);
#endif
    return false;
  }
  _tiny_voice_stop(voice);
  voice->sound = sound;
  voice->loop = loop;

  if (sound->kind == TINY_SOUND_SFX) {
#ifdef PNTR_APP_SFX_H__
    const tiny_sfx* sfx = _tiny_sfx_render(game, sound);
    if (sfx == NULL) {
      return false;
    }
    voice->samples = sfx->samples;
    voice->bits = 16;
    voice->count = sfx->count;
    voice->step = _tiny_voice_step(TINY_SFX_RATE);
#else
    /* The application did not compile pntr_app_sfx, so a baked SFX has nothing to play it. */
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_WARNING, "tinyengine: '%s' is an SFX; #include pntr_app_sfx.h to play it", sound->name);
#endif
    return false;
#endif
  } else {
    voice->samples = sound->data;
    voice->bits = sound->bits;
    voice->count = sound->size / (sound->bits == 8 ? 1u : 2u);
    voice->step = _tiny_voice_step(sound->rate);
  }
  /*
   * Switched on last: on an ESP32 the mixer runs on the other core, and it must never see
   * a voice that is active before its samples are in place.
   */
#if defined(__GNUC__)
  __sync_synchronize();
#endif
  voice->active = true;
  return true;
}

static void _tiny_mix_voices(tiny_game* game, int32_t* mix, int frames) {
  for (int v = 0; v < TINY_MAX_VOICES; v++) {
    tiny_voice* voice = &game->voices[v];
    if (!voice->active) {
      continue;
    }
    for (int i = 0; i < frames; i++) {
      uint32_t index = voice->position >> 16;
      if (index >= voice->count) {
        if (!voice->loop || voice->count == 0) {
          _tiny_voice_stop(voice);
          break;
        }
        /* Wrap on the sample index rather than `count << 16`, which overflows past 65535. */
        index %= voice->count;
        voice->position = (index << 16) | (voice->position & 0xFFFFu);
      }
      mix[i] += _tiny_voice_sample(voice, index);
      voice->position += voice->step;
    }
  }
}

#ifndef TINY_NO_MOD
/* micromod plays stereo; the mixer is mono, so fold it down. Chunked to keep the stack small. */
#define TINY_MOD_CHUNK 128

static void _tiny_mix_mod(tiny_game* game, int32_t* mix, int frames) {
  if (game->music == NULL) {
    return;
  }
  int16_t stereo[TINY_MOD_CHUNK * 2];
  int done = 0;
  while (done < frames) {
    int chunk = TINY_MIN(TINY_MOD_CHUNK, frames - done);
    TINY_MEMSET(stereo, 0, sizeof(int16_t) * (size_t)chunk * 2);
    micromod_get_audio(stereo, chunk);
    for (int i = 0; i < chunk; i++) {
      mix[done + i] += ((int32_t)stereo[i * 2] + stereo[i * 2 + 1]) / 2;
    }
    done += chunk;
    game->music_played += (uint32_t)chunk;
  }
  if (!game->music_loop && game->music_length > 0 && game->music_played >= game->music_length) {
    game->music = NULL;
  }
}

static bool _tiny_start_mod(tiny_game* game, const tiny_sound* sound, bool loop) {
  /* micromod reads the header in place and only ever reads the module, so dropping const is safe. */
  if (micromod_initialise((signed char*)sound->data, TINY_AUDIO_RATE) < 0) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_ERROR, "tinyengine: '%s' is not a MOD micromod can play", sound->name);
#endif
    return false;
  }
  micromod_set_gain(64);
  game->music = sound;
  game->music_loop = loop;
  game->music_played = 0;
  long length = micromod_calculate_song_duration();
  game->music_length = length > 0 ? (uint32_t)length : 0;
  return true;
}
#endif

bool tiny_sound_play(tiny_game* game, const char* name, bool loop) {
  if (game == NULL || name == NULL) {
    return false;
  }
  const tiny_sound* sound = _tiny_find_sound(game, name);
  if (sound == NULL) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_WARNING, "tinyengine: no sound named '%s'", name);
#endif
    return false;
  }
  if (sound->kind == TINY_SOUND_MOD) {
#ifdef TINY_NO_MOD
    return false;
#else
    return _tiny_start_mod(game, sound, loop);
#endif
  }
  return _tiny_start_voice(game, sound, loop);
}

void tiny_sound_stop(tiny_game* game, const char* name) {
  if (game == NULL) {
    return;
  }
  const tiny_sound* sound = _tiny_find_sound(game, name);
  if (sound == NULL) {
    return;
  }
  if (game->music == sound) {
    game->music = NULL;
  }
  for (int i = 0; i < TINY_MAX_VOICES; i++) {
    if (game->voices[i].active && game->voices[i].sound == sound) {
      _tiny_voice_stop(&game->voices[i]);
    }
  }
}

bool tiny_sound_playing(tiny_game* game, const char* name) {
  if (game == NULL) {
    return false;
  }
  const tiny_sound* sound = _tiny_find_sound(game, name);
  if (sound == NULL) {
    return false;
  }
  if (game->music == sound) {
    return true;
  }
  for (int i = 0; i < TINY_MAX_VOICES; i++) {
    if (game->voices[i].active && game->voices[i].sound == sound) {
      return true;
    }
  }
  return false;
}

bool tiny_audio_active(tiny_game* game) {
  if (game == NULL) {
    return false;
  }
#ifndef TINY_NO_MOD
  if (game->music != NULL) {
    return true;
  }
#endif
  for (int i = 0; i < TINY_MAX_VOICES; i++) {
    if (game->voices[i].active) {
      return true;
    }
  }
  return false;
}

void tiny_audio_render(tiny_game* game, int16_t* samples, int frames) {
  if (game == NULL || samples == NULL || frames <= 0) {
    return;
  }
  for (int start = 0; start < frames; start += TINY_AUDIO_CHUNK) {
    int count = TINY_MIN(TINY_AUDIO_CHUNK, frames - start);
    int32_t mix[TINY_AUDIO_CHUNK];
    TINY_MEMSET(mix, 0, sizeof(int32_t) * (size_t)count);
    _tiny_mix_voices(game, mix, count);
#ifndef TINY_NO_MOD
    _tiny_mix_mod(game, mix, count);
#endif
    for (int i = 0; i < count; i++) {
      int32_t sample = mix[i];
      samples[start + i] = (int16_t)(sample > 32767 ? 32767 : (sample < -32768 ? -32768 : sample));
    }
  }
}

/*-- audio output ---------------------------------------------------------------------------*/

/*
 * The sink is the only part of the audio that knows what hardware exists. Each one pulls
 * from tiny_audio_render(), and _tiny_audio_pump() runs once a frame from tiny_update().
 */

#if defined(TINY_NO_AUDIO_OUTPUT) || \
    !(defined(PNTR_APP_RAYLIB) || defined(PNTR_APP_WEB) || defined(PNTR_APP_ESP32))
/* No device. The mixer still works; the samples just go nowhere until someone asks. */
static void _tiny_audio_open(tiny_game* game) { (void)game; }
static void _tiny_audio_pump(tiny_game* game) { (void)game; }
static void _tiny_audio_close(void) {}

#elif defined(PNTR_APP_RAYLIB)
/*
 * raylib's buffer-push API (UpdateAudioStream) writes a whole sub-buffer at a time and zero
 * pads the rest, and its sub-buffer size is `max(default, device period)`, which differs
 * per machine. Pushing anything short of that chops the audio into silence-gaps and sounds
 * like heavy distortion. The stream callback avoids the whole problem: raylib asks for an
 * exact frame count whenever it needs one.
 *
 * That callback runs on the audio thread, so it must not touch engine state while the game
 * thread is updating it. So the game thread renders into a small ring and the callback only
 * drains it. One producer, one consumer, no locks.
 */
#define TINY_AUDIO_BLOCK 1024 /* frames rendered per pass */
#define TINY_AUDIO_RING 4096  /* power of two, comfortably more than a device period */
#define TINY_AUDIO_PREFILL (TINY_AUDIO_BLOCK * 2)

static AudioStream _tiny_audio_stream;
static bool _tiny_audio_ready;
static int16_t _tiny_audio_ring[TINY_AUDIO_RING];
static int16_t _tiny_audio_scratch[TINY_AUDIO_BLOCK];
static volatile uint32_t _tiny_audio_head; /* written by the game thread */
static volatile uint32_t _tiny_audio_tail; /* written by the audio thread */

static void _tiny_audio_fill(void* buffer, unsigned int frames) {
  int16_t* out = (int16_t*)buffer;
  /* The head is only ever advanced, so this stays correct across uint32 wrap. */
  uint32_t available = _tiny_audio_head - _tiny_audio_tail;
#if defined(__GNUC__)
  __sync_synchronize();
#endif
  uint32_t count = frames < available ? frames : available;
  for (uint32_t i = 0; i < count; i++) {
    out[i] = _tiny_audio_ring[(_tiny_audio_tail + i) & (TINY_AUDIO_RING - 1)];
  }
  for (unsigned int i = count; i < frames; i++) {
    out[i] = 0;
  }
  _tiny_audio_tail += count;
}

static void _tiny_audio_open(tiny_game* game) {
  (void)game;
  _tiny_audio_stream = LoadAudioStream(TINY_AUDIO_RATE, 16, 1);
  /* raylib no-ops on an invalid stream, so there is nothing to test here. */
  _tiny_audio_ready = true;
  SetAudioStreamCallback(_tiny_audio_stream, _tiny_audio_fill);
  PlayAudioStream(_tiny_audio_stream);
}

static void _tiny_audio_pump(tiny_game* game) {
  if (!_tiny_audio_ready) {
    return;
  }
  /* Top the ring up to the prefill, and no further: bounded work per frame, bounded latency. */
  uint32_t buffered = _tiny_audio_head - _tiny_audio_tail;
  uint32_t want = TINY_AUDIO_PREFILL > buffered ? TINY_AUDIO_PREFILL - buffered : 0;
  while (want > 0) {
    uint32_t chunk = want < TINY_AUDIO_BLOCK ? want : TINY_AUDIO_BLOCK;
    tiny_audio_render(game, _tiny_audio_scratch, (int)chunk);
    for (uint32_t i = 0; i < chunk; i++) {
      _tiny_audio_ring[(_tiny_audio_head + i) & (TINY_AUDIO_RING - 1)] = _tiny_audio_scratch[i];
    }
#if defined(__GNUC__)
    __sync_synchronize();
#endif
    _tiny_audio_head += chunk;
    want -= chunk;
  }
}

static void _tiny_audio_close(void) {
  if (_tiny_audio_ready) {
    StopAudioStream(_tiny_audio_stream);
    UnloadAudioStream(_tiny_audio_stream);
  }
  _tiny_audio_ready = false;
}

#elif defined(PNTR_APP_WEB)
#include <emscripten.h>

/*
 * A ScriptProcessorNode pulls a block from the engine on every audio callback. JavaScript is
 * single threaded, so this can never run in the middle of a C frame -- which is exactly why
 * the engine can be a plain render function with no locking. Browsers start the context
 * suspended until a gesture, so a one-shot handler resumes it on the first key or click.
 *
 * The context is asked for the engine's rate, but a browser may refuse and give its own
 * (usually 48000). Feeding 22050 samples into a 48000 context plays them ~2x fast -- which
 * sounds high pitched and wrong -- so the sink resamples from ctx.sampleRate to the engine
 * rate whenever the two disagree. At the requested rate the ratio is 1 and this is a copy.
 */
EM_JS(void, _tiny_web_audio_open, (int rate), {
  var ctx = new (window.AudioContext || window.webkitAudioContext)({sampleRate : rate});
  var step = rate / ctx.sampleRate; /* engine frames per output frame */
  var node = ctx.createScriptProcessor(2048, 0, 1);
  node.onaudioprocess = function(event) {
    var out = event.outputBuffer.getChannelData(0);
    var frames = out.length;
    /* One engine sample past the last needed output frame, so the interpolation has a pair. */
    var need = Math.ceil(frames * step) + 1;
    var ptr = _tiny_web_audio_buffer();
    _tiny_web_audio_fill(need);
    var heap = Module['HEAP16'] || HEAP16;
    var base = ptr >> 1;
    for (var i = 0; i < frames; i++) {
      var position = i * step;
      var index = position | 0;
      var frac = position - index;
      var a = heap[base + index];
      var b = heap[base + index + 1];
      out[i] = (a + (b - a) * frac) / 32768.0;
    }
  };
  var source = ctx.createBufferSource();
  source.connect(node);
  node.connect(ctx.destination);
  source.start(0);
  document.addEventListener('keydown', function() { ctx.resume(); }, {once : true});
  document.addEventListener('pointerdown', function() { ctx.resume(); }, {once : true});
});

/* Big enough for a worst-case low-rate context: 2048 output frames at 8000Hz wants 5645. */
#define TINY_WEB_AUDIO_BUFFER 8192
static int16_t _tiny_web_buffer[TINY_WEB_AUDIO_BUFFER];
static tiny_game* _tiny_web_game;

EMSCRIPTEN_KEEPALIVE int16_t* tiny_web_audio_buffer(void) { return _tiny_web_buffer; }

EMSCRIPTEN_KEEPALIVE void tiny_web_audio_fill(int frames) {
  if (frames > TINY_WEB_AUDIO_BUFFER) {
    frames = TINY_WEB_AUDIO_BUFFER;
  }
  tiny_audio_render(_tiny_web_game, _tiny_web_buffer, frames);
}

static void _tiny_audio_open(tiny_game* game) {
  _tiny_web_game = game;
  _tiny_web_audio_open(TINY_AUDIO_RATE);
}
static void _tiny_audio_pump(tiny_game* game) { (void)game; }
static void _tiny_audio_close(void) {}

#elif defined(PNTR_APP_ESP32)
/*
 * The ESP32's built-in DAC in continuous mode: DMA clocks the samples out at exactly the
 * mixer's rate, so a task only has to keep it fed. dac_continuous_write() blocks until there
 * is room, which is what paces the task -- writing the DAC by hand cannot, because nothing
 * on the CPU side holds a 45us sample period steadily.
 *
 * The task runs on core 0, away from the Arduino loop on core 1, so a slow frame never
 * starves the speaker.
 */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "driver/dac_continuous.h"
#include "driver/gpio.h"

#ifndef TINY_SPEAKER_PIN
/** GPIO25 or GPIO26, the two DAC pins. The CYD's speaker amplifier hangs off GPIO26. */
#define TINY_SPEAKER_PIN 26
#endif

/*
 * Some boards gate the speaker amplifier with its own pin, and leave it off while that pin
 * floats. Define TINY_SPEAKER_ENABLE_PIN to drive it to TINY_SPEAKER_ENABLE_LEVEL (low by
 * default) before any sound plays. The single USB-C revision of the CYD needs GPIO4 low.
 */
#ifndef TINY_SPEAKER_ENABLE_LEVEL
#define TINY_SPEAKER_ENABLE_LEVEL 0
#endif
#define TINY_AUDIO_BLOCK 256

static tiny_game* _tiny_esp_game;
static TaskHandle_t _tiny_audio_task;
static dac_continuous_handle_t _tiny_dac;

static void _tiny_audio_loop(void* argument) {
  (void)argument;
  int16_t block[TINY_AUDIO_BLOCK];
  uint8_t out[TINY_AUDIO_BLOCK];
  for (;;) {
    tiny_audio_render(_tiny_esp_game, block, TINY_AUDIO_BLOCK);
    /* The DAC is 8 bit unsigned, so silence is 128. */
    for (int i = 0; i < TINY_AUDIO_BLOCK; i++) {
      out[i] = (uint8_t)((block[i] >> 8) + 128);
    }
    dac_continuous_write(_tiny_dac, out, TINY_AUDIO_BLOCK, NULL, -1);
  }
}

static void _tiny_audio_open(tiny_game* game) {
  _tiny_esp_game = game;
#ifdef TINY_SPEAKER_ENABLE_PIN
  gpio_reset_pin((gpio_num_t)TINY_SPEAKER_ENABLE_PIN);
  gpio_set_direction((gpio_num_t)TINY_SPEAKER_ENABLE_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)TINY_SPEAKER_ENABLE_PIN, TINY_SPEAKER_ENABLE_LEVEL);
#endif
  dac_continuous_config_t config = {
      .chan_mask = TINY_SPEAKER_PIN == 25 ? DAC_CHANNEL_MASK_CH0 : DAC_CHANNEL_MASK_CH1,
      .desc_num = 4,
      .buf_size = TINY_AUDIO_BLOCK,
      .freq_hz = TINY_AUDIO_RATE,
      .offset = 0,
      .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
      .chan_mode = DAC_CHANNEL_MODE_SIMUL,
  };
  if (dac_continuous_new_channels(&config, &_tiny_dac) != ESP_OK || dac_continuous_enable(_tiny_dac) != ESP_OK) {
#ifndef TINY_NO_STDIO
    _tiny_log(PNTR_APP_LOG_ERROR, "tinyengine: could not open the DAC on GPIO%d", TINY_SPEAKER_PIN);
#endif
    _tiny_dac = NULL;
    return;
  }
  xTaskCreatePinnedToCore(_tiny_audio_loop, "tiny_audio", 4096, NULL, 5, &_tiny_audio_task, 0);
}
static void _tiny_audio_pump(tiny_game* game) { (void)game; }

static void _tiny_audio_close(void) {
  if (_tiny_audio_task != NULL) {
    vTaskDelete(_tiny_audio_task);
    _tiny_audio_task = NULL;
  }
  if (_tiny_dac != NULL) {
    dac_continuous_disable(_tiny_dac);
    dac_continuous_del_channels(_tiny_dac);
    _tiny_dac = NULL;
  }
}
#endif

/*-- touch controls -------------------------------------------------------------------------*/

#ifdef TINY_TOUCH_CONTROLS
/*
 * A d-pad bottom left and A/B bottom right, scaled off the short side of the screen. The
 * d-pad answers anywhere near it, diagonals included, so a thumb can slide around on it
 * without having to land on an arm exactly.
 */
typedef struct _tiny_touch_layout {
  float unit;
  float dpad_x, dpad_y;
  float a_x, a_y;
  float b_x, b_y;
} _tiny_touch_layout;

static _tiny_touch_layout _tiny_touch_place(tiny_game* game) {
  _tiny_touch_layout layout;
  float unit = (float)TINY_MIN(game->screen_w, game->screen_h) / 8.0f;
  float margin = 4.0f;
  layout.unit = unit;
  layout.dpad_x = margin + unit * 1.5f;
  layout.dpad_y = (float)game->screen_h - margin - unit * 1.5f;
  layout.a_x = (float)game->screen_w - margin - unit * 0.8f;
  layout.a_y = (float)game->screen_h - margin - unit * 2.0f;
  layout.b_x = (float)game->screen_w - margin - unit * 2.2f;
  layout.b_y = (float)game->screen_h - margin - unit * 0.9f;
  return layout;
}

/* The gamepad buttons one finger at (x, y) is holding, as TINY_BUTTON_BIT bits. */
static uint32_t _tiny_touch_hit(const _tiny_touch_layout* layout, float x, float y) {
  float unit = layout->unit;
  float dx = x - layout->dpad_x;
  float dy = y - layout->dpad_y;
  if (dx * dx + dy * dy < unit * unit * 2.2f * 2.2f) {
    uint32_t held = 0;
    float dead = unit * 0.35f;
    if (dx < -dead && -dx > fabsf(dy) * 0.5f) {
      held |= TINY_BUTTON_BIT(PNTR_APP_GAMEPAD_BUTTON_LEFT);
    }
    if (dx > dead && dx > fabsf(dy) * 0.5f) {
      held |= TINY_BUTTON_BIT(PNTR_APP_GAMEPAD_BUTTON_RIGHT);
    }
    if (dy < -dead && -dy > fabsf(dx) * 0.5f) {
      held |= TINY_BUTTON_BIT(PNTR_APP_GAMEPAD_BUTTON_UP);
    }
    if (dy > dead && dy > fabsf(dx) * 0.5f) {
      held |= TINY_BUTTON_BIT(PNTR_APP_GAMEPAD_BUTTON_DOWN);
    }
    return held;
  }

  /* The two buttons' reach overlaps, so the nearer one wins. */
  float ax = x - layout->a_x, ay = y - layout->a_y;
  float bx = x - layout->b_x, by = y - layout->b_y;
  float a = ax * ax + ay * ay;
  float b = bx * bx + by * by;
  float reach = unit * unit * 1.1f * 1.1f;
  if (a < reach && a <= b) {
    return TINY_BUTTON_BIT(PNTR_APP_GAMEPAD_BUTTON_A);
  }
  if (b < reach) {
    return TINY_BUTTON_BIT(PNTR_APP_GAMEPAD_BUTTON_B);
  }
  return 0;
}

#ifdef PNTR_APP_WEB
#include <emscripten/html5.h>

/*
 * pntr_app's web backend only listens for the mouse, and a phone only makes mouse events
 * for a tap: nothing while a finger is held down, and never for two fingers. So the engine
 * reads touches itself, which is what lets you hold the d-pad and press A at the same time.
 */
EM_JS(float, _tiny_web_canvas_scale_x, (int width), { return width / Module['canvas'].getBoundingClientRect().width; });
EM_JS(float, _tiny_web_canvas_scale_y, (int height), { return height / Module['canvas'].getBoundingClientRect().height; });

static EM_BOOL _tiny_web_touch(int type, const EmscriptenTouchEvent* event, void* user) {
  tiny_game* game = (tiny_game*)user;
  bool ending = type == EMSCRIPTEN_EVENT_TOUCHEND || type == EMSCRIPTEN_EVENT_TOUCHCANCEL;
  float sx = _tiny_web_canvas_scale_x(game->screen_w);
  float sy = _tiny_web_canvas_scale_y(game->screen_h);
  game->touch_count = 0;
  for (int i = 0; i < event->numTouches && game->touch_count < TINY_MAX_TOUCHES; i++) {
    const EmscriptenTouchPoint* touch = &event->touches[i];
    /* On a touchend, the changed touches are the fingers that just lifted. */
    if (ending && touch->isChanged) {
      continue;
    }
    game->touch_x[game->touch_count] = (float)touch->targetX * sx;
    game->touch_y[game->touch_count] = (float)touch->targetY * sy;
    game->touch_count++;
  }
  /* A touch screen has shown itself, so the controls are worth drawing now. */
  game->touch_visible = true;
  /* Handled, which also stops the browser scrolling and faking mouse events from it. */
  return EM_TRUE;
}

static void _tiny_touch_open(tiny_game* game) {
  emscripten_set_touchstart_callback("!canvas", game, true, _tiny_web_touch);
  emscripten_set_touchmove_callback("!canvas", game, true, _tiny_web_touch);
  emscripten_set_touchend_callback("!canvas", game, true, _tiny_web_touch);
  emscripten_set_touchcancel_callback("!canvas", game, true, _tiny_web_touch);
}
#else
/* Elsewhere a touch screen arrives as the mouse, and there is no keyboard to fall back on. */
static void _tiny_touch_open(tiny_game* game) { game->touch_visible = true; }
#endif

static void _tiny_touch_update(tiny_game* game) {
  _tiny_touch_layout layout = _tiny_touch_place(game);
  game->touch_buttons = 0;
  for (int i = 0; i < game->touch_count; i++) {
    game->touch_buttons |= _tiny_touch_hit(&layout, game->touch_x[i], game->touch_y[i]);
  }
  if (pntr_app_mouse_button_down(game->app, PNTR_APP_MOUSE_BUTTON_LEFT)) {
    game->touch_buttons |= _tiny_touch_hit(&layout, pntr_app_mouse_x(game->app), pntr_app_mouse_y(game->app));
  }
}

/*
 * One span per row, so a translucent colour covers every pixel exactly once. pntr's own
 * circle and rounded fills paint some rows twice, which shows as a brighter stripe.
 */
static void _tiny_fill_circle(pntr_image* screen, int cx, int cy, int radius, pntr_color color) {
  for (int dy = -radius; dy <= radius; dy++) {
    int half = (int)sqrtf((float)(radius * radius - dy * dy));
    pntr_draw_rectangle_fill(screen, cx - half, cy + dy, half * 2 + 1, 1, color);
  }
}

static void _tiny_draw_touch(tiny_game* game, pntr_image* screen) {
  if (!game->touch_visible) {
    return;
  }
  _tiny_touch_layout layout = _tiny_touch_place(game);
  pntr_color idle = pntr_new_color(255, 255, 255, 60);
  pntr_color down = pntr_new_color(255, 255, 255, 150);
  int unit = (int)layout.unit;
  int cx = (int)layout.dpad_x - unit / 2;
  int cy = (int)layout.dpad_y - unit / 2;

  /* The d-pad: a centre square and four arms, each lit while it is held. */
  static const struct {
    int dx, dy;
    pntr_app_gamepad_button button;
  } arms[] = {{-1, 0, PNTR_APP_GAMEPAD_BUTTON_LEFT},
              {1, 0, PNTR_APP_GAMEPAD_BUTTON_RIGHT},
              {0, -1, PNTR_APP_GAMEPAD_BUTTON_UP},
              {0, 1, PNTR_APP_GAMEPAD_BUTTON_DOWN}};
  pntr_draw_rectangle_fill(screen, cx, cy, unit, unit, idle);
  for (int i = 0; i < 4; i++) {
    bool held = (game->touch_buttons & TINY_BUTTON_BIT(arms[i].button)) != 0;
    pntr_draw_rectangle_fill(screen, cx + arms[i].dx * unit, cy + arms[i].dy * unit, unit, unit, held ? down : idle);
  }

  static const char* labels[] = {"A", "B"};
  float xs[] = {layout.a_x, layout.b_x};
  float ys[] = {layout.a_y, layout.b_y};
  pntr_app_gamepad_button buttons[] = {PNTR_APP_GAMEPAD_BUTTON_A, PNTR_APP_GAMEPAD_BUTTON_B};
  for (int i = 0; i < 2; i++) {
    bool held = (game->touch_buttons & TINY_BUTTON_BIT(buttons[i])) != 0;
    _tiny_fill_circle(screen, (int)xs[i], (int)ys[i], (int)(layout.unit * 0.8f), held ? down : idle);
    if (game->font != NULL) {
      pntr_vector size = pntr_measure_text_ex(game->font, labels[i], 1);
      pntr_draw_text(screen, game->font, labels[i], (int)xs[i] - size.x / 2, (int)ys[i] - size.y / 2, PNTR_WHITE);
    }
  }
}
#endif

/*-- input ----------------------------------------------------------------------------------*/

bool tiny_held(tiny_game* game, pntr_app_key key, pntr_app_gamepad_button button) {
  return pntr_app_key_down(game->app, key) || pntr_app_gamepad_button_down(game->app, 0, button) ||
         (game->touch_buttons & TINY_BUTTON_BIT(button)) != 0;
}

float tiny_axis_x(tiny_game* game) {
  return (tiny_held(game, PNTR_APP_KEY_RIGHT, PNTR_APP_GAMEPAD_BUTTON_RIGHT) ? 1.0f : 0.0f) -
         (tiny_held(game, PNTR_APP_KEY_LEFT, PNTR_APP_GAMEPAD_BUTTON_LEFT) ? 1.0f : 0.0f);
}

float tiny_axis_y(tiny_game* game) {
  return (tiny_held(game, PNTR_APP_KEY_DOWN, PNTR_APP_GAMEPAD_BUTTON_DOWN) ? 1.0f : 0.0f) -
         (tiny_held(game, PNTR_APP_KEY_UP, PNTR_APP_GAMEPAD_BUTTON_UP) ? 1.0f : 0.0f);
}

/*-- messages -------------------------------------------------------------------------------*/

void tiny_say(tiny_game* game, const char* text, float seconds) {
  game->message = text;
  game->message_timer = seconds;
}

/*
 * Greedy word wrap. A whole sentence does not fit across a small screen, and the default
 * font has no smaller size, so the only way to show one is to break it over several lines.
 *
 * Pass a NULL screen to count the lines without drawing, which is how the box behind the
 * text gets sized before the text goes into it.
 */
static int _tiny_wrap(pntr_image* screen, pntr_font* font, const char* text, int x, int y, int max_width,
                      int* widest) {
  char line[80];
  int length = 0;
  int lines = 0;
  int line_height = pntr_measure_text_ex(font, "A", 1).y + 1;
  const char* cursor = text;
  *widest = 0;

  while (*cursor != '\0' || length > 0) {
    if (*cursor != '\0') {
      const char* word_end = cursor;
      while (*word_end != '\0' && *word_end != ' ') {
        word_end++;
      }
      int word_length = (int)(word_end - cursor);

      /* Try the word on the current line, with a space in front of it if it is not the first. */
      int end = length > 0 ? length + 1 : 0;
      int take = TINY_MIN(word_length, (int)sizeof(line) - 1 - end);
      if (take == word_length || length == 0) {
        if (length > 0) {
          line[length] = ' ';
        }
        TINY_MEMCPY(line + end, cursor, (size_t)take);
        line[end + take] = '\0';

        /* A word wider than the whole box still goes on its own line rather than looping. */
        if (length == 0 || pntr_measure_text(font, line) <= max_width) {
          length = end + take;
          cursor = (*word_end == ' ') ? word_end + 1 : word_end;
          continue;
        }
      }
      line[length] = '\0';
    }

    if (screen != NULL) {
      pntr_draw_text(screen, font, line, x, y + lines * line_height, PNTR_WHITE);
    }
    *widest = TINY_MAX(*widest, pntr_measure_text(font, line));
    lines++;
    length = 0;
  }
  return lines;
}

static void _tiny_draw_message(tiny_game* game, pntr_image* screen) {
  if (game->message_timer <= 0.0f || game->message == NULL || game->font == NULL) {
    return;
  }
  int max_width = screen->width - 12;
  int line_height = pntr_measure_text_ex(game->font, "A", 1).y + 1;
  int widest = 0;
  int lines = _tiny_wrap(NULL, game->font, game->message, 0, 0, max_width, &widest);

  /* The box hugs the text rather than spanning the screen, so a short word is not a full bar. */
  int height = lines * line_height + 5;
  int width = widest + 6;
  int left = (screen->width - width) / 2;
  int top = screen->height - height - 3;

  pntr_draw_rectangle_fill(screen, left, top, width, height, PNTR_BLACK);
  _tiny_wrap(screen, game->font, game->message, left + 3, top + 3, max_width, &widest);
}

/*-- portals --------------------------------------------------------------------------------*/

/*
 * A portal has to see the player standing clear of it before it will fire. Landing on a
 * doorway therefore does nothing until you walk out of it, which is what stops a door you
 * just came through from throwing you straight back -- otherwise holding a direction bounces
 * you between two maps forever.
 */
static void _tiny_portal(tiny_game* game, tiny_object* obj) {
  if (game->player == NULL || !tiny_overlaps(obj, game->player)) {
    obj->portal = 1;
    return;
  }
  if (obj->portal == 0) {
    return;
  }
  obj->portal = 0;
  const char* destination = tiny_obj_str(obj, "destination", NULL);
  if (destination != NULL && tiny_goto_map(game, destination, tiny_obj_str(obj, "spawn", NULL))) {
    const char* sound = tiny_obj_str(obj, "sound", NULL);
    if (sound != NULL) {
      tiny_sound_play(game, sound, false);
    }
  }
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

/*
 * pntr_app's web backend drops every keystroke when the app has no event hook, before it
 * reaches the keysDown[] array the engine polls. An empty hook is enough to keep them.
 */
static void _tiny_ignore_event(pntr_app* app, pntr_app_event* event) {
  (void)app;
  (void)event;
}

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
  game->sounds = def->sounds;
  game->sound_count = def->sound_count;
  game->screen_w = pntr_app_width(app);
  game->screen_h = pntr_app_height(app);
  game->font = pntr_load_font_default();
  if (app->event == NULL) {
    app->event = _tiny_ignore_event;
  }
  _tiny_audio_open(game);
#ifdef TINY_TOUCH_CONTROLS
  _tiny_touch_open(game);
#endif

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
  _tiny_audio_close();
  for (int i = 0; i < TINY_MAX_VOICES; i++) {
    if (game->voices[i].active) {
      _tiny_voice_stop(&game->voices[i]);
    }
  }
  for (int i = 0; i < TINY_MAX_SFX; i++) {
    pntr_unload_memory(game->sfx[i].samples);
  }
  pntr_unload_font(game->font);
  pntr_unload_memory(game->objects);
  pntr_unload_memory(game);
}

void tiny_update(tiny_game* game, float dt) {
  if (game == NULL || game->map == NULL) {
    return;
  }
  dt = TINY_MIN(dt, 0.05f);
  game->time += dt;
  game->anim_time += dt;
  game->message_timer -= dt;
#ifdef TINY_TOUCH_CONTROLS
  _tiny_touch_update(game);
#endif

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
    if (obj->active && tiny_is(obj, "portal")) {
      _tiny_portal(game, obj);
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

  _tiny_audio_pump(game);
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
  _tiny_draw_message(game, screen);
#ifdef TINY_TOUCH_CONTROLS
  _tiny_draw_touch(game, screen);
#endif
}

#ifdef __cplusplus
}
#endif

#endif  // TINY_ENGINE_IMPLEMENTATION_ONCE
#endif  // TINY_ENGINE_IMPLEMENTATION
