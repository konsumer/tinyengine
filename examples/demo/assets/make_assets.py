#!/usr/bin/env python3
"""Generate the demo's tilesets and maps.

The demo needs art and five maps, one per map_type, and hand-drawing them would make this
example impossible to tweak. So they are drawn here instead. The .tmj and .tsj files that
come out are ordinary Tiled files: open them in Tiled, edit them, and re-run tiny_bake.py.

  ./make_assets.py

Everything is 8x8 so the whole thing fits a 160x120 screen, which is the size an ESP32 with
no PSRAM can actually allocate.
"""

import json
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))

TILE = 8
ISO_CELL = 16
SCREEN_W, SCREEN_H = 160, 120
ROOM_W, ROOM_H = SCREEN_W // TILE, SCREEN_H // TILE  # 20x15 tiles

# A small, deliberately flat palette. Two shades of everything is enough to read at 8x8.
C = {
  'clear': (0, 0, 0, 0),
  'grass': (86, 148, 74, 255),
  'grass2': (108, 176, 92, 255),
  'dirt': (140, 106, 74, 255),
  'dirt2': (166, 130, 94, 255),
  'stone': (112, 112, 128, 255),
  'stone2': (148, 148, 164, 255),
  'brick': (156, 84, 68, 255),
  'brick2': (188, 112, 92, 255),
  'sky': (108, 168, 220, 255),
  'water': (58, 106, 190, 255),
  'water2': (94, 148, 226, 255),
  'sand': (214, 198, 138, 255),
  'sand2': (232, 220, 168, 255),
  'wood': (128, 92, 56, 255),
  'dark': (34, 32, 46, 255),
  'dark2': (58, 54, 78, 255),
  'gold': (240, 196, 68, 255),
  'gold2': (255, 232, 140, 255),
  'skin': (232, 176, 132, 255),
  'red': (200, 62, 62, 255),
  'red2': (236, 108, 96, 255),
  'blue': (72, 96, 200, 255),
  'magenta': (188, 88, 200, 255),
  'magenta2': (226, 140, 236, 255),
  'white': (240, 240, 240, 255),
  'lava': (222, 92, 36, 255),
  'lava2': (250, 152, 60, 255)
}


def sheet(columns, count, cell):
  rows = (count + columns - 1) // columns
  return Image.new('RGBA', (columns * cell, rows * cell), C['clear'])


