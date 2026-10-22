"""Generate the framework's fourth top-menu cell from stock game assets.

Original inputs are read only. No new artwork or external GFX tool is needed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
import hud_layout as swf


def rect(bounds):
    w = swf.Writer()
    bits = swf.signed_bits(*bounds)
    w.unsigned(bits, 5)
    for value in bounds:
        w.unsigned(value & ((1 << bits) - 1), bits)
    return w.bytes()


def mask_shape(character, bounds):
    """Solid rectangle used only as a SWF clipping mask, in stock twips."""
    x0, x1, y0, y1 = bounds
    w = swf.Writer()
    w.unsigned(1, 4)  # one fill index bit
    w.unsigned(0, 4)  # no line styles
    w.unsigned(0, 1)
    w.unsigned(5, 5)  # MoveTo + FillStyle1
    bits = swf.signed_bits(x0, y0)
    w.unsigned(bits, 5)
    for value in (x0, y0):
        w.unsigned(value & ((1 << bits) - 1), bits)
    w.unsigned(1, 1)
    for vertical, value in ((False, x1-x0), (True, y1-y0), (False, x0-x1), (True, y0-y1)):
        bits = max(2, swf.signed_bits(value))
        w.unsigned(3, 2)  # straight edge
        w.unsigned(bits-2, 4)
        w.unsigned(0, 1)  # single-axis edge
        w.unsigned(vertical, 1)
        w.unsigned(value & ((1 << bits) - 1), bits)
    w.unsigned(0, 6)
    return swf.tag(2, struct.pack('<H', character) + rect(bounds) + b'\x01\x00\x00\x00\x00\x00' + w.bytes())


def build(data):
    if data[:3] != b'GFX' or struct.unpack_from('<I', data, 4)[0] != len(data):
        raise ValueError('Expected the uncompressed stock GFX movie')
    _, sprites, _ = swf.definitions(data)
    roots = swf.placements(data, swf.stage_start(data), len(data))
    start, end = sprites[roots['Top']]
    cells, used = {}, set()
    for code, _, body, length in swf.tags(data, start, end):
        if code not in (26, 70):
            continue
        char, span, name = swf.place_fields(data, body, code)
        depth = struct.unpack_from('<H', data, body + 1 + (code == 70))[0]
        used.add(depth)
        if name:
            cells[name] = code, body, length, char, span
    if 'Item_0_3' in cells:
        raise ValueError('Input already contains a fourth cell; use the stock movie')
    code, body, length, char, span = cells['Item_0_2']
    previous = cells['Item_0_1']
    if not span or not previous[4] or previous[3] != char:
        raise ValueError('Unexpected stock cell layout')
    matrix, _ = swf.read_matrix(data, span[0])
    last_matrix, _ = swf.read_matrix(data, previous[4][0])
    scale, rotate, (x, y) = matrix
    dx = x - last_matrix[2][0]
    if not 1000 < dx < 3000:
        raise ValueError('Unexpected top-cell spacing')
    depth = next(d for d in range(1, 65535) if d not in used)
    record = bytearray(data[body:body + length])
    struct.pack_into('<H', record, 1 + (code == 70), depth)
    record = record[:span[0] - body] + swf.write_matrix((scale, rotate, (x + dx, y))) + record[span[1] - body:]
    if record.count(b'Item_0_2\0') != 1:
        raise ValueError('Unexpected source instance name')
    record = record.replace(b'Item_0_2\0', b'Item_0_3\0')
    # The three top icons are baked into an unnamed sprite, separate from the
    # selectable text cells. Cloning a cell alone therefore produces no icon.
    # Reuse only the stock System icon via a clip mask; do not duplicate the
    # Inventory and Status icons or edit their original shape/placements.
    icon = [(c, b, n, span) for c, _, b, n in swf.tags(data, start, end)
            if c in (26, 70) for shape, span, name in [swf.place_fields(data, b, c)]
            if shape == 39 and name is None]
    if len(icon) != 1:
        raise ValueError('Unexpected stock top icon placement')
    _, _, _, icon_span = icon[0]
    original_shape = next((b for c, _, b, n in swf.tags(data, swf.stage_start(data), len(data))
                           if c == 22 and struct.unpack_from('<H', data, b)[0] == 38), None)
    if original_shape is None:
        raise ValueError('Stock top icon shape missing')
    r = swf.Reader(data, original_shape + 2)
    bits = r.unsigned(5)
    if tuple(r.signed(bits) for _ in range(4)) != (0, 5200, -160, 1442):
        raise ValueError('Stock top icon shape bounds changed')
    # All stock character definitions precede Top, whose ID is 74.
    if max(sprites) != 74:
        raise ValueError('Unexpected stock character namespace')
    mask_id, icon_id = 75, 76
    definitions = mask_shape(mask_id, (3600, 5200, -160, 1442))
    mask = swf.tag(26, b'\x46' + struct.pack('<HH', 1, mask_id) + swf.write_matrix((None, None, (0, 0))) + struct.pack('<H', 2))
    image = swf.tag(26, b'\x06' + struct.pack('<HH', 2, 38) + swf.write_matrix((None, None, (0, 0))))
    definitions += swf.tag(39, struct.pack('<HH', icon_id, 1) + mask + image + swf.tag(1, b'') + swf.tag(0, b''))
    icon_matrix, _ = swf.read_matrix(data, icon_span[0])
    icon_scale, icon_rotate, (ix, iy) = icon_matrix
    icon_depth = next(d for d in range(depth+1, 65535) if d not in used)
    icon_record = (b'\x26' + struct.pack('<HH', icon_depth, icon_id) +
                   swf.write_matrix((icon_scale, icon_rotate, (ix+dx, iy))) + b'BBModIcon\0')
    root_start = swf.stage_start(data)
    timeline = swf.rewrite_timeline(data, root_start, len(data), {},
        {start: swf.tag(code, record) + swf.tag(26, icon_record)})
    # Keep FileAttributes/metadata ordering, and define the new characters
    # before Top places them in its timeline.
    top_header = next(h for c, h, b, n in swf.tags(timeline, 0, len(timeline))
                      if c == 39 and struct.unpack_from('<H', timeline, b)[0] == roots['Top'])
    result = data[:root_start] + timeline[:top_header] + definitions + timeline[top_header:]
    result = result[:4] + struct.pack('<I', len(result)) + result[8:]
    _, new_sprites, _ = swf.definitions(result)
    new_roots = swf.placements(result, swf.stage_start(result), len(result))
    names = swf.placements(result, *new_sprites[new_roots['Top']])
    before = {name: item[3] for name, item in cells.items() if item[3] is not None}
    if names != {**before, 'Item_0_3': char, 'BBModIcon': icon_id}:
        raise ValueError('Generated movie changed existing cell identities')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=None)
    parser.add_argument('--out', type=Path, default=ROOT / '.local/native-ui-assets')
    args = parser.parse_args()
    if args.game is None:
        config = json.loads((ROOT / '.local/workspace.json').read_text())
        args.game = Path(config['game_assets']).parent
    game, out = args.game.resolve(), args.out.resolve()
    if out == game or game in out.parents or out in game.parents:
        raise ValueError('Output must be outside the original game tree')
    source = game / 'dvdroot_ps4/menu/ingametop.gfx'
    original = source.read_bytes()
    generated = build(original)
    target = out / 'dvdroot_ps4/menu/ingametop.gfx'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(generated)
    # The engine interprets each step name as a movie filename. The framework
    # aliases its custom steps to this stock hierarchy for the System factory.
    list_source = game / 'dvdroot_ps4/menu/optionsetting.gfx'
    list_original = list_source.read_bytes()
    list_target = out / 'dvdroot_ps4/menu/optionsetting.gfx'
    list_target.write_bytes(list_original)
    manifest = dict(version=1, source_sha256=hashlib.sha256(original).hexdigest(),
                    output_sha256=hashlib.sha256(generated).hexdigest(), native_navigation_validated=False,
                    added_cell='Top.Item_0_3', added_icon='Top.BBModIcon (stock System icon)', original_cells_preserved=True,
                    list_template='optionsetting.gfx',
                    list_template_sha256=hashlib.sha256(list_original).hexdigest())
    (out / 'native-ui-assets.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    assert source.read_bytes() == original
    assert list_source.read_bytes() == list_original
    print(f'Generated {target}; stock inputs unchanged. Native navigation remains unverified.')


if __name__ == '__main__':
    main()
