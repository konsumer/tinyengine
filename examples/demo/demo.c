/**
 * tinyengine demo -- one game, five kinds of map, one behaviour function.
 *
 * Everything the game does is in Behaviour(), below. It runs once per object per frame and
 * dispatches on the object's Class from Tiled, so adding a new kind of thing means drawing
 * it in Tiled, giving it a class, and adding a branch here.
 *
 * The maps link to each other through portal objects: walk into one, the engine reads its
 * `destination` and `spawn` properties, and the next map's map_type picks a different
 * camera and different physics without any of this code changing.
 */

#include <math.h>
#include <stdio.h>

/*
 * These four have to be included in this order, so they are fenced off from clang-format's
 * include sorting: each header's implementation is switched on by a #define above it, and
 * the baked maps need tinyengine's types to already exist.
 *
 * DEMO_HEADLESS is set by tools/tiny_preview.c, which drives this same file without a
 * window in order to render frames to PNG. It supplies its own pntr_app backend stubs.
 */
/* clang-format off */
#ifndef DEMO_HEADLESS
#define PNTR_APP_IMPLEMENTATION
#endif
#ifndef PNTR_ENABLE_DEFAULT_FONT
#define PNTR_ENABLE_DEFAULT_FONT
#endif
#include "pntr_app.h"

/* The procedural sfx synth. tinyengine plays a baked SfxParams through it, and only if this
   header was included first: the engine checks for PNTR_APP_SFX_H__ before it uses it. */
#define PNTR_ENABLE_MATH
#define PNTR_APP_SFX_IMPLEMENTATION
#include "pntr_app_sfx.h"

#define TINY_ENGINE_IMPLEMENTATION
#include "tinyengine.h"

/* Baked from examples/demo/assets by tools/tiny_bake.py. Include it exactly once. */
#include "generated/game_maps.h"
/* clang-format on */

static int score;

/* Which way the player last faced, so standing still keeps the right sprite. */
enum { FACE_DOWN = 0,
       FACE_UP,
       FACE_SIDE };

#define WALK_SPEED 44.0f /* Pixels per second on an orthogonal map. */
#define ISO_SPEED 2.6f   /* Tiles per second on an isometric one. */
#define JUMP_SPEED 175.0f

/* Anything that hurts the player starts the map over, back where the map put them. */
static void Hurt(tiny_game* game, const char* message) {
  tiny_goto_map(game, game->map->name, NULL);
  tiny_sound_play(game, "hurt", false);
  tiny_say(game, message, 2.5f);
}

/*--------------------------------------------------------------------------------------------
 * The player, which is where the five map types actually feel different.
 *------------------------------------------------------------------------------------------*/

static void PlayerPlatformer(tiny_game* game, tiny_object* self, float dt) {
  float axis = tiny_axis_x(game);
  self->vx = axis * WALK_SPEED * 1.6f;

  if (game->gravity > 0.0f) {
    /* Up jumps too, so a single-touch screen can run and jump with one finger on the d-pad. */
    bool jump = tiny_held(game, PNTR_APP_KEY_SPACE, PNTR_APP_GAMEPAD_BUTTON_A) ||
                tiny_held(game, PNTR_APP_KEY_Z, PNTR_APP_GAMEPAD_BUTTON_B) || tiny_axis_y(game) < 0.0f;
    if (self->on_ground && jump) {
      self->vy = -JUMP_SPEED;
      self->on_ground = false;
      tiny_sound_play(game, "jump", false);
    }
    /* tiny_step applies the map's gravity and slides along whatever it hits. */
    tiny_step(game, self, dt);
  } else {
    /*
     * The map turned gravity off, so this is a side shooter rather than a platformer.
     * Nothing falls, a jump has nothing to push against, and up/down simply fly.
     */
    self->vy = tiny_axis_y(game) * WALK_SPEED * 1.6f;
    tiny_move(game, self, self->vx * dt, self->vy * dt);
  }

  if (axis != 0.0f) {
    self->flip_x = axis < 0.0f;
    tiny_play(game, self, "player_side");
  } else {
    tiny_pose(game, self, "player_side", 0);
  }

  if (game->gravity <= 0.0f) {
    return;
  }

  /* Standing in the lava, or fallen out of the map entirely: start the map over. */
  int tx, ty;
  tiny_world_to_tile(game, self->x + self->w * 0.5f, self->y + self->h - 1.0f, &tx, &ty);
  bool burned = (tiny_flags_at(game, tx, ty) & TINY_TILE_HAZARD) != 0;
  if (burned || self->y > (float)(game->map->height * game->map->tile_h)) {
    Hurt(game, "ouch!");
  }
}

