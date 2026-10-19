"""Offline Bloodborne content compiler. Original game archives are read-only inputs."""
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
FRAMEWORK = ROOT
_CONFIG = ROOT / '.local/workspace.json'
GAME = Path(json.loads(_CONFIG.read_text())['game_assets']) if _CONFIG.exists() else ROOT / '.local/UNCONFIGURED'
from archive_formats import unpack_dcx, pack_dcx, bnd4_files, rebuild_bnd4, write_fmg

PARAM_PATH = 'param/gameparam/gameparam.parambnd.dcx'
TABLE = {'weapon': 'EquipParamWeapon', 'protector': 'EquipParamProtector',
         'goods': 'EquipParamGoods', 'accessory': 'EquipParamAccessory'}
MASK = {'weapon': 0, 'protector': 0x10000000, 'accessory': 0x20000000, 'goods': 0x40000000}
FMG = {'weapon': (11, 21, 25), 'protector': (12, 22, 26),
       'goods': (10, 20, 24), 'accessory': (13, 23, 27)}
TYPES = {'weapon': 'weapon', 'armor': 'protector', 'accessory': 'accessory',
         'goods': 'goods', 'consumable': 'goods', 'key_item': 'goods', 'material': 'goods'}
FMT = {'S8': 'b', 'U8': 'B', 'S16': 'h', 'U16': 'H', 'S32': 'i', 'U32': 'I', 'F32': 'f'}
DEFAULT_TYPE = {'weapon': 'weapon', 'protector': 'armor', 'accessory': 'accessory', 'goods': 'goods'}
ID_RE = re.compile(r'^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$')
VERSION = 1


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fail(message):
    raise ValueError(message)


def read_fmg(data):
    """Bounded reader for the local 64-bit little-endian FMG format."""
    if len(data) < 40 or data[0:3] != bytes((0, 0, 2)):
        fail('Unsupported FMG header')
    size = struct.unpack_from('<I', data, 4)[0]
    groups, count = struct.unpack_from('<II', data, 12)
    offsets = struct.unpack_from('<Q', data, 24)[0]
    if size != len(data) or groups > 100000 or count > 100000 or 40+16*groups > len(data) or offsets+8*count > len(data):
        fail('Truncated/oversized FMG')
    strings = {}
    for g in range(groups):
        index, first, last, _ = struct.unpack_from('<iiii', data, 40+16*g)
        if first < 0 or last < first or index < 0 or index+last-first >= count:
            fail('Invalid FMG group')
        for k, ident in enumerate(range(first,last+1)):
            if ident in strings:
                fail('Duplicate FMG ID')
            at = struct.unpack_from('<q', data, offsets+8*(index+k))[0]
            if at == 0:
                strings[ident] = None
                continue
            if at < offsets+8*count or at % 2 or at >= len(data):
                fail('Invalid FMG string offset')
            end = at
            while end+2 <= len(data) and data[end:end+2] != b'\0\0':
                end += 2
            if end+2 > len(data):
                fail('Unterminated FMG string')
            strings[ident] = data[at:end].decode('utf-16-le')
    if len(strings) != count:
        fail('FMG directory count mismatch')
    return strings


def safe_relative(value):
    if not isinstance(value, str) or '\\' in value or ':' in value:
        fail('Use a relative forward-slash path')
    p = PurePosixPath(value)
    if p.is_absolute() or '..' in p.parts or not p.parts or str(p) != value:
        fail(f'Unsafe relative path: {value!r}')
    return value


def checked_child(root, rel):
    path = (root / safe_relative(rel)).resolve()
    if not path.is_relative_to(root.resolve()):
        fail('Path escapes source directory')
    return path


from param_schema import fields_schema


