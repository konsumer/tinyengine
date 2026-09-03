#!/usr/bin/env python3
"""Bake Tiled maps into C headers for tinyengine.

Takes every map in a project at once, because that is the only way to know which tiles are
actually reachable. Tiles are sliced out of their tilesets, flipped the way each map asked
for, hashed, and deduplicated into one table shared by every map. A tile no map references
is never emitted, and a tile that happens to look like another one is emitted once.

Everything written is `static const`, so on an ESP32 it lands in .rodata and pntr draws
straight out of flash. Nothing is decoded at runtime and nothing but the object array
touches the heap.

  tiny_bake.py maps/*.tmj -o generated/
  tiny_bake.py maps/*.tmj -o generated/ --format argb --flash-budget 1310720

Emits, into the output directory:

  game_tiles.h   the shared tile table and every animation
  map_<name>.h   one per map: layers, objects, properties
  game_maps.h    includes all of the above and defines TINY_MAPS[]

Include game_maps.h from exactly one translation unit.
"""

import argparse
import base64
import json
import os
import re
import sys
import zlib
from collections import OrderedDict

try:
  from PIL import Image
except ImportError:
  sys.exit('tiny_bake: needs Pillow (pip install Pillow)')

# The app partition an ESP32 Arduino build gets by default.
DEFAULT_FLASH_BUDGET = 1310720

# Tiled packs the three flip bits into the top of every global tile id.
FLIP_H = 0x80000000
FLIP_V = 0x40000000
FLIP_D = 0x20000000
GID_MASK = 0x1FFFFFFF

# What a map's `map_type` property may say.
#
# Three modes, because most of what looks like another genre is one of these with a different
# map size or a different property. A one-screen `top` map is an arcade screen; a set of them
# joined by portals is room-to-room exploration; a `plat` map with gravity 0 is a side-shooter.
MAP_TYPES = {
  # Side-on.
  'plat': 'TINY_MAP_PLATFORMER',
  'platformer': 'TINY_MAP_PLATFORMER',
  'smb': 'TINY_MAP_PLATFORMER',
  'sidescroller': 'TINY_MAP_PLATFORMER',
  'shooter': 'TINY_MAP_PLATFORMER',
  # Top-down.
  'top': 'TINY_MAP_TOPDOWN',
  'topdown': 'TINY_MAP_TOPDOWN',
  'rpg': 'TINY_MAP_TOPDOWN',
  'zelda': 'TINY_MAP_TOPDOWN',
  'pokemon': 'TINY_MAP_TOPDOWN',
  'final-fantasy': 'TINY_MAP_TOPDOWN',
  'arcade': 'TINY_MAP_TOPDOWN',
  'tetris': 'TINY_MAP_TOPDOWN',
  'pacman': 'TINY_MAP_TOPDOWN',
  'bubble-bobble': 'TINY_MAP_TOPDOWN',
  '1943': 'TINY_MAP_TOPDOWN',
  # Isometric.
  'iso': 'TINY_MAP_ISOMETRIC',
  'isometric': 'TINY_MAP_ISOMETRIC',
  'qbert': 'TINY_MAP_ISOMETRIC',
  'rc-pro-am': 'TINY_MAP_ISOMETRIC'
}

# Boolean tile properties that become tiny_tile_flag bits.
TILE_FLAGS = [
  ('solid', 1 << 0),
  ('platform', 1 << 1),
  ('ladder', 1 << 2),
  ('hazard', 1 << 3),
  ('user1', 1 << 4),
  ('user2', 1 << 5),
  ('user3', 1 << 6),
  ('user4', 1 << 7)
]

PROP_TYPES = {
  'int': 0,
  'float': 1,
  'bool': 2,
  'string': 3,
  'color': 4,
  'object': 5,
  'file': 3
}

warnings = []


def note(message):
  print('tiny_bake: %s' % message, file=sys.stderr)


def warn(message):
  warnings.append(message)
  print('tiny_bake: warning: %s' % message, file=sys.stderr)


def die(message):
  sys.exit('tiny_bake: %s' % message)


def identifier(text):
  """Turn a name into something usable as a C identifier."""
  name = re.sub(r'[^0-9a-zA-Z_]', '_', text or '')
  if not name or name[0].isdigit():
    name = 'm_' + name
  return name


def fnv1a(text):
  """The same hash tiny_hash() computes, so lookups can skip most strcmp calls."""
  if text is None:
    return 0
  hashed = 2166136261
  for byte in text.encode('utf-8'):
    hashed = ((hashed ^ byte) * 16777619) & 0xFFFFFFFF
  return hashed


def c_string(text):
  if text is None:
    return 'NULL'
  escaped = text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r')
  return '"%s"' % escaped


