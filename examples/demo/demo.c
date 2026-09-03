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
#include <string.h>

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

#define TINY_ENGINE_IMPLEMENTATION
#include "tinyengine.h"

/* Baked from examples/demo/assets by tools/tiny_bake.py. Include it exactly once. */
#include "generated/game_maps.h"
/* clang-format on */

typedef struct DemoState {
  pntr_font* font;
  int score;
  float message_timer;
  const char* message;
} DemoState;

/* Which way the player last faced, so standing still keeps the right sprite. */
enum { FACE_DOWN = 0,
       FACE_UP,
       FACE_SIDE };

/* A portal's arming state, kept in tiny_object::state. */
enum { PORTAL_NEW = 0,
       PORTAL_READY,
       PORTAL_BLOCKED };

#define WALK_SPEED 44.0f /* Pixels per second on an orthogonal map. */
#define ISO_SPEED 2.6f   /* Tiles per second on an isometric one. */
#define JUMP_SPEED 175.0f

/*--------------------------------------------------------------------------------------------
 * Input, read straight off the pntr_app the engine is holding.
 *------------------------------------------------------------------------------------------*/

static bool Held(tiny_game* game, pntr_app_key key, pntr_app_gamepad_button button) {
  return pntr_app_key_down(game->app, key) || pntr_app_gamepad_button_down(game->app, 0, button);
}

static float AxisX(tiny_game* game) {
  float axis = 0.0f;
  if (Held(game, PNTR_APP_KEY_LEFT, PNTR_APP_GAMEPAD_BUTTON_LEFT)) {
    axis -= 1.0f;
  }
  if (Held(game, PNTR_APP_KEY_RIGHT, PNTR_APP_GAMEPAD_BUTTON_RIGHT)) {
    axis += 1.0f;
  }
  return axis;
}

static float AxisY(tiny_game* game) {
  float axis = 0.0f;
  if (Held(game, PNTR_APP_KEY_UP, PNTR_APP_GAMEPAD_BUTTON_UP)) {
    axis -= 1.0f;
  }
  if (Held(game, PNTR_APP_KEY_DOWN, PNTR_APP_GAMEPAD_BUTTON_DOWN)) {
    axis += 1.0f;
  }
  return axis;
}

static bool JumpPressed(tiny_game* game) {
  return Held(game, PNTR_APP_KEY_SPACE, PNTR_APP_GAMEPAD_BUTTON_A) ||
         Held(game, PNTR_APP_KEY_Z, PNTR_APP_GAMEPAD_BUTTON_B);
}

static void Say(tiny_game* game, const char* message) {
  DemoState* state = (DemoState*)game->user;
  state->message = message;
  state->message_timer = 2.5f;
}

/*--------------------------------------------------------------------------------------------
 * The player, which is where the five map types actually feel different.
 *------------------------------------------------------------------------------------------*/

static void PlayerPlatformer(tiny_game* game, tiny_object* self, float dt) {
  float axis = AxisX(game);
  self->vx = axis * WALK_SPEED * 1.6f;

  if (game->gravity > 0.0f) {
    if (self->on_ground && JumpPressed(game)) {
      self->vy = -JUMP_SPEED;
      self->on_ground = false;
    }
    /* tiny_step applies the map's gravity and slides along whatever it hits. */
    tiny_step(game, self, dt);
  } else {
    /*
     * The map turned gravity off, so this is a side shooter rather than a platformer.
     * Nothing falls, a jump has nothing to push against, and up/down simply fly.
     */
    self->vy = AxisY(game) * WALK_SPEED * 1.6f;
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
    tiny_goto_map(game, game->map->name, "from_town");
    Say(game, "ouch");
  }
}

static void PlayerTopDown(tiny_game* game, tiny_object* self, float dt) {
  float dx = AxisX(game);
  float dy = AxisY(game);
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
}