class Param:
    """Preserve row blobs and raw name strings; rebuild only 64-bit row indexes."""
    def __init__(self, data):
        if len(data) < 64 or data[44:48] != bytes.fromhex('00040700'):
            fail('Unsupported PARAM header; requires local 64-bit 1.09 layout')
        self.original = data
        self.header = data[:64]
        count = struct.unpack_from('<H', data, 10)[0]
        if count < 2 or 64 + count * 24 > len(data):
            fail('Unsupported/truncated PARAM row directory')
        records = [struct.unpack_from('<iIQQ', data, 64 + i * 24) for i in range(count)]
        self.stride = records[1][2] - records[0][2]
        self.tail_start = struct.unpack_from('<I', data)[0]
        if self.stride <= 0 or records[0][2] != 64 + count * 24 or self.tail_start != records[-1][2] + self.stride:
            fail('Non-contiguous PARAM layout')
        if struct.unpack_from('<Q', data, 48)[0] != records[0][2] or self.tail_start > len(data):
            fail('Unexpected PARAM data offset')
        self.rows, self.names, self.padding = {}, {}, {}
        for i, (row_id, pad, offset, name) in enumerate(records):
            if row_id in self.rows or offset != records[0][2] + i * self.stride:
                fail('Duplicate/non-contiguous PARAM rows')
            if name and not self.tail_start <= name < len(data):
                fail('PARAM name outside string block')
            self.rows[row_id] = bytearray(data[offset:offset+self.stride])
            self.names[row_id] = name
            self.padding[row_id] = pad
        self.order = list(self.rows)

    def clone(self, source, target):
        if target in self.rows:
            fail(f'Parameter row already exists: {target}')
        self.rows[target] = self.rows[source].copy()
        self.names[target] = 0
        self.padding[target] = 0

    def encode(self):
        # Preserve original order for a byte-identical no-op. Added rows are
        # sorted with all existing rows for the native binary-search indexes.
        ids = self.order if len(self.rows) == len(self.order) else sorted(self.rows)
        if len(ids) > 65535:
            fail('Too many PARAM rows')
        start = 64 + 24 * len(ids)
        tail = start + self.stride * len(ids)
        out = bytearray(self.header)
        struct.pack_into('<I', out, 0, tail)
        struct.pack_into('<H', out, 10, len(ids))
        struct.pack_into('<Q', out, 48, start)
        for i, row_id in enumerate(ids):
            name = self.names[row_id]
            out += struct.pack('<iIQQ', row_id, self.padding[row_id], start + i*self.stride,
                               name + tail - self.tail_start if name else 0)
        for row_id in ids:
            out += self.rows[row_id]
        out += self.original[self.tail_start:]
        return bytes(out)


def bnd_name(bnd, entry):
    at = struct.unpack_from('<I', bnd, entry+32)[0]
    end = at
    while end + 2 <= len(bnd) and bnd[end:end+2] != b'\0\0':
        end += 2
    if end + 2 > len(bnd):
        fail('Unterminated binder name')
    return bnd[at:end].decode('utf-16-le').replace('\\', '/').rsplit('/', 1)[-1]


def read_parameters(raw):
    bnd = unpack_dcx(raw)
    entries = {}
    for entry, offset, size, file_id in bnd4_files(bnd):
        name = bnd_name(bnd, entry)
        if name in entries or offset + size > len(bnd):
            fail('Duplicate/truncated binder member')
        entries[name] = (file_id, bnd[offset:offset+size])
    return bnd, entries


def patch_value(row, field, operation):
    ty, off, bit, bits = field['ty'], field['offset'], field['bit'], field['bits']
    fmt = '<' + FMT.get(ty, '')
    if ty not in FMT or off + struct.calcsize(fmt) > len(row):
        fail('Unsupported field layout')
    current = struct.unpack_from(fmt, row, off)[0]
    value = ((current >> bit) & ((1 << bits)-1)) if bits else current
    if isinstance(operation, dict):
        if len(operation) != 1 or next(iter(operation)) not in ('add', 'multiply'):
            fail('Field operation must be a number, {add:number}, or {multiply:number}')
        op, operand = next(iter(operation.items()))
    else:
        op, operand = 'set', operation
    if isinstance(operand, bool) or not isinstance(operand, (int, float)) or not math.isfinite(operand):
        fail('Field value must be finite numeric')
    value = operand if op == 'set' else value + operand if op == 'add' else value * operand
    if not math.isfinite(value):
        fail('Non-finite field result')
    if ty != 'F32':
        if op == 'set' and value != int(value):
            fail('Integer field requires integral assignment')
        value = math.trunc(value)
    if bits:
        if not 0 <= value < 1 << bits:
            fail('Bitfield out of range')
        value = (current & ~(((1 << bits)-1) << bit)) | (value << bit)
    try:
        struct.pack_into(fmt, row, off, value)
    except (struct.error, OverflowError):
        fail('Field result outside storage range')


def load_catalog():
    return json.loads((FRAMEWORK / 'sdk/rust/bbmod/local-data/items.json').read_text(encoding='utf-8'))