static void PlayerTopDown(tiny_game* game, tiny_object* self, float dt) {
  float dx = tiny_axis_x(game);
  float dy = tiny_axis_y(game);
  /* Normalise the diagonal so walking at an angle is not faster than walking straight. */
  if (dx != 0.0f && dy != 0.0f) {
    dx *= 0.7071f;
    dy *= 0.7071f;
  }

  tiny_move(game, self, dx * WALK_SPEED * dt, dy * WALK_SPEED * dt);

  if (dy < 0.0f) {
    self->state = FACE_UP;
  } else if (dx != 0.0f) {
    self->state = FACE_SIDE;
  } else if (dy > 0.0f) {
    self->state = FACE_DOWN;
  }
  if (dx != 0.0f) {
    self->flip_x = dx < 0.0f;
  }

  const char* anim = self->state == FACE_UP     ? "player_up"
                     : self->state == FACE_SIDE ? "player_side"
                                                : "player_down";

  /* Frame 0 of a walk cycle is the standing pose, so a still player stops animating. */
  if (dx != 0.0f || dy != 0.0f) {
    tiny_play(game, self, anim);
  } else {
    tiny_pose(game, self, anim, 0);
  }

  int tx, ty;
  tiny_world_to_tile(game, self->x + self->w * 0.5f, self->y + self->h * 0.5f, &tx, &ty);
  if ((tiny_flags_at(game, tx, ty) & TINY_TILE_HAZARD) != 0) {
    Hurt(game, "ouch!");
  }
}

static void PlayerIsometric(tiny_game* game, tiny_object* self, float dt) {
  /*
   * On screen the world is rotated 45 degrees, so the arrow keys are rotated to match:
   * pressing up should walk towards the top of the screen, not along a tile axis.
   */
  float sx = tiny_axis_x(game);
  float sy = tiny_axis_y(game);
  float dx = (sy + sx) * 0.7071f;
  float dy = (sy - sx) * 0.7071f;

  tiny_move(game, self, dx * ISO_SPEED * dt, dy * ISO_SPEED * dt);
  self->flip_x = sx < 0.0f;
}

static void Player(tiny_game* game, tiny_object* self, float dt) {
  switch (game->map->type) {
    case TINY_MAP_PLATFORMER:
      PlayerPlatformer(game, self, dt);
      break;
    case TINY_MAP_ISOMETRIC:
      PlayerIsometric(game, self, dt);
      break;
    default:
      PlayerTopDown(game, self, dt);
      break;
  }
}

/*--------------------------------------------------------------------------------------------
 * Everything else
 *------------------------------------------------------------------------------------------*/

/* Walks in one direction until something stops it, then turns around. */
static void Patrol(tiny_game* game, tiny_object* self, float dt) {
  float speed = tiny_obj_float(self, "speed", 20.0f);
  if (self->state == 0) {
    self->state = 1;
    tiny_play(game, self, self->def->tile ? "enemy" : NULL);
  }

  float dx = (float)self->state * speed * dt;

  if (game->map->type == TINY_MAP_PLATFORMER && game->gravity > 0.0f) {
    /* Turn at a ledge as well as at a wall, so nothing walks off into the lava. A one-way
       platform is floor too, even though it is not solid. */
    int tx, ty;
    tiny_world_to_tile(game, self->state > 0 ? self->x + self->w + 1.0f : self->x - 1.0f, self->y + self->h + 1.0f,
                       &tx, &ty);
    if ((tiny_flags_at(game, tx, ty) & (TINY_TILE_SOLID | TINY_TILE_PLATFORM)) == 0) {
      self->state = -self->state;
      dx = -dx;
    }
    tiny_move(game, self, dx, 0.0f);
    self->vy += game->gravity * dt;
    tiny_move(game, self, 0.0f, self->vy * dt);
  } else {
    tiny_move(game, self, dx, 0.0f);
  }

  if (self->hit & (TINY_HIT_LEFT | TINY_HIT_RIGHT)) {
    self->state = -self->state;
  }
  self->flip_x = self->state < 0;
}

