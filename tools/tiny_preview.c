/**
 * Render the demo to PNG files, with no window and no platform backend.
 *
 * The engine only ever asks pntr_app for its size, its input state, and a logger, so those
 * can be stubbed and the real demo driven straight from main(). That makes it possible to
 * check what every map type actually draws, and to do it in CI, without a display.
 *
 *   tiny_preview --map town --frames 60 --hold right,down --out town.png
 *
 * Build it against the same include path as the demo. It compiles demo.c directly, so what
 * it renders is the game, not a copy of it.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Order matters here, so it is fenced off from clang-format's include sorting. */
/* clang-format off */
#define PNTR_IMPLEMENTATION
#define PNTR_ENABLE_DEFAULT_FONT
#define PNTR_ENABLE_VARGS
#include "pntr.h"

/* Types only: no PNTR_APP_IMPLEMENTATION, so nothing pulls in a backend. */
#include "pntr_app.h"
/* clang-format on */

/*-- the small part of pntr_app the engine and the demo actually call ----------------------*/

void* pntr_app_userdata(pntr_app* app) {
  return app->userData;
}

void pntr_app_set_userdata(pntr_app* app, void* userData) {
  app->userData = userData;
}

int pntr_app_width(pntr_app* app) {
  return app->width;
}

int pntr_app_height(pntr_app* app) {
  return app->height;
}

float pntr_app_delta_time(pntr_app* app) {
  return app->deltaTime;
}

bool pntr_app_key_down(pntr_app* app, pntr_app_key key) {
  return app->keysDown[key];
}

bool pntr_app_gamepad_button_down(pntr_app* app, int gamepad, pntr_app_gamepad_button button) {
  (void)app;
  (void)gamepad;
  (void)button;
  return false;
}

void pntr_app_log(pntr_app_log_type type, const char* message) {
  static const char* names[] = {"debug", "info", "warning", "error"};
  fprintf(stderr, "tiny_preview: %s: %s\n", names[type], message);
}

void pntr_app_log_ex(pntr_app_log_type type, const char* message, ...) {
  char buffer[512];
  va_list args;
  va_start(args, message);
  vsnprintf(buffer, sizeof(buffer), message, args);
  va_end(args);
  pntr_app_log(type, buffer);
}

/*-- the game -----------------------------------------------------------------------------*/

#define DEMO_HEADLESS
#include "demo.c"

/*-----------------------------------------------------------------------------------------*/

static pntr_app_key key_named(const char* name) {
  if (strcmp(name, "left") == 0) {
    return PNTR_APP_KEY_LEFT;
  }
  if (strcmp(name, "right") == 0) {
    return PNTR_APP_KEY_RIGHT;
  }
  if (strcmp(name, "up") == 0) {
    return PNTR_APP_KEY_UP;
  }
  if (strcmp(name, "down") == 0) {
    return PNTR_APP_KEY_DOWN;
  }
  if (strcmp(name, "space") == 0) {
    return PNTR_APP_KEY_SPACE;
  }
  fprintf(stderr, "tiny_preview: unknown key '%s'\n", name);
  return PNTR_APP_KEY_INVALID;
}

int main(int argc, char* argv[]) {
  const char* map_name = NULL;
  const char* out_path = "preview.png";
  const char* hold = NULL;
  int frames = 30;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
      map_name = argv[++i];
    } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      out_path = argv[++i];
    } else if (strcmp(argv[i], "--hold") == 0 && i + 1 < argc) {
      hold = argv[++i];
    } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      frames = atoi(argv[++i]);
    } else {
      fprintf(stderr, "usage: %s [--map NAME] [--frames N] [--hold left,right,up,down,space] [--out FILE]\n", argv[0]);
      return 1;
    }
  }

  pntr_app app = Main(0, NULL);
  app.deltaTime = 1.0f / 60.0f;

  pntr_image* screen = pntr_gen_image_color(app.width, app.height, PNTR_BLACK);
  if (screen == NULL) {
    fprintf(stderr, "tiny_preview: could not allocate a %dx%d screen\n", app.width, app.height);
    return 1;
  }
  app.screen = screen;

  if (!app.init(&app)) {
    fprintf(stderr, "tiny_preview: the demo failed to start\n");
    return 1;
  }

  AppData* data = (AppData*)app.userData;
  if (map_name != NULL) {
    if (!tiny_goto_map(data->game, map_name, NULL)) {
      return 1;
    }
    /* The switch is deferred to the end of a frame, so spend one getting there. */
    tiny_update(data->game, 0.0f);
  }

  /* Hold the requested keys for the whole run, which is enough to exercise movement. */
  if (hold != NULL) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s", hold);
    for (char* token = strtok(buffer, ","); token != NULL; token = strtok(NULL, ",")) {
      pntr_app_key key = key_named(token);
      if (key == PNTR_APP_KEY_INVALID) {
        return 1;
      }
      app.keysDown[key] = true;
    }
  }

  for (int frame = 0; frame < frames; frame++) {
    app.update(&app, screen);
  }

  if (!pntr_save_image(screen, out_path)) {
    fprintf(stderr, "tiny_preview: could not write %s\n", out_path);
    return 1;
  }

  /*
   * Walking the objects through the public API is half the point of this tool: it is the
   * same loop a game writes, so it fails here if that ever stops working.
   */
  tiny_game* game = data->game;
  int alive = 0;
  int drawn = 0;
  for (int i = 0; i < tiny_object_count(game); i++) {
    tiny_object* object = tiny_object_at_index(game, i);
    if (object->active) {
      alive++;
      if (object->visible && object->tile != 0 && tiny_on_screen(game, object)) {
        drawn++;
      }
    }
  }

  /*
   * A frame that is entirely the background colour means nothing drew, which is the failure
   * this whole tool exists to catch. Counting distinct colours also catches a map that
   * rendered as one flat slab.
   */
  int colours = 0;
  for (int i = 0; i < screen->width * screen->height && colours < 8; i++) {
    bool seen = false;
    for (int j = 0; j < i; j++) {
      if (screen->data[j].value == screen->data[i].value) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      colours++;
    }
  }

  printf(
      "tiny_preview: %s -- map '%s', %d frames, %d alive, %d on screen, %d+ colours, "
      "player (%.2f, %.2f), camera (%.2f, %.2f)\n",
      out_path, game->map->name, frames, alive, drawn, colours,
      game->player != NULL ? (double)game->player->x : 0.0,
      game->player != NULL ? (double)game->player->y : 0.0,
      (double)game->camera.x, (double)game->camera.y);

  /*
   * The player must never come to rest inside a solid tile. This is the invariant that
   * catches collision going wrong -- walking through a wall leaves the player standing in
   * one, and no amount of looking at a screenshot reliably shows that.
   */
  if (game->player != NULL &&
      tiny_solid_rect(game, game->player->x, game->player->y, game->player->w, game->player->h)) {
    fprintf(stderr, "tiny_preview: the player ended up inside a solid tile at (%.2f, %.2f)\n",
            (double)game->player->x, (double)game->player->y);
    return 1;
  }

  if (game->player == NULL) {
    fprintf(stderr, "tiny_preview: this map has no player object\n");
    return 1;
  }
  if (drawn == 0) {
    fprintf(stderr, "tiny_preview: nothing was on screen to draw\n");
    return 1;
  }
  if (colours < 3) {
    fprintf(stderr, "tiny_preview: the frame is flat, so the map did not render\n");
    return 1;
  }

  app.close(&app);
  pntr_unload_image(screen);
  return 0;
}