def read_specs(paths):
    specs, namespaces = [], set()
    for path in paths:
        path = Path(path).resolve()
        spec = json.loads(path.read_text(encoding='utf-8'))
        allowed = {'schema_version', 'namespace', 'items', 'edits', 'assets'}
        if set(spec) - allowed or spec.get('schema_version') != 1:
            fail(f'Unsupported content manifest: {path}')
        namespace = spec.get('namespace', '')
        if not re.fullmatch('[a-z][a-z0-9_]*', namespace) or namespace == 'bloodborne' or namespace in namespaces:
            fail('Invalid, reserved or duplicate mod namespace')
        namespaces.add(namespace)
        specs.append((namespace, path, spec))
    return sorted(specs)


def allocation(lock, ident, category, occupied):
    if lock.get('schema_version') != 1:
        fail('Unsupported ID lock format')
    records = lock['items']
    if ident in records:
        record = records[ident]
        if record['category'] != category:
            fail('An existing item cannot change its category')
        return record['param_id']
    # Weapon IDs use dynamic handles; goods IDs stay below the verified inline
    # resolver limit. A reservation is experimental until gameplay validation.
    start, stop, step = (50_000_000, 99_900_000, 100_000) if category == 'weapon' else (6_000_000, 7_900_000, 1000)
    occupied = set(occupied)
    for row in records.values():
        if row['category'] == category:
            occupied.update(range(row['param_id'], row['param_id'] + row['reserved_span']))
    span = 1100 if category == 'weapon' else 1
    for base in range(start, stop, step):
        if not any(base <= n < base+span for n in occupied):
            records[ident] = dict(category=category, param_id=base, reserved_span=span)
            return base
    fail('No available IDs in allocator range')


def validate_lock(lock):
    if not isinstance(lock, dict) or set(lock) != {'schema_version', 'items'} or lock['schema_version'] != 1 or not isinstance(lock['items'], dict):
        fail('Invalid item ID lock')
    spans = defaultdict(list)
    for ident, record in lock['items'].items():
        if not isinstance(ident,str) or not ID_RE.fullmatch(ident) or ident.startswith('bloodborne.') or not isinstance(record,dict) or set(record) != {'category', 'param_id', 'reserved_span'}:
            fail('Invalid ID lock record')
        category, base, span = record['category'], record['param_id'], record['reserved_span']
        if category not in MASK or type(base) is not int or type(span) is not int:
            fail('Invalid ID lock types')
        start, stop, step = (50_000_000, 99_900_000, 100_000) if category == 'weapon' else (6_000_000, 7_900_000, 1000)
        if not start <= base < stop or (base-start) % step or span != (1100 if category == 'weapon' else 1):
            fail('ID lock reservation outside supported range')
        if any(base < end and base+span > begin for begin,end in spans[category]):
            fail('Overlapping ID lock reservations')
        spans[category].append((base,base+span))


def rust_string(value):
    escaped = ''.join('\\"' if ch == '"' else '\\\\' if ch == '\\' else
                      f'\\u{{{ord(ch):x}}}' if ord(ch) < 32 or ord(ch) == 127 else ch for ch in value)
    return '"' + escaped + '"'


def rust_registry(registry):
    lines = ['// Generated content registry. Include in a mod built against bbmod.',
             'pub const ITEMS: &[bbmod::content::ItemDefinition<\'static>] = &[']
    quote = rust_string
    opt = lambda v: 'None' if v is None else 'Some(' + quote(v) + ')'
    for item in registry['items']:
        typ = ''.join(p.title() for p in item['type'].split('_'))
        tags = ','.join(quote(tag) for tag in item['tags'])
        lines.append('    bbmod::content::ItemDefinition { '
                     f'id:{quote(item["id"])}, item_type:bbmod::content::ItemType::{typ}, '
                     f'category:bbmod::items::Category::{item["category"].title()}, '
                     f'encoded_item_id:{item["encoded_item_id"]}, name:{opt(item["name"])}, '
                     f'tags:&[{tags}], base:{opt(item["base"])}, upgrade_level:{item["upgrade_level"]}' + ' },')
    return '\n'.join(lines + ['];', ''])