/* Steps towards the player on whichever axis is furthest away, and gives up if blocked. */
static void Chase(tiny_game* game, tiny_object* self, float dt) {
  tiny_object* player = game->player;
  /* game->time restarts with the map, so this gives the player a head start after a death. */
  if (player == NULL || game->time < 1.5f) {
    return;
  }
  float speed = tiny_obj_float(self, "speed", 24.0f);
  float dx = player->x - self->x;
  float dy = player->y - self->y;

  if (fabsf(dx) > fabsf(dy)) {
    tiny_move(game, self, (dx > 0.0f ? 1.0f : -1.0f) * speed * dt, 0.0f);
    if (self->hit != 0) {
      tiny_move(game, self, 0.0f, (dy > 0.0f ? 1.0f : -1.0f) * speed * dt);
    }
  } else {
    tiny_move(game, self, 0.0f, (dy > 0.0f ? 1.0f : -1.0f) * speed * dt);
    if (self->hit != 0) {
      tiny_move(game, self, (dx > 0.0f ? 1.0f : -1.0f) * speed * dt, 0.0f);
    }
  }
  self->flip_x = dx < 0.0f;
}

void Behaviour(tiny_game* game, tiny_object* self, float dt) {
  if (tiny_is(self, "player")) {
    Player(game, self, dt);
    return;
  }

  if (tiny_is(self, "enemy")) {
    Patrol(game, self, dt);
    if (tiny_overlaps(self, game->player)) {
      Hurt(game, "bumped an enemy");
    }
    return;
  }

  if (tiny_is(self, "ghost")) {
    Chase(game, self, dt);
    if (tiny_overlaps(self, game->player)) {
      Hurt(game, "caught!");
    }
    return;
  }

  if (tiny_is(self, "coin") || tiny_is(self, "pellet")) {
    /* The bob is cosmetic: nudge the sprite, leave the collision box where it was. */
    self->y += sinf(game->time * 4.0f + (float)self->def->id) * 4.0f * dt;
    if (tiny_overlaps(self, game->player)) {
      score += tiny_is(self, "coin") ? 10 : 1;
      tiny_sound_play(game, "coin", false);
      tiny_kill(game, self);
    }
    return;
  }

  if (tiny_is(self, "npc")) {
    /*
     * Say your piece when the player is right next to you, which is as much dialogue as this
     * demo has. state remembers they were already close, so the blip plays once per visit.
     */
    if (game->player != NULL) {
      float dx = game->player->x - self->x;
      float dy = game->player->y - self->y;
      bool close = dx * dx + dy * dy < 12.0f * 12.0f;
      if (close) {
        if (self->state == 0) {
          tiny_sound_play(game, "blip", false);
        }
        tiny_say(game, tiny_obj_str(self, "says", "..."), 2.5f);
      }
      self->state = close ? 1 : 0;
    }
    return;
  }
}

/*--------------------------------------------------------------------------------------------
 * The HUD, and pntr_app glue
 *------------------------------------------------------------------------------------------*/

void Hud(tiny_game* game, pntr_image* screen) {
  char line[48];
  snprintf(line, sizeof(line), "%s  %d", game->map->name, score);
  pntr_draw_text(screen, game->font, line, 3, 3, PNTR_BLACK);
  pntr_draw_text(screen, game->font, line, 2, 2, PNTR_WHITE);
}

static const tiny_game_def DEMO = {
    .maps = TINY_MAPS,
    .map_count = TINY_MAP_COUNT,
    .start_map = "town",
    .sounds = TINY_SOUNDS,
    .sound_count = TINY_SOUND_COUNT,
    .object = Behaviour,
    .draw = Hud};

bool Init(pntr_app* app) {
  tiny_game* game = tiny_init(app, &DEMO, NULL);
  pntr_app_set_userdata(app, game);
  tiny_sound_play(game, "theme", true);
  return game != NULL;
}

bool Update(pntr_app* app, pntr_image* screen) {
  tiny_game* game = (tiny_game*)pntr_app_userdata(app);
  tiny_update(game, pntr_app_delta_time(app));
  tiny_draw(game, screen);
  return true;
}

void Close(pntr_app* app) {
  tiny_unload((tiny_game*)pntr_app_userdata(app));
}

pntr_app Main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  return (pntr_app){
      .width = 160,
      .height = 120,
      .title = "tinyengine demo",
      .init = Init,
      .update = Update,
      .close = Close,
      .fps = 60};
}