def c_float(value):
  """A C float literal. Bare digits are not one, so make sure a '.' or 'e' survives."""
  value = float(value)
  if value != value or value in (float('inf'), float('-inf')):
    warn('a non-finite number was clamped to 0')
    value = 0.0
  text = '%.6g' % value
  if '.' not in text and 'e' not in text and 'E' not in text:
    text += '.0'
  return text + 'f'


def pack_color(pixel, pixel_format):
  """Pack an RGBA tuple the way pntr_color's union lays out in memory on a little endian host.

  pntr_color overlays a uint32_t with four bytes whose order depends on the pixel format:
  PNTR_PIXELFORMAT_RGBA stores r,g,b,a and PNTR_PIXELFORMAT_ARGB stores b,g,r,a.
  """
  r, g, b, a = pixel
  if pixel_format == 'argb':
    return b | (g << 8) | (r << 16) | (a << 24)
  return r | (g << 8) | (b << 16) | (a << 24)


def parse_tiled_color(text):
  """Tiled writes colours as #RRGGBB or #AARRGGBB."""
  if not text:
    return (0, 0, 0, 255)
  text = text.lstrip('#')
  if len(text) == 6:
    return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16), 255)
  if len(text) == 8:
    return (int(text[2:4], 16), int(text[4:6], 16), int(text[6:8], 16), int(text[0:2], 16))
  warn('could not read the colour %r' % text)
  return (0, 0, 0, 255)


def properties(raw):
  """Flatten a Tiled property list into (name, type, value) triples."""
  out = []
  for prop in raw or []:
    kind = prop.get('type', 'string')
    if kind == 'class':
      warn('property %r is a class, which is not supported; flatten it in Tiled' % prop.get('name'))
      continue
    out.append((prop.get('name', ''), kind, prop.get('value')))
  return out


def prop_lookup(props, name, fallback=None):
  for prop_name, _kind, value in props:
    if prop_name == name:
      return value
  return fallback


# ---------------------------------------------------------------------------------------------
# Tilesets
# ---------------------------------------------------------------------------------------------