def compile_content(game, specs, lock):
    """Return files and report in memory; caller publishes only after validation."""
    validate_lock(lock)
    catalog = load_catalog()
    raw = (game / PARAM_PATH).read_bytes()
    expected = next(s['sha256'] for s in catalog['sources'] if s['path'] == PARAM_PATH)
    if digest(raw) != expected:
        fail('Game parameter archive differs from catalog baseline; rebuild/review catalog first')
    bnd, entries = read_parameters(raw)
    params = {name: Param(entries[name+'.param'][1]) for name in TABLE.values()}
    schema = fields_schema()
    for name, param in params.items():
        if schema[name][0] != param.stride:
            fail(f'Parameter schema stride mismatch: {name}')
    registry = dict(schema_version=1, game_version='1.09', items=[])
    by_key = catalog['items']
    for key, row in by_key.items():
        level = re.search(r'_plus_(\d+)$', key) if row['category'] == 'weapon' else None
        level = int(level[1]) if level and int(level[1]) <= 10 else 0
        registry['items'].append(dict(id='bloodborne.'+key, type=DEFAULT_TYPE[row['category']],
            category=row['category'], encoded_item_id=row['encoded_item_id'], name=row['name'],
            tags=[row['category']], base=None, upgrade_level=level))
    text_additions = defaultdict(dict)
    changed, changes, claims = set(), [], {}
    output = {}
    source_hashes = {PARAM_PATH: digest(raw)}
    all_ids = {i['id'] for i in registry['items']}

    def lookup(ident):
        if not isinstance(ident, str) or not ident.startswith('bloodborne.') or ident[11:] not in by_key:
            fail(f'Unknown vanilla donor/target: {ident}')
        return by_key[ident[11:]]

    def levels(ident, enabled):
        row = lookup(ident)
        if not isinstance(enabled, bool):
            fail('upgrades must be boolean')
        result = [(0, row)]
        if enabled:
            if row['category'] != 'weapon' or '_plus_' in ident:
                fail('upgrades requires a base weapon key')
            for level in range(1, 11):
                variant = lookup(ident + f'_plus_{level}')
                if variant['category'] != row['category']:
                    fail('Upgrade category mismatch')
                result.append((level, variant))
        return result

    def apply(table, row_id, fields, owner):
        if not isinstance(fields, dict):
            fail('fields must be an object')
        row = params[table].rows[row_id]
        for name, op in sorted(fields.items()):
            field = schema[table][1].get(name)
            if not field or name.startswith(('pad', 'dummy')):
                fail(f'Unknown or reserved field {table}.{name}')
            key = table, row_id, name
            mode = next(iter(op)) if isinstance(op, dict) and len(op) == 1 else 'set'
            if key in claims and ('set' in (mode, claims[key][0])):
                fail(f'Conflicting field edits {key}: {claims[key][1]} and {owner}')
            claims[key] = mode, owner
            before = bytes(row)
            patch_value(row, field, op)
            changes.append(dict(owner=owner, table=table, row_id=row_id, field=name,
                                operation=op, before_sha256=digest(before), after_sha256=digest(row)))
            changed.add(table)

    # Clone all donors before applying vanilla edits; one mod's rebalance must
    # not silently change another mod's inherited baseline.
    for namespace, path, spec in specs:
        for item in spec.get('items', []):
            if set(item) - {'id','base','type','name','description','tags','fields','upgrades'}:
                fail('Unknown new-item property')
            ident = item.get('id', '')
            if not ID_RE.fullmatch(ident) or not ident.startswith(namespace+'.') or ident in all_ids:
                fail('Invalid/duplicate namespaced item ID')
            category = TYPES.get(item.get('type'))
            donor = lookup(item['base'])
            if category != donor['category']:
                fail('Item type and donor category differ')
            name, desc = item.get('name'), item.get('description', '')
            if not isinstance(name, str) or not name.strip() or not isinstance(desc, str) or '\0' in name+desc:
                fail('Item requires a valid display name/description')
            tags = item.get('tags', [])
            if not isinstance(tags, list) or any(not isinstance(t, str) or not re.fullmatch('[a-z][a-z0-9_.-]*', t) for t in tags) or len(set(tags)) != len(tags):
                fail('Invalid/duplicate item tags')
            table = TABLE[category]
            variants = levels(item['base'], item.get('upgrades', False))
            occupied = set(params[table].rows)
            # Include text-only rows so an apparently vacant PARAM ID does not
            # silently steal an existing text ID.
            msg = unpack_dcx((game / 'msg/engus/item.msgbnd.dcx').read_bytes())
            for _, off, n, fid in bnd4_files(msg):
                if fid in FMG[category]:
                    occupied.update(read_fmg(msg[off:off+n]))
            base = allocation(lock, ident, category, occupied)
            remap = {v['param_id']:base + level*100 for level, v in variants}
            for level, variant in variants:
                rid = remap[variant['param_id']]
                qualified = ident + (f'_plus_{level}' if level else '')
                if qualified in all_ids:
                    fail('Generated upgrade ID collides with another item')
                all_ids.add(qualified)
                params[table].clone(variant['param_id'], rid)
                if category == 'weapon':
                    for fieldname, field in schema[table][1].items():
                        if fieldname.startswith('originEquipWep'):
                            off = field['offset']
                            old = struct.unpack_from('<i', params[table].rows[rid], off)[0]
                            if old in remap:
                                struct.pack_into('<i', params[table].rows[rid], off, remap[old])
                            elif old >= 0:
                                # A disabled upgrade chain must never upgrade
                                # into a vanilla weapon or unknown custom row.
                                struct.pack_into('<i', params[table].rows[rid], off, -1)
                apply(table, rid, item.get('fields', {}), ident)
                changed.add(table)
                display = name + (f' +{level}' if level else '')
                for fid, text in zip(FMG[category], (display, desc, desc)):
                    text_additions[fid][rid] = text
                registry['items'].append(dict(id=qualified, type=item['type'], category=category,
                    encoded_item_id=MASK[category] | rid, name=display, tags=tags,
                    base=item['base'], upgrade_level=level))
        for asset in spec.get('assets', []):
            if set(asset) != {'source', 'target'}:
                fail('Asset requires source and target')
            target = safe_relative(asset['target'])
            if not target.startswith('parts/') or not target.endswith('.partsbnd.dcx'):
                fail('This content version accepts new parts/*.partsbnd.dcx assets only')
            if (game / target).exists() or target.casefold() in {k.casefold() for k in output}:
                fail('Asset target conflicts with base game or another mod')
            source = checked_child(path.parent, asset['source'])
            payload = source.read_bytes()
            unpack_dcx(payload)  # reject unsupported compression/container
            output[target] = payload
            source_hashes[f'{namespace}/{asset["source"]}'] = digest(payload)

    for namespace, _, spec in specs:
        for edit in spec.get('edits', []):
            if set(edit) - {'item', 'fields', 'upgrades'}:
                fail('Unknown edit property')
            for _, variant in levels(edit['item'], edit.get('upgrades', False)):
                apply(TABLE[variant['category']], variant['param_id'], edit['fields'], namespace)

    if changed:
        replacements = {entries[name+'.param'][0]: params[name].encode() for name in changed}
        output[PARAM_PATH] = pack_dcx(raw, rebuild_bnd4(bnd, replacements))
        # Reparse every changed output and compare every expected row byte.
        _, rebuilt = read_parameters(output[PARAM_PATH])
        for name in changed:
            decoded = Param(rebuilt[name+'.param'][1])
            if decoded.rows != params[name].rows:
                fail('Rebuilt parameter verification failed')
        for name, (_, blob) in entries.items():
            if name[:-6] not in changed and rebuilt[name][1] != blob:
                fail('Unrelated parameter table changed')

    if text_additions:
        # Install English custom labels into each supported locale while
        # retaining every existing localized string verbatim.
        for path in sorted((game / 'msg').glob('*/item.msgbnd.dcx')):
            rel = path.relative_to(game).as_posix()
            template = path.read_bytes()
            source_hashes[rel] = digest(template)
            archive = unpack_dcx(template)
            replacements = {}
            found = set()
            for _, offset, size, fid in bnd4_files(archive):
                if fid not in text_additions:
                    continue
                found.add(fid)
                original = archive[offset:offset+size]
                strings = read_fmg(original)
                for key, value in text_additions[fid].items():
                    if key in strings:
                        fail(f'New text ID collides in {rel}: {fid}/{key}')
                    strings[key] = value
                encoded = write_fmg(original, strings)
                if read_fmg(encoded) != strings:
                    fail('Text roundtrip failed')
                replacements[fid] = encoded
            if found != set(text_additions):
                fail(f'Missing item text tables: {rel}')
            output[rel] = pack_dcx(template, rebuild_bnd4(archive, replacements))
    registry['items'].sort(key=lambda i:i['id'])
    report = dict(schema_version=1, game_version='1.09',
                  live_validated=False, requires_restart=True, source_hashes=source_hashes,
                  mods=[n for n, _, _ in specs], field_changes=changes,
                  new_items=[i for i in registry['items'] if not i['id'].startswith('bloodborne.')],
                  outputs={p:digest(b) for p,b in sorted(output.items())})
    return output, registry, report
