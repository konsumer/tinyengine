/**
 * Map 'pyramid' -- baked from pyramid.tmj by tiny_bake.py. Do not edit.
 */
#ifndef TINY_BAKED_MAP_PYRAMID_H
#define TINY_BAKED_MAP_PYRAMID_H

#include "game_tiles.h"
#include "tinyengine.h"

static const tiny_prop tiny_pyramid_layer0_props[] = {
  { .name = "collision", .hash = 0x2176DB8FU, .type = 2, .as = { .i = 1 } },
};

static const tiny_prop tiny_pyramid_layer1_props[] = {
  { .name = "collision", .hash = 0x2176DB8FU, .type = 2, .as = { .i = 1 } },
};

static const uint16_t tiny_pyramid_layer0[144] = {
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
  29, 32, 32, 32, 33, 32, 32, 32, 32, 33, 32, 32,
  29, 32, 32, 33, 32, 32, 32, 32, 33, 32, 32, 32,
  29, 32, 33, 32, 32, 32, 32, 33, 32, 32, 32, 32,
  29, 33, 32, 32, 32, 32, 33, 32, 32, 32, 32, 33,
  29, 32, 32, 32, 32, 33, 32, 32, 32, 32, 33, 32,
  29, 32, 32, 32, 33, 32, 32, 32, 32, 33, 32, 32,
  29, 32, 32, 33, 32, 32, 32, 32, 33, 32, 32, 32,
  29, 32, 33, 32, 32, 32, 32, 33, 32, 32, 32, 32,
  29, 33, 32, 32, 32, 32, 33, 32, 32, 32, 32, 33,
  29, 32, 32, 32, 32, 33, 32, 32, 32, 32, 34, 32,
  29, 32, 32, 32, 33, 32, 32, 32, 32, 33, 32, 32,
};
static const uint16_t tiny_pyramid_layer1[144] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 35, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 36, 0, 0,
  0, 0, 0, 0, 35, 35, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 35, 0, 0, 0, 0, 35, 0, 0,
  0, 0, 0, 0, 0, 0, 36, 0, 0, 0, 0, 0,
  0, 0, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 35, 0, 0, 0,
  0, 0, 0, 36, 0, 35, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const tiny_layer tiny_pyramid_layers[3] = {
  { .name = "ground", .hash = 0x9CA1C77EU, .kind = TINY_LAYER_TILE, .visible = true, .sort = false,
    .collision = true, .width = 12, .height = 12, .offset_x = 0.0f, .offset_y = 0.0f,
    .parallax_x = 1.0f, .parallax_y = 1.0f, .tiles = tiny_pyramid_layer0,
    .props = { .items = tiny_pyramid_layer0_props, .count = 1 } },
  { .name = "blocks", .hash = 0x160DA1C3U, .kind = TINY_LAYER_TILE, .visible = true, .sort = false,
    .collision = true, .width = 12, .height = 12, .offset_x = 0.0f, .offset_y = 0.0f,
    .parallax_x = 1.0f, .parallax_y = 1.0f, .tiles = tiny_pyramid_layer1,
    .props = { .items = tiny_pyramid_layer1_props, .count = 1 } },
  { .name = "things", .hash = 0x9E7A4F34U, .kind = TINY_LAYER_OBJECT, .visible = true, .sort = true,
    .collision = false, .width = 0, .height = 0, .offset_x = 0.0f, .offset_y = 0.0f,
    .parallax_x = 1.0f, .parallax_y = 1.0f, .tiles = NULL,
    .props = { .items = NULL, .count = 0 } },
};

static const tiny_prop tiny_pyramid_obj2_props[] = {
  { .name = "destination", .hash = 0xB2DE64BFU, .type = 3, .as = { .s = "town" } },
  { .name = "spawn", .hash = 0x3A224D98U, .type = 3, .as = { .s = "from_pyramid" } },
};

static const tiny_prop tiny_pyramid_obj3_props[] = {
  { .name = "speed", .hash = 0x7B80C780U, .type = 1, .as = { .f = 1.6f } },
};