class Tileset:
  """One tileset, resolved far enough to slice pixels out of."""

  def __init__(self, uid, firstgid, data, base_dir):
    self.uid = uid
    self.firstgid = firstgid
    self.name = data.get('name', 'tileset%d' % uid)
    self.tile_w = data.get('tilewidth', 0)
    self.tile_h = data.get('tileheight', 0)
    self.spacing = data.get('spacing', 0)
    self.margin = data.get('margin', 0)
    self.columns = data.get('columns', 0)
    self.count = data.get('tilecount', 0)
    offset = data.get('tileoffset') or {}
    self.offset_x = offset.get('x', 0)
    self.offset_y = offset.get('y', 0)

    self.sheet = None
    if data.get('image'):
      path = os.path.normpath(os.path.join(base_dir, data['image']))
      try:
        self.sheet = Image.open(path).convert('RGBA')
      except Exception as error:
        die('cannot open the tileset image %s: %s' % (path, error))
      if not self.columns:
        self.columns = max(1, (self.sheet.width - 2 * self.margin + self.spacing) // (self.tile_w + self.spacing))

    # Per-tile records: properties, animation frames, class, and a standalone image for
    # collection tilesets, which is how sprite sheets of differently sized frames arrive.
    self.tiles = {}
    for tile in data.get('tiles', []):
      local_id = tile.get('id')
      record = {
        'props': properties(tile.get('properties')),
        'animation': tile.get('animation'),
        'type': tile.get('type') or tile.get('class'),
        'image': None
      }
      if tile.get('image'):
        path = os.path.normpath(os.path.join(base_dir, tile['image']))
        try:
          record['image'] = Image.open(path).convert('RGBA')
        except Exception as error:
          die('cannot open the tile image %s: %s' % (path, error))
      self.tiles[local_id] = record

  def slice(self, local_id):
    """The pixels of one tile, as an RGBA image."""
    record = self.tiles.get(local_id)
    if record is not None and record['image'] is not None:
      return record['image']
    if self.sheet is None:
      die('tileset %r has neither a sheet nor an image for tile %d' % (self.name, local_id))
    if not self.columns:
      die('tileset %r has no column count' % self.name)

    col = local_id % self.columns
    row = local_id // self.columns
    x = self.margin + col * (self.tile_w + self.spacing)
    y = self.margin + row * (self.tile_h + self.spacing)
    if x + self.tile_w > self.sheet.width or y + self.tile_h > self.sheet.height:
      die('tile %d is outside the %dx%d sheet for tileset %r'
          % (local_id, self.sheet.width, self.sheet.height, self.name))
    return self.sheet.crop((x, y, x + self.tile_w, y + self.tile_h))


def load_tilesets(map_data, map_path, cache):
  """Resolve a map's tilesets, sharing any that another map already loaded."""
  base_dir = os.path.dirname(os.path.abspath(map_path))
  out = []
  for entry in map_data.get('tilesets', []):
    firstgid = entry.get('firstgid', 1)
    if 'source' in entry:
      path = os.path.normpath(os.path.join(base_dir, entry['source']))
      key = path
      if key not in cache:
        try:
          with open(path) as handle:
            data = json.load(handle)
        except Exception as error:
          die('cannot read the tileset %s: %s (only JSON .tsj tilesets are supported)' % (path, error))
        cache[key] = (data, os.path.dirname(path))
      data, tileset_dir = cache[key]
    else:
      data, tileset_dir = entry, base_dir
    out.append(Tileset(len(out), firstgid, data, tileset_dir))
  # Highest firstgid first, so resolving a gid is a linear scan that stops at the right one.
  out.sort(key=lambda tileset: tileset.firstgid, reverse=True)
  return out


# ---------------------------------------------------------------------------------------------
# The tile table
# ---------------------------------------------------------------------------------------------


class TileTable:
  """The deduplicated, project-wide set of baked tiles and animations."""

  def __init__(self, pixel_format):
    self.pixel_format = pixel_format
    # Index 0 is the empty tile every layer uses for a hole.
    self.tiles = [None]
    self.by_key = {}
    self.anims = OrderedDict()
    self.anim_order = []
    self.slices = 0

  def flags_of(self, record):
    flags = 0
    for name, bit in TILE_FLAGS:
      if record is not None and prop_lookup(record['props'], name, False):
        flags |= bit
    return flags

  def bake(self, tileset, local_id, flip, animated=True):
    """Return the table index for one tile of one tileset, flipped as the map asked."""
    record = tileset.tiles.get(local_id)
    flags = self.flags_of(record)
    props = tuple(record['props']) if record is not None else ()

    anim_key = None
    if animated and record is not None and record['animation']:
      anim_key = (tileset.uid, local_id, flip)

    image = tileset.slice(local_id)
    self.slices += 1
    # Tiled applies the diagonal flip first, then the horizontal one, then the vertical one.
    if flip & FLIP_D:
      image = image.transpose(Image.TRANSPOSE)
    if flip & FLIP_H:
      image = image.transpose(Image.FLIP_LEFT_RIGHT)
    if flip & FLIP_V:
      image = image.transpose(Image.FLIP_TOP_BOTTOM)

    pixels = image.tobytes()
    key = (hash(pixels), len(pixels), image.size, flags, anim_key, props,
           tileset.offset_x, tileset.offset_y)
    if key in self.by_key:
      return self.by_key[key]

    index = len(self.tiles)
    if index > 0xFFFF:
      die('more than 65535 unique tiles, which does not fit the uint16 layer indices')
    self.tiles.append({
      'pixels': pixels,
      'width': image.width,
      'height': image.height,
      'flags': flags,
      'props': props,
      'anim': 0,
      'offset_x': tileset.offset_x,
      'offset_y': tileset.offset_y
    })
    self.by_key[key] = index

    if anim_key is not None:
      self.tiles[index]['anim'] = self._bake_anim(tileset, record, anim_key, flip)
    return index

  def _bake_anim(self, tileset, record, anim_key, flip):
    """Bake an animation's frames, with the same flip as the tile that referenced it."""
    if anim_key in self.anims:
      return self.anims[anim_key]['index']

    # Reserve the slot before recursing, so a self-referencing animation cannot loop.
    slot = {'index': len(self.anim_order) + 1, 'name': prop_lookup(record['props'], 'anim'), 'frames': []}
    self.anims[anim_key] = slot
    self.anim_order.append(slot)

    total = 0
    for frame in record['animation']:
      duration = int(frame.get('duration', 100))
      if duration <= 0:
        duration = 1
      # Frames are plain images: an animation whose frames animate is not a thing Tiled makes.
      slot['frames'].append((self.bake(tileset, frame.get('tileid', 0), flip, animated=False), duration))
      total += duration

    if total > 0xFFFF:
      warn('animation %r runs for %dms, which does not fit; it was clamped to 65535ms'
           % (slot['name'] or anim_key, total))
      total = 0xFFFF
    slot['total'] = total
    return slot['index']

  def bytes_used(self):
    return sum(len(tile['pixels']) for tile in self.tiles[1:])


# ---------------------------------------------------------------------------------------------
# Maps
# ---------------------------------------------------------------------------------------------


def decode_layer(layer):
  """Tiled writes tile layers as a CSV array or as base64, optionally compressed."""
  data = layer.get('data')
  if data is None:
    return []
  if isinstance(data, list):
    return data

  encoding = layer.get('encoding', 'csv')
  if encoding != 'base64':
    die('layer %r uses the unsupported encoding %r' % (layer.get('name'), encoding))

  raw = base64.b64decode(data)
  compression = layer.get('compression', '')
  if compression == 'zlib':
    raw = zlib.decompress(raw)
  elif compression == 'gzip':
    raw = zlib.decompress(raw, 16 + zlib.MAX_WBITS)
  elif compression:
    die('layer %r uses the unsupported compression %r; save the map as CSV or zlib'
        % (layer.get('name'), compression))

  return [int.from_bytes(raw[i:i + 4], 'little') for i in range(0, len(raw), 4)]


def resolve_gid(tilesets, gid):
  """Split a Tiled global tile id into its tileset, local id, and flip bits."""
  flip = gid & (FLIP_H | FLIP_V | FLIP_D)
  local = gid & GID_MASK
  if local == 0:
    return None, 0, 0
  for tileset in tilesets:
    if local >= tileset.firstgid:
      return tileset, local - tileset.firstgid, flip
  warn('gid %d does not belong to any tileset' % local)
  return None, 0, 0


class BakedMap:
  """One map, chewed down to what the C structs need."""

  def __init__(self, path, table, name_override=None):
    with open(path) as handle:
      data = json.load(handle)

    self.path = path
    self.data = data
    self.props = properties(data.get('properties'))
    self.name = name_override or prop_lookup(self.props, 'name') or os.path.splitext(os.path.basename(path))[0]
    self.ident = identifier(self.name)

    orientation = data.get('orientation', 'orthogonal')
    if orientation not in ('orthogonal', 'isometric'):
      die('map %r is %r; only orthogonal and isometric maps are supported'
          % (self.name, orientation))
    self.iso = orientation == 'isometric'
    self.orientation = 'TINY_ISOMETRIC' if self.iso else 'TINY_ORTHOGONAL'

    raw_type = str(prop_lookup(self.props, 'map_type', '') or '').strip().lower()
    if not raw_type:
      raw_type = 'isometric' if self.iso else 'topdown'
      warn('map %r has no map_type property, defaulting to %r' % (self.name, raw_type))
    if raw_type not in MAP_TYPES:
      die('map %r has map_type %r; expected one of: %s'
          % (self.name, raw_type, ', '.join(sorted(MAP_TYPES))))
    self.type = MAP_TYPES[raw_type]

    self.width = data.get('width', 0)
    self.height = data.get('height', 0)
    self.tile_w = data.get('tilewidth', 0)
    self.tile_h = data.get('tileheight', 0)
    self.background = parse_tiled_color(data.get('backgroundcolor'))

    self.tilesets = load_tilesets(data, path, table.tileset_cache)
    self.layers = []
    self.objects = []
    self._walk(data.get('layers', []), table)
    self._settle_collision()

  def _world_scale(self):
    """Tiled stores isometric object positions in units of one tile height, on both axes."""
    return float(self.tile_h) if self.iso else 1.0

  def _walk(self, layers, table, offset=(0.0, 0.0), opacity_parent=True):
    """Flatten Tiled's layer tree. Groups contribute their offset and visibility."""
    for layer in layers:
      kind = layer.get('type')
      visible = layer.get('visible', True) and opacity_parent
      off = (offset[0] + layer.get('offsetx', 0.0), offset[1] + layer.get('offsety', 0.0))

      if kind == 'group':
        self._walk(layer.get('layers', []), table, off, visible)
        continue

      if kind == 'tilelayer':
        self._add_tile_layer(layer, table, off, visible)
      elif kind == 'objectgroup':
        self._add_object_layer(layer, table, off, visible)
      elif kind == 'imagelayer':
        warn('image layer %r in map %r was skipped; convert it to a tile layer'
             % (layer.get('name'), self.name))
      else:
        warn('layer %r in map %r has the unknown type %r' % (layer.get('name'), self.name, kind))

  def _add_tile_layer(self, layer, table, offset, visible):
    gids = decode_layer(layer)
    width = layer.get('width', self.width)
    height = layer.get('height', self.height)
    if len(gids) != width * height:
      die('layer %r in map %r has %d cells but is %dx%d'
          % (layer.get('name'), self.name, len(gids), width, height))

    indices = []
    for gid in gids:
      tileset, local, flip = resolve_gid(self.tilesets, gid)
      indices.append(0 if tileset is None else table.bake(tileset, local, flip))

    props = properties(layer.get('properties'))
    self.layers.append({
      'kind': 'TINY_LAYER_TILE',
      'name': layer.get('name', ''),
      'visible': visible,
      'sort': False,
      'collision': prop_lookup(props, 'collision'),
      'width': width,
      'height': height,
      'offset': offset,
      'parallax': (layer.get('parallaxx', 1.0), layer.get('parallaxy', 1.0)),
      'tiles': indices,
      'props': props
    })

  def _add_object_layer(self, layer, table, offset, visible):
    props = properties(layer.get('properties'))
    index = len(self.layers)
    # Depth sorting is what every mode except a platformer and a fixed arcade screen wants.
    default_sort = self.type != 'TINY_MAP_PLATFORMER'
    self.layers.append({
      'kind': 'TINY_LAYER_OBJECT',
      'name': layer.get('name', ''),
      'visible': visible,
      'sort': bool(prop_lookup(props, 'sort', default_sort)),
      'collision': None,
      'width': 0,
      'height': 0,
      'offset': offset,
      'parallax': (layer.get('parallaxx', 1.0), layer.get('parallaxy', 1.0)),
      'tiles': None,
      'props': props
    })

    scale = self._world_scale()
    for obj in layer.get('objects', []):
      x = obj.get('x', 0.0) + offset[0]
      y = obj.get('y', 0.0) + offset[1]
      w = obj.get('width', 0.0)
      h = obj.get('height', 0.0)

      if obj.get('rotation'):
        warn('object %r in map %r is rotated, which is ignored' % (obj.get('name'), self.name))

      tile = 0
      shape = 'TINY_SHAPE_RECT'
      if obj.get('gid'):
        tileset, local, flip = resolve_gid(self.tilesets, obj['gid'])
        if tileset is not None:
          tile = table.bake(tileset, local, flip)
        shape = 'TINY_SHAPE_TILE'
        # Tiled anchors a tile object at its bottom-left; the engine works in top-left.
        y -= h
      elif obj.get('point'):
        shape = 'TINY_SHAPE_POINT'
      elif obj.get('ellipse'):
        shape = 'TINY_SHAPE_ELLIPSE'
      elif obj.get('polygon') or obj.get('polyline'):
        shape = 'TINY_SHAPE_POLYGON'
        warn('object %r in map %r is a polygon; only its bounding box is baked' % (obj.get('name'), self.name))

      self.objects.append({
        'name': obj.get('name') or None,
        'type': obj.get('type') or obj.get('class') or None,
        'id': obj.get('id', 0),
        'x': x / scale,
        'y': y / scale,
        'w': w / scale,
        'h': h / scale,
        'tile': tile,
        'shape': shape,
        'layer': index,
        'visible': obj.get('visible', True),
        'props': properties(obj.get('properties'))
      })

  def _settle_collision(self):
    """
    A layer collides if it says so. When no layer says anything, they all collide, which is
    what you want when solidity was set on the tiles in the tileset rather than per layer.
    """
    tile_layers = [layer for layer in self.layers if layer['kind'] == 'TINY_LAYER_TILE']
    declared = [layer for layer in tile_layers if layer['collision'] is not None]
    for layer in tile_layers:
      layer['collision'] = bool(layer['collision']) if declared else True


# ---------------------------------------------------------------------------------------------
# Emitting C
# ---------------------------------------------------------------------------------------------


def emit_props(out, symbol, props, pixel_format):
  """Write a property array, or return the empty bag when there is nothing to write."""
  if not props:
    return '{ .items = NULL, .count = 0 }'

  out.append('static const tiny_prop %s[] = {' % symbol)
  for name, kind, value in props:
    type_id = PROP_TYPES.get(kind, 3)
    if kind == 'float':
      body = '.as = { .f = %s }' % c_float(value)
    elif kind == 'bool':
      body = '.as = { .i = %d }' % (1 if value else 0)
    elif kind in ('int', 'object'):
      body = '.as = { .i = %d }' % int(value or 0)
    elif kind == 'color':
      body = '.as = { .i = (int32_t)0x%08X }' % pack_color(parse_tiled_color(value), pixel_format)
    else:
      body = '.as = { .s = %s }' % c_string('' if value is None else str(value))
    out.append('  { .name = %s, .hash = 0x%08XU, .type = %d, %s },'
               % (c_string(name), fnv1a(name), type_id, body))
  out.append('};')
  out.append('')
  return '{ .items = %s, .count = %d }' % (symbol, len(props))


def emit_tiles(table, pixel_format, columns):
  out = []
  out.append('/**')
  out.append(' * The shared tile table -- baked by tiny_bake.py. Do not edit.')
  out.append(' *')
  out.append(' * %d unique tiles, %d bytes of flash. The pixels live in .rodata, which the ESP32'
             % (len(table.tiles) - 1, table.bytes_used()))
  out.append(' * memory maps for reads, so drawing from them costs no heap. They are READ ONLY:')
  out.append(' * fine as a pntr_draw_image() source, but never as a destination.')
  out.append(' */')
  out.append('#ifndef TINY_BAKED_TILES_H')
  out.append('#define TINY_BAKED_TILES_H')
  out.append('')
  out.append('#include "tinyengine.h"')
  out.append('')

  # The packing above depends on the pixel format, so refuse to compile under the wrong one.
  wrong = 'PNTR_PIXELFORMAT_ARGB' if pixel_format == 'rgba' else 'PNTR_PIXELFORMAT_RGBA'
  other = 'argb' if pixel_format == 'rgba' else 'rgba'
  out.append('#if defined(%s)' % wrong)
  out.append('#error "These tiles were baked for PNTR_PIXELFORMAT_%s. Re-run tiny_bake.py --format %s."'
             % (pixel_format.upper(), other))
  out.append('#endif')
  out.append('')
  out.append('#define TINY_TILE_COUNT %d' % len(table.tiles))
  out.append('#define TINY_ANIM_COUNT %d' % len(table.anim_order))
  out.append('')

  for index, tile in enumerate(table.tiles):
    if tile is None:
      continue
    raw = tile['pixels']
    values = [pack_color(raw[i:i + 4], pixel_format) for i in range(0, len(raw), 4)]
    out.append('static const pntr_color tiny_px_%d[%d] = {' % (index, len(values)))
    for start in range(0, len(values), columns):
      row = values[start:start + columns]
      # pntr_color is a union, so each element needs braces and a named member.
      out.append('  ' + ' '.join('{ .value = 0x%08X },' % value for value in row))
    out.append('};')
  out.append('')

  tile_props = {}
  for index, tile in enumerate(table.tiles):
    if tile is None or not tile['props']:
      continue
    tile_props[index] = emit_props(out, 'tiny_tileprops_%d' % index, list(tile['props']), pixel_format)

  for slot in table.anim_order:
    out.append('static const tiny_anim_frame tiny_animframes_%d[] = {' % slot['index'])
    for tile_index, duration in slot['frames']:
      out.append('  { .tile = %d, .ms = %d },' % (tile_index, duration))
    out.append('};')
  if table.anim_order:
    out.append('')

  out.append('static const tiny_tile TINY_TILES[TINY_TILE_COUNT] = {')
  for index, tile in enumerate(table.tiles):
    if tile is None:
      out.append('  /* 0 */ { .image = { .data = NULL, .width = 0, .height = 0, .pitch = 0, .subimage = false,')
      out.append('            .clip = { 0, 0, 0, 0 } }, .flags = 0, .anim = 0, .offset_x = 0, .offset_y = 0,')
      out.append('            .props = { .items = NULL, .count = 0 } },')
      continue
    width, height = tile['width'], tile['height']
    out.append('  /* %d */ { .image = { .data = (pntr_color*)tiny_px_%d, .width = %d, .height = %d,'
               % (index, index, width, height))
    out.append('            .pitch = %d * (int)sizeof(pntr_color), .subimage = false,' % width)
    out.append('            .clip = { 0, 0, %d, %d } },' % (width, height))
    out.append('            .flags = 0x%02X, .anim = %d, .offset_x = %d, .offset_y = %d,'
               % (tile['flags'], tile['anim'], tile['offset_x'], tile['offset_y']))
    out.append('            .props = %s },' % tile_props.get(index, '{ .items = NULL, .count = 0 }'))
  out.append('};')
  out.append('')

  if table.anim_order:
    out.append('static const tiny_anim TINY_ANIMS[TINY_ANIM_COUNT] = {')
    for slot in table.anim_order:
      out.append('  { .name = %s, .hash = 0x%08XU, .frames = tiny_animframes_%d, .count = %d, .total_ms = %d },'
                 % (c_string(slot['name']), fnv1a(slot['name']), slot['index'], len(slot['frames']), slot['total']))
    out.append('};')
  else:
    # An empty array is not valid C, so give the pointer something legal to be.
    out.append('#define TINY_ANIMS NULL')
  out.append('')
  out.append('#endif  // TINY_BAKED_TILES_H')
  out.append('')
  return '\n'.join(out)


def emit_map(baked, pixel_format):
  out = []
  guard = 'TINY_BAKED_MAP_%s_H' % baked.ident.upper()
  out.append('/**')
  out.append(' * Map %r -- baked from %s by tiny_bake.py. Do not edit.' % (baked.name, os.path.basename(baked.path)))
  out.append(' */')
  out.append('#ifndef %s' % guard)
  out.append('#define %s' % guard)
  out.append('')
  out.append('#include "game_tiles.h"')
  out.append('#include "tinyengine.h"')
  out.append('')

  prefix = 'tiny_%s' % baked.ident

  layer_props = {}
  for index, layer in enumerate(baked.layers):
    if layer['props']:
      layer_props[index] = emit_props(out, '%s_layer%d_props' % (prefix, index), layer['props'], pixel_format)

  for index, layer in enumerate(baked.layers):
    if layer['tiles'] is None:
      continue
    out.append('static const uint16_t %s_layer%d[%d] = {' % (prefix, index, len(layer['tiles'])))
    for row in range(layer['height']):
      cells = layer['tiles'][row * layer['width']:(row + 1) * layer['width']]
      out.append('  ' + ' '.join('%d,' % cell for cell in cells))
    out.append('};')
  out.append('')

  out.append('static const tiny_layer %s_layers[%d] = {' % (prefix, max(1, len(baked.layers))))
  if not baked.layers:
    out.append('  { .name = NULL, .hash = 0, .kind = TINY_LAYER_TILE, .visible = false, .sort = false,')
    out.append('    .collision = false, .width = 0, .height = 0, .offset_x = 0.0f, .offset_y = 0.0f,')
    out.append('    .parallax_x = 1.0f, .parallax_y = 1.0f, .tiles = NULL,')
    out.append('    .props = { .items = NULL, .count = 0 } },')
  for index, layer in enumerate(baked.layers):
    tiles = 'NULL' if layer['tiles'] is None else '%s_layer%d' % (prefix, index)
    out.append('  { .name = %s, .hash = 0x%08XU, .kind = %s, .visible = %s, .sort = %s,'
               % (c_string(layer['name']), fnv1a(layer['name']), layer['kind'],
                  'true' if layer['visible'] else 'false', 'true' if layer['sort'] else 'false'))
    out.append('    .collision = %s, .width = %d, .height = %d, .offset_x = %s, .offset_y = %s,'
               % ('true' if layer['collision'] else 'false', layer['width'], layer['height'],
                  c_float(layer['offset'][0]), c_float(layer['offset'][1])))
    out.append('    .parallax_x = %s, .parallax_y = %s, .tiles = %s,'
               % (c_float(layer['parallax'][0]), c_float(layer['parallax'][1]), tiles))
    out.append('    .props = %s },' % layer_props.get(index, '{ .items = NULL, .count = 0 }'))
  out.append('};')
  out.append('')

  object_props = {}
  for index, obj in enumerate(baked.objects):
    if obj['props']:
      object_props[index] = emit_props(out, '%s_obj%d_props' % (prefix, index), obj['props'], pixel_format)

  out.append('static const tiny_object_def %s_objects[%d] = {' % (prefix, max(1, len(baked.objects))))
  if not baked.objects:
    out.append('  { .name = NULL, .type = NULL, .name_hash = 0, .type_hash = 0, .id = 0,')
    out.append('    .x = 0.0f, .y = 0.0f, .w = 0.0f, .h = 0.0f, .tile = 0, .shape = TINY_SHAPE_RECT,')
    out.append('    .layer = 0, .visible = false, .props = { .items = NULL, .count = 0 } },')
  for index, obj in enumerate(baked.objects):
    out.append('  { .name = %s, .type = %s, .name_hash = 0x%08XU, .type_hash = 0x%08XU, .id = %d,'
               % (c_string(obj['name']), c_string(obj['type']),
                  fnv1a(obj['name']), fnv1a(obj['type']), obj['id']))
    out.append('    .x = %s, .y = %s, .w = %s, .h = %s,'
               % (c_float(obj['x']), c_float(obj['y']), c_float(obj['w']), c_float(obj['h'])))
    out.append('    .tile = %d, .shape = %s, .layer = %d, .visible = %s,'
               % (obj['tile'], obj['shape'], obj['layer'], 'true' if obj['visible'] else 'false'))
    out.append('    .props = %s },' % object_props.get(index, '{ .items = NULL, .count = 0 }'))
  out.append('};')
  out.append('')

  map_props = emit_props(out, '%s_props' % prefix, baked.props, pixel_format)

  r, g, b, a = baked.background
  out.append('static const tiny_map TINY_MAP_%s = {' % baked.ident)
  out.append('  .name = %s,' % c_string(baked.name))
  out.append('  .hash = 0x%08XU,' % fnv1a(baked.name))
  out.append('  .type = %s,' % baked.type)
  out.append('  .orientation = %s,' % baked.orientation)
  out.append('  .width = %d, .height = %d,' % (baked.width, baked.height))
  out.append('  .tile_w = %d, .tile_h = %d,' % (baked.tile_w, baked.tile_h))
  out.append('  .background = { .value = 0x%08X },' % pack_color((r, g, b, a), pixel_format))
  out.append('  .layers = %s_layers, .layer_count = %d,' % (prefix, len(baked.layers)))
  out.append('  .objects = %s_objects, .object_count = %d,' % (prefix, len(baked.objects)))
  out.append('  .tiles = TINY_TILES, .tile_count = TINY_TILE_COUNT,')
  out.append('  .anims = TINY_ANIMS, .anim_count = TINY_ANIM_COUNT,')
  out.append('  .props = %s' % map_props)
  out.append('};')
  out.append('')
  out.append('#endif  // %s' % guard)
  out.append('')
  return '\n'.join(out)


def emit_registry(maps):
  out = []
  out.append('/**')
  out.append(' * Every map in the project -- baked by tiny_bake.py. Do not edit.')
  out.append(' *')
  out.append(' * Include this from exactly one translation unit: it defines the data, not just')
  out.append(' * declarations. Hand TINY_MAPS and TINY_MAP_COUNT to your tiny_game_def.')
  out.append(' */')
  out.append('#ifndef TINY_BAKED_MAPS_H')
  out.append('#define TINY_BAKED_MAPS_H')
  out.append('')
  out.append('#include "game_tiles.h"')
  for baked in maps:
    out.append('#include "map_%s.h"' % baked.ident)
  out.append('')
  out.append('#define TINY_MAP_COUNT %d' % len(maps))
  out.append('')
  out.append('static const tiny_map* const TINY_MAPS[TINY_MAP_COUNT] = {')
  for baked in maps:
    out.append('  &TINY_MAP_%s,' % baked.ident)
  out.append('};')
  out.append('')
  out.append('#endif  // TINY_BAKED_MAPS_H')
  out.append('')
  return '\n'.join(out)


# ---------------------------------------------------------------------------------------------


def report(table, maps, budget):
  tile_bytes = table.bytes_used()
  map_bytes = sum(sum(len(layer['tiles'] or []) for layer in baked.layers) * 2 for baked in maps)
  total = tile_bytes + map_bytes
  share = 100.0 * total / budget if budget else 0.0

  note('%d maps, %d tile slices baked down to %d unique tiles'
       % (len(maps), table.slices, len(table.tiles) - 1))
  note('%d bytes of tile pixels + %d bytes of layer data = %d bytes of flash (%.1f%% of a %d byte partition)'
       % (tile_bytes, map_bytes, total, share, budget))
  if table.anim_order:
    named = sum(1 for slot in table.anim_order if slot['name'])
    note('%d animations, %d of them named and playable with tiny_play()' % (len(table.anim_order), named))

  if total >= budget:
    warn('this exceeds the app partition on its own, so it will not link')
  elif share >= 50.0:
    warn('that is over half the partition; consider fewer or smaller tiles')


def main():
  parser = argparse.ArgumentParser(
    description='Bake Tiled maps into C headers for tinyengine.',
    epilog='Include the generated game_maps.h from one translation unit, after tinyengine.h.')
  parser.add_argument('maps', nargs='+', help='Tiled JSON maps (.tmj or .json)')
  parser.add_argument('-o', '--output', required=True, help='directory to write the headers into')
  parser.add_argument('--format', choices=('rgba', 'argb'), default='rgba',
                      help="pntr pixel format to pack for (default: rgba, which is what pntr_app uses)")
  parser.add_argument('--flash-budget', type=int, default=DEFAULT_FLASH_BUDGET, metavar='BYTES',
                      help='app partition size used for the size report')
  parser.add_argument('--columns', type=int, default=8, help='pixels per line of output')
  parser.add_argument('--strict', action='store_true', help='treat warnings as errors')
  args = parser.parse_args()

  table = TileTable(args.format)
  table.tileset_cache = {}

  baked_maps = []
  seen = {}
  for path in args.maps:
    baked = BakedMap(path, table)
    if baked.ident in seen:
      die('two maps are both called %r: %s and %s' % (baked.name, seen[baked.ident], path))
    seen[baked.ident] = path
    note('read %s as map %r (%s, %dx%d, %d layers, %d objects)'
         % (path, baked.name, baked.type[9:].lower(), baked.width, baked.height,
            len(baked.layers), len(baked.objects)))
    baked_maps.append(baked)

  os.makedirs(args.output, exist_ok=True)

  written = []
  path = os.path.join(args.output, 'game_tiles.h')
  with open(path, 'w') as handle:
    handle.write(emit_tiles(table, args.format, max(1, args.columns)))
  written.append(path)

  for baked in baked_maps:
    path = os.path.join(args.output, 'map_%s.h' % baked.ident)
    with open(path, 'w') as handle:
      handle.write(emit_map(baked, args.format))
    written.append(path)

  path = os.path.join(args.output, 'game_maps.h')
  with open(path, 'w') as handle:
    handle.write(emit_registry(baked_maps))
  written.append(path)

  report(table, baked_maps, args.flash_budget)
  note('wrote %d headers into %s' % (len(written), args.output))

  if args.strict and warnings:
    die('%d warnings, and --strict was given' % len(warnings))


if __name__ == '__main__':
  main()