def cell_box(index, columns, cell):
  return ((index % columns) * cell, (index // columns) * cell)


# ---------------------------------------------------------------------------------------------
# The 8x8 orthogonal tileset
# ---------------------------------------------------------------------------------------------

TILE_NAMES = [
  'grass', 'grass_flower', 'wall', 'floor', 'water_a', 'water_b', 'brick', 'sky',
  'platform', 'ladder', 'tree', 'portal_a', 'portal_b', 'sand', 'path', 'roof',
  'lava', 'dot', 'cave_floor', 'cave_wall'
]
TILE_INDEX = {name: i for i, name in enumerate(TILE_NAMES)}

# Which tiles stop you, and how.
TILE_PROPS = {
  'wall': {'solid': True},
  'brick': {'solid': True},
  'tree': {'solid': True},
  'roof': {'solid': True},
  'cave_wall': {'solid': True},
  'water_a': {'solid': True},
  'platform': {'platform': True},
  'ladder': {'ladder': True},
  'lava': {'hazard': True}
}


def draw_tiles():
  columns = 8
  image = sheet(columns, len(TILE_NAMES), TILE)
  pen = ImageDraw.Draw(image)

  def at(name):
    return cell_box(TILE_INDEX[name], columns, TILE)

  def fill(name, colour):
    x, y = at(name)
    pen.rectangle([x, y, x + TILE - 1, y + TILE - 1], fill=C[colour])
    return x, y

  def speck(x, y, points, colour):
    for px, py in points:
      pen.point((x + px, y + py), fill=C[colour])

  x, y = fill('grass', 'grass')
  speck(x, y, [(1, 2), (5, 1), (3, 5), (6, 6)], 'grass2')

  x, y = fill('grass_flower', 'grass')
  speck(x, y, [(2, 3), (3, 2), (3, 4), (4, 3)], 'white')
  speck(x, y, [(3, 3)], 'gold')
  speck(x, y, [(6, 6), (1, 6)], 'grass2')

  # A block with a lit top edge and a shaded bottom reads as solid at any size.
  x, y = fill('wall', 'stone')
  pen.line([x, y, x + TILE - 1, y], fill=C['stone2'])
  pen.line([x, y + TILE - 1, x + TILE - 1, y + TILE - 1], fill=C['dark2'])
  speck(x, y, [(2, 3), (5, 5)], 'stone2')

  x, y = fill('floor', 'dirt')
  speck(x, y, [(1, 1), (4, 2), (6, 5), (2, 6)], 'dirt2')

  for name, base, ripple in (('water_a', 'water', 'water2'), ('water_b', 'water', 'water2')):
    x, y = fill(name, base)
    offset = 0 if name.endswith('_a') else 3
    for row in (1, 4, 6):
      pen.line([x + (offset + row) % 5, y + row, x + (offset + row) % 5 + 2, y + row], fill=C[ripple])

  x, y = fill('brick', 'brick')
  pen.line([x, y + 3, x + TILE - 1, y + 3], fill=C['dark2'])
  pen.line([x, y + 7, x + TILE - 1, y + 7], fill=C['dark2'])
  pen.line([x + 3, y, x + 3, y + 3], fill=C['dark2'])
  pen.line([x + 6, y + 4, x + 6, y + 7], fill=C['dark2'])
  speck(x, y, [(0, 0), (1, 0), (4, 4)], 'brick2')

  fill('sky', 'sky')

  # A one-way platform: solid top, nothing underneath, so it reads as something to land on.
  x, y = fill('platform', 'clear')
  pen.rectangle([x, y, x + TILE - 1, y + 1], fill=C['wood'])
  pen.line([x, y, x + TILE - 1, y], fill=C['dirt2'])

  x, y = fill('ladder', 'clear')
  pen.line([x + 1, y, x + 1, y + TILE - 1], fill=C['wood'])
  pen.line([x + 6, y, x + 6, y + TILE - 1], fill=C['wood'])
  for rung in (1, 4, 7):
    pen.line([x + 1, y + rung, x + 6, y + rung], fill=C['dirt2'])

  x, y = fill('tree', 'grass')
  pen.rectangle([x + 3, y + 5, x + 4, y + 7], fill=C['wood'])
  pen.ellipse([x + 1, y, x + 6, y + 5], fill=C['grass2'])
  pen.ellipse([x + 2, y + 1, x + 5, y + 4], fill=C['grass'])

  # The portal is the only thing in the demo that changes maps, so it is the loudest tile.
  for name, inner in (('portal_a', 3), ('portal_b', 2)):
    x, y = fill(name, 'dark')
    pen.ellipse([x + 1, y + 1, x + 6, y + 6], fill=C['magenta'])
    pen.ellipse([x + inner, y + inner, x + 7 - inner, y + 7 - inner], fill=C['magenta2'])

  x, y = fill('sand', 'sand')
  speck(x, y, [(2, 2), (5, 3), (1, 6), (6, 6)], 'sand2')

  x, y = fill('path', 'sand2')
  speck(x, y, [(0, 3), (3, 1), (5, 5), (7, 2)], 'sand')

  x, y = fill('roof', 'brick')
  for row in range(0, TILE, 2):
    pen.line([x, y + row, x + TILE - 1, y + row], fill=C['brick2'])

  x, y = fill('lava', 'lava')
  for row in (2, 5):
    pen.line([x + row, y + row, x + row + 3, y + row], fill=C['lava2'])

  x, y = fill('dot', 'dark')
  pen.rectangle([x + 3, y + 3, x + 4, y + 4], fill=C['gold2'])

  x, y = fill('cave_floor', 'dark2')
  speck(x, y, [(2, 2), (5, 4), (3, 6)], 'stone')

  x, y = fill('cave_wall', 'stone')
  pen.rectangle([x, y, x + TILE - 1, y + 1], fill=C['stone2'])
  pen.rectangle([x, y + 5, x + TILE - 1, y + TILE - 1], fill=C['dark2'])

  return image


# ---------------------------------------------------------------------------------------------
# The 8x8 sprite tileset
# ---------------------------------------------------------------------------------------------

SPRITE_NAMES = [
  'player_down_a', 'player_down_b', 'player_up_a', 'player_up_b',
  'player_side_a', 'player_side_b', 'enemy_a', 'enemy_b',
  'coin_a', 'coin_b', 'coin_c', 'coin_d',
  'npc', 'goal', 'ghost_a', 'ghost_b'
]
SPRITE_INDEX = {name: i for i, name in enumerate(SPRITE_NAMES)}


def draw_sprites():
  columns = 4
  image = sheet(columns, len(SPRITE_NAMES), TILE)
  pen = ImageDraw.Draw(image)

  def at(name):
    return cell_box(SPRITE_INDEX[name], columns, TILE)

  def person(name, shirt, face_dir, step):
    x, y = at(name)
    pen.rectangle([x + 2, y + 1, x + 5, y + 3], fill=C['skin'])  # head
    pen.rectangle([x + 2, y + 4, x + 5, y + 6], fill=C[shirt])   # body
    if face_dir == 'down':
      pen.point((x + 3, y + 2), fill=C['dark'])
      pen.point((x + 4, y + 2), fill=C['dark'])
    elif face_dir == 'side':
      pen.point((x + 4, y + 2), fill=C['dark'])
    # The legs alternate, which is the whole of the walk cycle at this size.
    pen.point((x + 2, y + 7), fill=C['dark'] if step == 0 else C['clear'])
    pen.point((x + 5, y + 7), fill=C['dark'] if step == 1 else C['clear'])

  person('player_down_a', 'blue', 'down', 0)
  person('player_down_b', 'blue', 'down', 1)
  person('player_up_a', 'blue', 'up', 0)
  person('player_up_b', 'blue', 'up', 1)
  person('player_side_a', 'blue', 'side', 0)
  person('player_side_b', 'blue', 'side', 1)
  person('npc', 'gold', 'down', 0)

  for name, squash in (('enemy_a', 0), ('enemy_b', 1)):
    x, y = at(name)
    pen.ellipse([x + 1, y + 2 + squash, x + 6, y + 7], fill=C['red'])
    pen.point((x + 2, y + 4 + squash), fill=C['white'])
    pen.point((x + 5, y + 4 + squash), fill=C['white'])

  # A coin spinning: full circle, narrower, edge on, narrower again.
  for name, width in (('coin_a', 3), ('coin_b', 2), ('coin_c', 0), ('coin_d', 2)):
    x, y = at(name)
    pen.ellipse([x + 3 - width, y + 1, x + 4 + width, y + 6], fill=C['gold'])
    if width > 0:
      pen.ellipse([x + 4 - width, y + 2, x + 3 + width, y + 5], fill=C['gold2'])

  x, y = at('goal')
  pen.rectangle([x + 1, y + 1, x + 6, y + 6], fill=C['white'])
  for row in range(1, 7):
    for col in range(1, 7):
      if (row + col) % 2 == 0:
        pen.point((x + col, y + row), fill=C['dark'])

  for name, wobble in (('ghost_a', 0), ('ghost_b', 1)):
    x, y = at(name)
    pen.ellipse([x + 1, y + 1, x + 6, y + 5], fill=C['magenta'])
    pen.rectangle([x + 1, y + 3, x + 6, y + 6], fill=C['magenta'])
    for foot in (1, 3, 5):
      pen.point((x + foot + wobble, y + 7), fill=C['magenta'])
    pen.point((x + 2, y + 3), fill=C['white'])
    pen.point((x + 5, y + 3), fill=C['white'])

  return image


# ---------------------------------------------------------------------------------------------
# The isometric tileset: 16x16 cells holding a 16x8 diamond, bottom aligned
# ---------------------------------------------------------------------------------------------

ISO_NAMES = ['iso_grass', 'iso_sand', 'iso_block', 'iso_wall', 'iso_water_a', 'iso_water_b',
             'iso_goal', 'iso_player', 'iso_hopper_a', 'iso_hopper_b']
ISO_INDEX = {name: i for i, name in enumerate(ISO_NAMES)}


def draw_iso():
  columns = 4
  image = sheet(columns, len(ISO_NAMES), ISO_CELL)
  pen = ImageDraw.Draw(image)

  def diamond(x, bottom, colour):
    """A 16x8 diamond whose bounding box bottom sits at `bottom`."""
    top = bottom - 8
    pen.polygon([(x + 8, top), (x + 16, top + 4), (x + 8, bottom), (x, top + 4)], fill=C[colour])

  def block(name, top_colour, left_colour, right_colour, height):
    x, y = cell_box(ISO_INDEX[name], columns, ISO_CELL)
    bottom = y + ISO_CELL - 1
    face_top = bottom - height
    # The two visible side faces, then the lit top, so the top always wins the overlap.
    pen.polygon([(x, face_top - 4), (x + 8, face_top), (x + 8, bottom), (x, bottom - 4)], fill=C[left_colour])
    pen.polygon([(x + 16, face_top - 4), (x + 8, face_top), (x + 8, bottom), (x + 16, bottom - 4)], fill=C[right_colour])
    diamond(x, face_top, top_colour)

  block('iso_grass', 'grass2', 'dirt', 'dirt2', 2)
  block('iso_sand', 'sand2', 'sand', 'dirt2', 2)
  block('iso_block', 'stone2', 'stone', 'dark2', 5)
  block('iso_wall', 'brick2', 'brick', 'dark2', 9)
  block('iso_water_a', 'water2', 'water', 'water', 1)
  block('iso_water_b', 'water', 'water2', 'water', 2)
  block('iso_goal', 'gold2', 'gold', 'dirt', 2)

  # Figures stand on the middle of the diamond, so their feet land at the cell's bottom edge.
  def figure(name, body, height):
    x, y = cell_box(ISO_INDEX[name], columns, ISO_CELL)
    feet = y + ISO_CELL - 3
    pen.rectangle([x + 6, feet - height, x + 9, feet], fill=C[body])
    pen.rectangle([x + 6, feet - height - 3, x + 9, feet - height - 1], fill=C['skin'])
    pen.point((x + 7, feet - height - 2), fill=C['dark'])
    pen.point((x + 8, feet - height - 2), fill=C['dark'])

  figure('iso_player', 'blue', 4)
  figure('iso_hopper_a', 'red', 4)
  figure('iso_hopper_b', 'red', 2)

  return image


# ---------------------------------------------------------------------------------------------
# Tiled files
# ---------------------------------------------------------------------------------------------


def prop(name, value):
  kind = {bool: 'bool', int: 'int', float: 'float', str: 'string'}[type(value)]
  return {'name': name, 'type': kind, 'value': value}


def props(mapping):
  return [prop(name, value) for name, value in mapping.items()]


def tileset_file(name, image_name, tile_w, tile_h, columns, count, tiles):
  return {
    'columns': columns,
    'image': image_name,
    'imageheight': ((count + columns - 1) // columns) * tile_h,
    'imagewidth': columns * tile_w,
    'margin': 0,
    'name': name,
    'spacing': 0,
    'tilecount': count,
    'tiledversion': '1.11.0',
    'tileheight': tile_h,
    'tilewidth': tile_w,
    'type': 'tileset',
    'version': '1.10',
    'tiles': tiles
  }


def tile_layer(name, width, height, data, extra=None):
  layer = {
    'data': data,
    'height': height,
    'id': 0,
    'name': name,
    'opacity': 1,
    'type': 'tilelayer',
    'visible': True,
    'width': width,
    'x': 0,
    'y': 0
  }
  if extra:
    layer['properties'] = props(extra)
  return layer


def object_layer(name, objects, extra=None):
  layer = {
    'draworder': 'topdown',
    'id': 0,
    'name': name,
    'objects': objects,
    'opacity': 1,
    'type': 'objectgroup',
    'visible': True,
    'x': 0,
    'y': 0
  }
  if extra:
    layer['properties'] = props(extra)
  return layer


_next_id = [1]


def obj(name, kind, x, y, w=TILE, h=TILE, gid=None, extra=None, point=False):
  _next_id[0] += 1
  out = {
    'id': _next_id[0],
    'name': name,
    'type': kind,
    'rotation': 0,
    'visible': True,
    'x': x,
    'y': y,
    'width': 0 if point else w,
    'height': 0 if point else h
  }
  if point:
    out['point'] = True
  if gid is not None:
    out['gid'] = gid
    # Tiled anchors tile objects at the bottom-left, so the caller's y is the feet.
    out['y'] = y + h
  if extra:
    out['properties'] = props(extra)
  return out


def tiled_map(orientation, width, height, tile_w, tile_h, tilesets, layers, map_props, background):
  return {
    'backgroundcolor': background,
    'compressionlevel': -1,
    'height': height,
    'infinite': False,
    'layers': layers,
    'nextlayerid': len(layers) + 1,
    'nextobjectid': _next_id[0] + 1,
    'orientation': orientation,
    'properties': props(map_props),
    'renderorder': 'right-down',
    'tiledversion': '1.11.0',
    'tileheight': tile_h,
    'tilesets': tilesets,
    'tilewidth': tile_w,
    'type': 'map',
    'version': '1.10',
    'width': width
  }


def write_json(path, data):
  with open(path, 'w') as handle:
    json.dump(data, handle, indent=1)
    handle.write('\n')
  print('wrote %s' % os.path.relpath(path, HERE))


# Global tile ids. The ortho maps use tiles.tsj at 1 and sprites.tsj after it.
TILES_FIRST = 1
SPRITES_FIRST = TILES_FIRST + len(TILE_NAMES)
ISO_FIRST = 1
ISO_SPRITES_FIRST = ISO_FIRST + len(ISO_NAMES)


def T(name):
  return TILES_FIRST + TILE_INDEX[name]


def S(name):
  return SPRITES_FIRST + SPRITE_INDEX[name]


def I(name):
  return ISO_FIRST + ISO_INDEX[name]


ORTHO_TILESETS = [
  {'firstgid': TILES_FIRST, 'source': 'tiles.tsj'},
  {'firstgid': SPRITES_FIRST, 'source': 'sprites.tsj'}
]
ISO_TILESETS = [{'firstgid': ISO_FIRST, 'source': 'iso.tsj'}]


def grid(width, height, fill):
  return [fill] * (width * height)


def put(data, width, x, y, value):
  if 0 <= x < width and 0 <= y < len(data) // width:
    data[y * width + x] = value


def box(data, width, x0, y0, x1, y1, value):
  for y in range(y0, y1 + 1):
    for x in range(x0, x1 + 1):
      put(data, width, x, y, value)


def border(data, width, height, value):
  box(data, width, 0, 0, width - 1, 0, value)
  box(data, width, 0, height - 1, width - 1, height - 1, value)
  box(data, width, 0, 0, 0, height - 1, value)
  box(data, width, width - 1, 0, width - 1, height - 1, value)


# ---------------------------------------------------------------------------------------------


def make_town():
  """RPG: two screens square, an overhead layer, and portals to everywhere else."""
  w, h = ROOM_W * 2, ROOM_H * 2
  ground = grid(w, h, T('grass'))
  for x in range(0, w, 7):
    put(ground, w, x, (x * 3) % h, T('grass_flower'))
  box(ground, w, 0, 12, w - 1, 14, T('path'))
  box(ground, w, 18, 0, 20, h - 1, T('path'))
  box(ground, w, 2, 2, 9, 6, T('water_a'))

  decor = grid(w, h, 0)
  # A house, with the roof on the layer that draws over the player.
  box(decor, w, 24, 4, 30, 9, T('brick'))
  box(decor, w, 26, 8, 27, 9, 0)
  for x in (5, 12, 33, 36):
    put(decor, w, x, 20, T('tree'))
    put(decor, w, x + 1, 24, T('tree'))
  border(decor, w, h, T('tree'))

  overhead = grid(w, h, 0)
  box(overhead, w, 23, 2, 31, 4, T('roof'))

  # Every door is painted with the animated portal tile so you can see where the other games
  # are, and the matching return spawn sits ON the door. Arriving inside the doorway is what
  # stops you bouncing straight back out: a portal starts disarmed and only re-arms once the
  # player has stepped clear of it.
  # A door is one tile, and the spawn you come back to sits one tile beyond it on the side
  # you re-enter from. Going through a door therefore never leaves you standing on one: you
  # come out already past it, still heading the way you were, and just keep walking.
  #
  #   door tile        spawn tile      you are heading
  portals = [
    ('cave_door', 'from_cave', 2, 13, 3, 13, 'cave'),          # west, back in heading east
    ('ledge_door', 'from_ledge', 37, 13, 36, 13, 'ledge'),     # east, back in heading west
    ('arena_door', 'from_arena', 19, 2, 19, 3, 'arena'),       # north, back in heading south
    ('pyramid_door', 'from_pyramid', 19, h - 3, 19, h - 4, 'pyramid')
  ]

  objects = [
    obj('player', 'player', 19 * TILE, 13 * TILE, gid=S('player_down_a')),
    obj('elder', 'npc', 26 * TILE, 11 * TILE, gid=S('npc'),
        extra={'says': 'The cave lies west, past the water. Mind your step.'})
  ]
  for door, spawn, door_x, door_y, spawn_x, spawn_y, destination in portals:
    put(decor, w, door_x, door_y, T('portal_a'))
    objects.append(obj(door, 'portal', door_x * TILE, door_y * TILE,
                       extra={'destination': destination, 'spawn': 'from_town'}))
    objects.append(obj(spawn, 'spawn', spawn_x * TILE, spawn_y * TILE, point=True))

  for index, x in enumerate((14, 16, 22, 25)):
    objects.append(obj('coin%d' % index, 'coin', x * TILE, 17 * TILE, gid=S('coin_a')))

  layers = [
    tile_layer('ground', w, h, ground, {'collision': True}),
    tile_layer('decor', w, h, decor, {'collision': True}),
    object_layer('things', objects),
    tile_layer('overhead', w, h, overhead, {'collision': False})
  ]
  return tiled_map('orthogonal', w, h, TILE, TILE, ORTHO_TILESETS, layers,
                   {'map_type': 'pokemon', 'name': 'town'}, '#6ca85c')


def make_cave():
  """Top-down: exactly four rooms, so the camera has somewhere to snap to."""
  w, h = ROOM_W * 2, ROOM_H * 2
  ground = grid(w, h, T('cave_floor'))
  walls = grid(w, h, 0)
  border(walls, w, h, T('cave_wall'))

  # A wall between the rooms, with a doorway through each so you can actually leave.
  box(walls, w, ROOM_W - 1, 0, ROOM_W, h - 1, T('cave_wall'))
  box(walls, w, 0, ROOM_H - 1, w - 1, ROOM_H, T('cave_wall'))
  box(walls, w, ROOM_W - 1, 6, ROOM_W, 8, 0)
  box(walls, w, ROOM_W - 1, 21, ROOM_W, 23, 0)
  box(walls, w, 8, ROOM_H - 1, 10, ROOM_H, 0)
  box(walls, w, 28, ROOM_H - 1, 30, ROOM_H, 0)

  for x, y in ((5, 5), (14, 9), (25, 4), (33, 10), (7, 20), (15, 25), (27, 19), (34, 24)):
    put(walls, w, x, y, T('cave_wall'))
  box(ground, w, 30, 22, 34, 26, T('lava'))

  # The door is on the east wall. You leave town heading west, so you arrive here still
  # heading west, one tile past the door, and can carry on without turning round.
  put(ground, w, w - 2, 13, T('portal_a'))
  objects = [
    obj('player', 'player', 3 * TILE, 3 * TILE, gid=S('player_down_a')),
    obj('from_town', 'spawn', (w - 3) * TILE, 13 * TILE, point=True),
    obj('exit', 'portal', (w - 2) * TILE, 13 * TILE,
        extra={'destination': 'town', 'spawn': 'from_cave'})
  ]
  for index, (x, y) in enumerate(((12, 5), (30, 8), (6, 22), (33, 18))):
    objects.append(obj('bat%d' % index, 'enemy', x * TILE, y * TILE, gid=S('enemy_a'),
                       extra={'speed': 22.0}))
  for index, (x, y) in enumerate(((17, 3), (24, 11), (11, 26))):
    objects.append(obj('gem%d' % index, 'coin', x * TILE, y * TILE, gid=S('coin_a')))

  layers = [
    tile_layer('ground', w, h, ground, {'collision': True}),
    tile_layer('walls', w, h, walls, {'collision': True}),
    object_layer('things', objects)
  ]
  return tiled_map('orthogonal', w, h, TILE, TILE, ORTHO_TILESETS, layers,
                   {'map_type': 'zelda', 'name': 'cave'}, '#22202e')


def make_ledge():
  """Platformer: one screen tall, five wide, with gaps that need the jump."""
  w, h = ROOM_W * 5, ROOM_H
  ground = grid(w, h, T('sky'))
  solid = grid(w, h, 0)

  box(solid, w, 0, h - 2, w - 1, h - 1, T('brick'))
  for gap_x in (22, 44, 66, 84):
    box(solid, w, gap_x, h - 2, gap_x + 3, h - 1, 0)
    box(ground, w, gap_x, h - 2, gap_x + 3, h - 1, T('lava'))

  for x, y, length in ((12, 9, 4), (26, 8, 5), (37, 10, 3), (48, 7, 4), (58, 9, 5),
                       (70, 8, 4), (78, 6, 3), (88, 9, 4)):
    box(solid, w, x, y, x + length - 1, y, T('platform'))
  for x, y, length in ((18, 6, 3), (54, 5, 4), (74, 4, 3)):
    box(solid, w, x, y, x + length - 1, y, T('brick'))

  box(solid, w, 33, h - 6, 34, h - 3, T('ladder'))
  box(solid, w, 33, h - 7, 36, h - 7, T('brick'))
  border(solid, w, h, T('brick'))
  box(solid, w, 1, 1, w - 2, 1, 0)

  # Home is the west door, the run continues out of the east one. Both spawns sit one tile
  # inside their door, so arriving never lands you on top of it.
  put(solid, w, 2, h - 3, T('portal_a'))
  put(solid, w, w - 3, h - 3, T('portal_a'))
  objects = [
    obj('player', 'player', 3 * TILE, (h - 3) * TILE, gid=S('player_side_a')),
    obj('from_town', 'spawn', 3 * TILE, (h - 3) * TILE, point=True),
    obj('from_sky', 'spawn', (w - 4) * TILE, (h - 3) * TILE, point=True),
    obj('back', 'portal', 2 * TILE, (h - 3) * TILE,
        extra={'destination': 'town', 'spawn': 'from_ledge'}),
    obj('onward', 'portal', (w - 3) * TILE, (h - 3) * TILE,
        extra={'destination': 'sky', 'spawn': 'from_ledge'})
  ]
  for index, (x, y) in enumerate(((14, 8), (28, 7), (50, 6), (60, 8), (90, 8))):
    objects.append(obj('goomba%d' % index, 'enemy', x * TILE, y * TILE, gid=S('enemy_a'),
                       extra={'speed': 26.0, 'patrol': 3.0}))
  for index, (x, y) in enumerate(((13, 7), (29, 6), (38, 8), (49, 5), (59, 7), (71, 6), (79, 4), (89, 7))):
    objects.append(obj('coin%d' % index, 'coin', x * TILE, y * TILE, gid=S('coin_a')))

  layers = [
    tile_layer('sky', w, h, ground, {'collision': False}),
    tile_layer('solid', w, h, solid, {'collision': True}),
    object_layer('things', objects, {'sort': False})
  ]
  return tiled_map('orthogonal', w, h, TILE, TILE, ORTHO_TILESETS, layers,
                   {'map_type': 'smb', 'name': 'ledge', 'gravity': 420.0}, '#6ca8dc')


def make_arena():
  """Arcade: exactly one screen, no camera at all."""
  w, h = ROOM_W, ROOM_H
  ground = grid(w, h, T('floor'))
  walls = grid(w, h, 0)
  border(walls, w, h, T('wall'))
  for x in range(3, w - 3, 4):
    for y in range(3, h - 3, 4):
      box(walls, w, x, y, x + 1, y + 1, T('wall'))

  # South wall, because you came in heading south from town's north door.
  put(walls, w, 10, h - 2, 0)
  put(ground, w, 10, h - 2, T('portal_a'))
  objects = [
    obj('player', 'player', 1 * TILE, 1 * TILE, gid=S('player_down_a')),
    obj('from_town', 'spawn', 10 * TILE, (h - 3) * TILE, point=True),
    obj('exit', 'portal', 10 * TILE, (h - 2) * TILE,
        extra={'destination': 'town', 'spawn': 'from_arena'})
  ]
  for index, (x, y) in enumerate(((17, 1), (1, 13), (9, 7), (17, 13))):
    objects.append(obj('ghost%d' % index, 'ghost', x * TILE, y * TILE, gid=S('ghost_a'),
                       extra={'speed': 30.0}))
  for x in range(1, w - 1, 2):
    for y in range(1, h - 1, 4):
      if walls[y * w + x] == 0:
        objects.append(obj(None, 'pellet', x * TILE, y * TILE, gid=S('coin_a')))

  layers = [
    tile_layer('ground', w, h, ground, {'collision': True}),
    tile_layer('walls', w, h, walls, {'collision': True}),
    object_layer('things', objects)
  ]
  return tiled_map('orthogonal', w, h, TILE, TILE, ORTHO_TILESETS, layers,
                   {'map_type': 'pacman', 'name': 'arena'}, '#22202e')


def make_sky():
  """Side shooter: the same mode as the platformer, with gravity set to 0.

  That one property is the whole difference. Nothing falls, so there is nothing for a jump
  to push against and the up/down keys simply fly. It is the mode 1943 or Gradius run in,
  and it needs no code in the engine that the platformer did not already have.
  """
  w, h = ROOM_W * 3, ROOM_H
  ground = grid(w, h, T('sky'))
  solid = grid(w, h, 0)
  border(solid, w, h, T('brick'))
  for x, y, tall in ((10, 3, 4), (18, 8, 5), (26, 2, 3), (34, 9, 4), (44, 4, 6), (52, 7, 3)):
    box(solid, w, x, y, x + 1, y + tall - 1, T('brick'))

  put(solid, w, 2, 7, T('portal_a'))
  objects = [
    obj('player', 'player', 3 * TILE, 7 * TILE, gid=S('player_side_a')),
    obj('from_ledge', 'spawn', 3 * TILE, 7 * TILE, point=True),
    obj('back', 'portal', 2 * TILE, 7 * TILE,
        extra={'destination': 'ledge', 'spawn': 'from_sky'})
  ]
  for index, (x, y) in enumerate(((14, 5), (22, 10), (30, 6), (40, 3), (48, 11), (56, 8))):
    objects.append(obj('drone%d' % index, 'enemy', x * TILE, y * TILE, gid=S('enemy_a'),
                       extra={'speed': 30.0}))
  for index, (x, y) in enumerate(((12, 8), (24, 4), (38, 9), (50, 5))):
    objects.append(obj('pod%d' % index, 'coin', x * TILE, y * TILE, gid=S('coin_a')))

  layers = [
    tile_layer('sky', w, h, ground, {'collision': False}),
    tile_layer('solid', w, h, solid, {'collision': True}),
    object_layer('things', objects, {'sort': False})
  ]
  return tiled_map('orthogonal', w, h, TILE, TILE, ORTHO_TILESETS, layers,
                   {'map_type': 'shooter', 'name': 'sky', 'gravity': 0.0}, '#3a366e')


def make_pyramid():
  """Isometric: a small plateau with blocks to walk around and a goal in the corner."""
  w, h = 12, 12
  ground = grid(w, h, I('iso_grass'))
  for y in range(w):
    for x in range(h):
      if (x + y) % 5 == 0:
        put(ground, w, x, y, I('iso_sand'))
  box(ground, w, 0, 0, w - 1, 0, I('iso_water_a'))
  box(ground, w, 0, 0, 0, h - 1, I('iso_water_a'))
  put(ground, w, w - 2, h - 2, I('iso_goal'))

  blocks = grid(w, h, 0)
  for x, y in ((4, 4), (5, 4), (4, 5), (7, 2), (2, 7), (8, 8), (9, 5), (5, 9)):
    put(blocks, w, x, y, I('iso_block'))
  for x, y in ((6, 6), (3, 9), (9, 3)):
    put(blocks, w, x, y, I('iso_wall'))

  # Isometric object positions are in tile-height units on both axes, which for a 16x8 grid
  # means one tile step is `tile_h` across. Tiled does this too; it is not a tinyengine thing.
  step = TILE

  # Figures get a half-tile box, inset to sit in the middle of their cell. A full-tile box
  # only clears a gap when it happens to be exactly grid-aligned -- the moment it straddles a
  # boundary it tests all four neighbouring cells at once, which makes scattered blocks into
  # a wall you cannot walk between.
  body = step // 2
  inset = step // 4

  def figure(name, kind, tx, ty, tile, extra=None):
    return obj(name, kind, tx * step + inset, ty * step + inset,
               w=body, h=body, gid=I(tile), extra=extra)

  objects = [
    figure('player', 'player', 1, 1, 'iso_player'),
    obj('from_town', 'spawn', 1 * step + inset, 1 * step + inset, point=True),
    obj('goal', 'portal', (w - 2) * step, (h - 2) * step, w=step, h=step,
        extra={'destination': 'town', 'spawn': 'from_pyramid'})
  ]
  for index, (x, y) in enumerate(((8, 1), (1, 8), (6, 10))):
    objects.append(figure('hopper%d' % index, 'enemy', x, y, 'iso_hopper_a', {'speed': 1.6}))

  layers = [
    tile_layer('ground', w, h, ground, {'collision': True}),
    tile_layer('blocks', w, h, blocks, {'collision': True}),
    object_layer('things', objects)
  ]
  return tiled_map('isometric', w, h, ISO_CELL, TILE, ISO_TILESETS, layers,
                   {'map_type': 'qbert', 'name': 'pyramid'}, '#3a366e')


# ---------------------------------------------------------------------------------------------


def main():
  draw_tiles().save(os.path.join(HERE, 'tiles.png'))
  draw_sprites().save(os.path.join(HERE, 'sprites.png'))
  draw_iso().save(os.path.join(HERE, 'iso.png'))
  print('wrote tiles.png, sprites.png, iso.png')

  # Tile behaviour and the water/portal animations live on the tileset, where Tiled puts them.
  tiles = {}
  for name, flags in TILE_PROPS.items():
    tiles[TILE_INDEX[name]] = {'id': TILE_INDEX[name], 'properties': props(flags)}

  def animate(name, other, duration):
    tile = tiles.setdefault(TILE_INDEX[name], {'id': TILE_INDEX[name]})
    tile['animation'] = [{'duration': duration, 'tileid': TILE_INDEX[name]},
                         {'duration': duration, 'tileid': TILE_INDEX[other]}]

  animate('water_a', 'water_b', 600)
  animate('portal_a', 'portal_b', 220)
  tiles = [tiles[key] for key in sorted(tiles)]

  write_json(os.path.join(HERE, 'tiles.tsj'),
             tileset_file('tiles', 'tiles.png', TILE, TILE, 8, len(TILE_NAMES), tiles))

  # Named animations: a string `anim` property on the first frame is what tiny_play() finds.
  sprite_tiles = [
    {'id': SPRITE_INDEX['player_down_a'], 'properties': props({'anim': 'player_down'}),
     'animation': [{'duration': 220, 'tileid': SPRITE_INDEX['player_down_a']},
                   {'duration': 220, 'tileid': SPRITE_INDEX['player_down_b']}]},
    {'id': SPRITE_INDEX['player_up_a'], 'properties': props({'anim': 'player_up'}),
     'animation': [{'duration': 220, 'tileid': SPRITE_INDEX['player_up_a']},
                   {'duration': 220, 'tileid': SPRITE_INDEX['player_up_b']}]},
    {'id': SPRITE_INDEX['player_side_a'], 'properties': props({'anim': 'player_side'}),
     'animation': [{'duration': 180, 'tileid': SPRITE_INDEX['player_side_a']},
                   {'duration': 180, 'tileid': SPRITE_INDEX['player_side_b']}]},
    {'id': SPRITE_INDEX['enemy_a'], 'properties': props({'anim': 'enemy'}),
     'animation': [{'duration': 260, 'tileid': SPRITE_INDEX['enemy_a']},
                   {'duration': 260, 'tileid': SPRITE_INDEX['enemy_b']}]},
    {'id': SPRITE_INDEX['ghost_a'], 'properties': props({'anim': 'ghost'}),
     'animation': [{'duration': 300, 'tileid': SPRITE_INDEX['ghost_a']},
                   {'duration': 300, 'tileid': SPRITE_INDEX['ghost_b']}]},
    {'id': SPRITE_INDEX['coin_a'], 'properties': props({'anim': 'coin'}),
     'animation': [{'duration': 110, 'tileid': SPRITE_INDEX['coin_a']},
                   {'duration': 110, 'tileid': SPRITE_INDEX['coin_b']},
                   {'duration': 110, 'tileid': SPRITE_INDEX['coin_c']},
                   {'duration': 110, 'tileid': SPRITE_INDEX['coin_d']}]}
  ]
  write_json(os.path.join(HERE, 'sprites.tsj'),
             tileset_file('sprites', 'sprites.png', TILE, TILE, 4, len(SPRITE_NAMES), sprite_tiles))

  iso_tiles = [
    {'id': ISO_INDEX['iso_block'], 'properties': props({'solid': True})},
    {'id': ISO_INDEX['iso_wall'], 'properties': props({'solid': True})},
    {'id': ISO_INDEX['iso_water_a'], 'properties': props({'solid': True}),
     'animation': [{'duration': 700, 'tileid': ISO_INDEX['iso_water_a']},
                   {'duration': 700, 'tileid': ISO_INDEX['iso_water_b']}]},
    {'id': ISO_INDEX['iso_hopper_a'], 'properties': props({'anim': 'hopper'}),
     'animation': [{'duration': 320, 'tileid': ISO_INDEX['iso_hopper_a']},
                   {'duration': 320, 'tileid': ISO_INDEX['iso_hopper_b']}]}
  ]
  write_json(os.path.join(HERE, 'iso.tsj'),
             tileset_file('iso', 'iso.png', ISO_CELL, ISO_CELL, 4, len(ISO_NAMES), iso_tiles))

  for name, builder in (('town', make_town), ('cave', make_cave), ('ledge', make_ledge),
                        ('arena', make_arena), ('pyramid', make_pyramid), ('sky', make_sky)):
    write_json(os.path.join(HERE, '%s.tmj' % name), builder())


if __name__ == '__main__':
  main()