static void PlayerIsometric(tiny_game* game, tiny_object* self, float dt) {
  /*
   * On screen the world is rotated 45 degrees, so the arrow keys are rotated to match:
   * pressing up should walk towards the top of the screen, not along a tile axis.
   */
  float sx = AxisX(game);
  float sy = AxisY(game);
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
    /* Turn at a ledge as well as at a wall, so nothing walks off into the lava. */
    float ahead = self->state > 0 ? self->x + self->w + 1.0f : self->x - 1.0f;
    if (!tiny_solid(game, ahead, self->y + self->h + 1.0f)) {
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
  if (player == NULL) {
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
  DemoState* state = (DemoState*)game->user;

  if (tiny_is(self, "player")) {
    Player(game, self, dt);
    return;
  }

  if (tiny_is(self, "enemy")) {
    Patrol(game, self, dt);
    if (tiny_overlaps(self, game->player)) {
      Say(game, "bumped an enemy");
    }
    return;
  }

  if (tiny_is(self, "ghost")) {
    Chase(game, self, dt);
    if (tiny_overlaps(self, game->player)) {
      Say(game, "caught!");
    }
    return;
  }

  if (tiny_is(self, "coin") || tiny_is(self, "pellet")) {
    /* The bob is cosmetic: nudge the sprite, leave the collision box where it was. */
    self->y += sinf(game->time * 4.0f + (float)self->def->id) * 4.0f * dt;
    if (tiny_overlaps(self, game->player)) {
      state->score += tiny_is(self, "coin") ? 10 : 1;
      tiny_kill(game, self);
    }
    return;
  }

  if (tiny_is(self, "portal")) {
    /*
     * A portal has to see the player standing clear of it before it will fire. Landing on a
     * doorway therefore does nothing until you walk out of it, which is what stops a door
     * you just came through from throwing you straight back -- otherwise holding a direction
     * bounces you between two maps forever.
     */
    if (!tiny_overlaps(self, game->player)) {
      self->state = PORTAL_READY;
    } else if (self->state == PORTAL_NEW) {
      self->state = PORTAL_BLOCKED;
    } else if (self->state == PORTAL_READY) {
      const char* destination = tiny_obj_str(self, "destination", NULL);
      if (destination != NULL) {
        tiny_goto_map(game, destination, tiny_obj_str(self, "spawn", NULL));
      }
      self->state = PORTAL_BLOCKED;
    }
    return;
  }

  if (tiny_is(self, "npc")) {
    /* Say your piece when the player is close, which is as much dialogue as this demo has. */
    if (game->player != NULL) {
      float dx = game->player->x - self->x;
      float dy = game->player->y - self->y;
      if (dx * dx + dy * dy < 24.0f * 24.0f) {
        Say(game, tiny_obj_str(self, "says", "..."));
      }
    }
    return;
  }
}

/*--------------------------------------------------------------------------------------------
 * The overlay, and the two hooks the demo bothers with
 *------------------------------------------------------------------------------------------*/

void Enter(tiny_game* game) {
  DemoState* state = (DemoState*)game->user;
  state->message = game->map->name;
  state->message_timer = 1.5f;
}

/*
 * Greedy word wrap. A whole sentence does not fit across 160 pixels, and the default font
 * has no smaller size, so the only way to show one is to break it over several lines.
 *
 * Pass a NULL screen to count the lines without drawing, which is how the box behind the
 * text gets sized before the text goes into it.
 */
static int WrapText(pntr_image* screen, pntr_font* font, const char* text, int x, int y, int max_width,
                    int* widest) {
  char line[80];
  int length = 0;
  int lines = 0;
  int line_height = pntr_measure_text_ex(font, "A", 1).y + 1;
  const char* cursor = text;
  if (widest != NULL) {
    *widest = 0;
  }

  while (*cursor != '\0') {
    const char* word_end = cursor;
    while (*word_end != '\0' && *word_end != ' ') {
      word_end++;
    }
    int word_length = (int)(word_end - cursor);

    /* Try the word on the current line, with a space in front of it if it is not the first. */
    int needed = length + (length > 0 ? 1 : 0) + word_length + 1;
    if (needed <= (int)sizeof(line)) {
      int end = length;
      if (end > 0) {
        line[end++] = ' ';
      }
      memcpy(line + end, cursor, (size_t)word_length);
      line[end + word_length] = '\0';

      /* A word wider than the whole box still goes on its own line rather than looping. */
      if (length == 0 || pntr_measure_text(font, line) <= max_width) {
        length = end + word_length;
        cursor = (*word_end == ' ') ? word_end + 1 : word_end;
        continue;
      }
      line[length] = '\0';
    }

    if (screen != NULL) {
      pntr_draw_text(screen, font, line, x, y + lines * line_height, PNTR_WHITE);
    }
    if (widest != NULL) {
      int measured = pntr_measure_text(font, line);
      *widest = measured > *widest ? measured : *widest;
    }
    lines++;
    length = 0;
  }

  if (length > 0) {
    if (screen != NULL) {
      pntr_draw_text(screen, font, line, x, y + lines * line_height, PNTR_WHITE);
    }
    if (widest != NULL) {
      int measured = pntr_measure_text(font, line);
      *widest = measured > *widest ? measured : *widest;
    }
    lines++;
  }
  return lines;
}

void Overlay(tiny_game* game, pntr_image* screen) {
  DemoState* state = (DemoState*)game->user;
  if (state->font == NULL) {
    return;
  }

  char line[48];
  snprintf(line, sizeof(line), "%s  %d", game->map->name, state->score);
  pntr_draw_text(screen, state->font, line, 3, 3, PNTR_BLACK);
  pntr_draw_text(screen, state->font, line, 2, 2, PNTR_WHITE);

  if (state->message_timer > 0.0f && state->message != NULL) {
    int max_width = screen->width - 12;
    int line_height = pntr_measure_text_ex(state->font, "A", 1).y + 1;
    int widest = 0;
    int lines = WrapText(NULL, state->font, state->message, 0, 0, max_width, &widest);

    /* The box hugs the text rather than spanning the screen, so "town" is not a full bar. */
    int height = lines * line_height + 5;
    int width = widest + 6;
    int left = (screen->width - width) / 2;
    int top = screen->height - height - 3;

    pntr_draw_rectangle_fill(screen, left, top, width, height, PNTR_BLACK);
    WrapText(screen, state->font, state->message, left + 3, top + 3, max_width, NULL);
  }
}

/*--------------------------------------------------------------------------------------------
 * pntr_app glue
 *------------------------------------------------------------------------------------------*/

/*
 * This hook has to exist even though it does nothing.
 *
 * tinyengine polls input -- pntr_app_key_down() reads the app's keysDown[] array -- and that
 * array is only ever maintained by pntr_app_process_event(). On the web backend the key
 * handler opens with `if (app == NULL || app->event == NULL) return EM_FALSE;`, so leaving
 * `event` unset drops every keystroke before it can reach keysDown[], and the game runs but
 * never responds. The desktop backends read the keyboard directly and do not care.
 */
void Event(pntr_app* app, pntr_app_event* event) {
  (void)app;
  (void)event;
}

static const tiny_game_def DEMO = {
    .maps = TINY_MAPS,
    .map_count = TINY_MAP_COUNT,
    .start_map = "town",
    .object = Behaviour,
    .enter = Enter,
    .leave = NULL,
    .draw = Overlay};

typedef struct AppData {
  tiny_game* game;
  DemoState state;
} AppData;

bool Init(pntr_app* app) {
  AppData* data = (AppData*)pntr_load_memory(sizeof(AppData));
  if (data == NULL) {
    return false;
  }
  memset(data, 0, sizeof(AppData));
  pntr_app_set_userdata(app, data);

  data->state.font = pntr_load_font_default();

  data->game = tiny_init(app, &DEMO, &data->state);
  if (data->game == NULL) {
    return false;
  }
  return true;
}

bool Update(pntr_app* app, pntr_image* screen) {
  AppData* data = (AppData*)pntr_app_userdata(app);
  float dt = pntr_app_delta_time(app);
  if (dt > 0.05f) {
    dt = 0.05f; /* A long stall should not teleport anything through a wall. */
  }

  if (data->state.message_timer > 0.0f) {
    data->state.message_timer -= dt;
  }

  tiny_update(data->game, dt);
  tiny_draw(data->game, screen);
  return true;
}

void Close(pntr_app* app) {
  AppData* data = (AppData*)pntr_app_userdata(app);
  if (data == NULL) {
    return;
  }
  tiny_unload(data->game);
  pntr_unload_font(data->state.font);
  pntr_unload_memory(data);
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
      .event = Event,
      .fps = 60};
}
