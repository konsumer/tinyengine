/**
 * Every map in the project -- baked by tiny_bake.py. Do not edit.
 *
 * Include this from exactly one translation unit: it defines the data, not just
 * declarations. Hand TINY_MAPS and TINY_MAP_COUNT to your tiny_game_def.
 */
#ifndef TINY_BAKED_MAPS_H
#define TINY_BAKED_MAPS_H

#include "game_tiles.h"
#include "map_arena.h"
#include "map_cave.h"
#include "map_ledge.h"
#include "map_pyramid.h"
#include "map_sky.h"
#include "map_town.h"

#define TINY_MAP_COUNT 6

static const tiny_map* const TINY_MAPS[TINY_MAP_COUNT] = {
  &TINY_MAP_arena,
  &TINY_MAP_cave,
  &TINY_MAP_ledge,
  &TINY_MAP_pyramid,
  &TINY_MAP_sky,
  &TINY_MAP_town,
};

#endif  // TINY_BAKED_MAPS_H