static const tiny_prop tiny_pyramid_obj4_props[] = {
  { .name = "speed", .hash = 0x7B80C780U, .type = 1, .as = { .f = 1.6f } },
};

static const tiny_prop tiny_pyramid_obj5_props[] = {
  { .name = "speed", .hash = 0x7B80C780U, .type = 1, .as = { .f = 1.6f } },
};

static const tiny_object_def tiny_pyramid_objects[6] = {
  { .name = "player", .type = "player", .name_hash = 0x2C99C300U, .type_hash = 0x2C99C300U, .id = 87,
    .x = 1.25f, .y = 1.25f, .w = 0.5f, .h = 0.5f,
    .tile = 37, .shape = TINY_SHAPE_TILE, .layer = 2, .visible = true,
    .props = { .items = NULL, .count = 0 } },
  { .name = "from_town", .type = "spawn", .name_hash = 0x4C890B96U, .type_hash = 0x3A224D98U, .id = 88,
    .x = 1.25f, .y = 1.25f, .w = 0.0f, .h = 0.0f,
    .tile = 0, .shape = TINY_SHAPE_POINT, .layer = 2, .visible = true,
    .props = { .items = NULL, .count = 0 } },
  { .name = "goal", .type = "portal", .name_hash = 0x166F6876U, .type_hash = 0x150C5D8BU, .id = 89,
    .x = 10.0f, .y = 10.0f, .w = 1.0f, .h = 1.0f,
    .tile = 0, .shape = TINY_SHAPE_RECT, .layer = 2, .visible = true,
    .props = { .items = tiny_pyramid_obj2_props, .count = 2 } },
  { .name = "hopper0", .type = "enemy", .name_hash = 0xE95DA43DU, .type_hash = 0x69EA6DABU, .id = 90,
    .x = 8.25f, .y = 1.25f, .w = 0.5f, .h = 0.5f,
    .tile = 38, .shape = TINY_SHAPE_TILE, .layer = 2, .visible = true,
    .props = { .items = tiny_pyramid_obj3_props, .count = 1 } },
  { .name = "hopper1", .type = "enemy", .name_hash = 0xE85DA2AAU, .type_hash = 0x69EA6DABU, .id = 91,
    .x = 1.25f, .y = 8.25f, .w = 0.5f, .h = 0.5f,
    .tile = 38, .shape = TINY_SHAPE_TILE, .layer = 2, .visible = true,
    .props = { .items = tiny_pyramid_obj4_props, .count = 1 } },
  { .name = "hopper2", .type = "enemy", .name_hash = 0xE75DA117U, .type_hash = 0x69EA6DABU, .id = 92,
    .x = 6.25f, .y = 10.25f, .w = 0.5f, .h = 0.5f,
    .tile = 38, .shape = TINY_SHAPE_TILE, .layer = 2, .visible = true,
    .props = { .items = tiny_pyramid_obj5_props, .count = 1 } },
};

static const tiny_prop tiny_pyramid_props[] = {
  { .name = "map_type", .hash = 0x30D0500AU, .type = 3, .as = { .s = "qbert" } },
  { .name = "name", .hash = 0x8D39BDE6U, .type = 3, .as = { .s = "pyramid" } },
};

static const tiny_map TINY_MAP_pyramid = {
  .name = "pyramid",
  .hash = 0xE8A937E7U,
  .type = TINY_MAP_ISOMETRIC,
  .orientation = TINY_ISOMETRIC,
  .width = 12, .height = 12,
  .tile_w = 16, .tile_h = 8,
  .background = { .value = 0xFF6E363A },
  .layers = tiny_pyramid_layers, .layer_count = 3,
  .objects = tiny_pyramid_objects, .object_count = 6,
  .tiles = TINY_TILES, .tile_count = TINY_TILE_COUNT,
  .anims = TINY_ANIMS, .anim_count = TINY_ANIM_COUNT,
  .props = { .items = tiny_pyramid_props, .count = 2 }
};

#endif  // TINY_BAKED_MAP_PYRAMID_H
