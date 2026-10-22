"""Generate private annotated and local-renamed Lua/HKS references without AI."""
import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import struct

from decompile_lua import atomic
import lua_readability

ROOT = Path(__file__).resolve().parents[2]
VERSION = 2
LONG = re.compile(r'\[(=*)\[')
WORD = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
NUMBER = re.compile(r'(?:0[xX][0-9a-fA-F]+|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?)')
KEYWORDS = set('and break do else elseif end false for function if in local nil not or repeat return then true until while'.split())
# These names describe the argument, not the undocumented gameplay meaning of its value.
ID_ARGS = {'HasSpecialEffectId': (1, 'special_effect'),
           'GetNumber': (0, 'ai_number_slot'), 'SetNumber': (0, 'ai_number_slot'),
           'GetTimer': (0, 'timer_slot'), 'SetTimer': (0, 'timer_slot')}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tokens(text):
    """Lexical tokens with original lines. Ignore comments and quoted/long strings.

    This is deliberately not a Lua parser and does not infer scope or execution.
    """
    result = []
    i, line = 0, 1
    while i < len(text):
        start, at_line = i, line
        if text[i].isspace():
            i += 1
        elif text.startswith('--', i):
            match = LONG.match(text, i + 2)
            if match:
                close = ']' + match[1] + ']'
                end = text.find(close, match.end())
                if end < 0:
                    raise ValueError(f'Unterminated long comment at line {line}')
                i = end + len(close)
            else:
                end = text.find('\n', i)
                i = len(text) if end < 0 else end
        elif text[i] in ('"', "'"):
            quote = text[i]
            i += 1
            while i < len(text) and text[i] != quote:
                i += 2 if text[i] == '\\' else 1
            if i >= len(text):
                raise ValueError(f'Unterminated string at line {line}')
            i += 1
            result.append(('<string>', at_line))
        elif (match := LONG.match(text, i)):
            close = ']' + match[1] + ']'
            end = text.find(close, match.end())
            if end < 0:
                raise ValueError(f'Unterminated long string at line {line}')
            i = end + len(close)
            result.append(('<string>', at_line))
        elif (match := WORD.match(text, i)) or (match := NUMBER.match(text, i)):
            i = match.end()
            result.append((match[0], at_line))
        else:
            i += 1
            result.append((text[start:i], at_line))
        line += text[start:i].count('\n')
    return result


def arguments(ts, opening):
    """Return token lists for a parenthesized call's arguments, respecting nesting."""
    args, current, stack = [], [], []
    pairs = {')': '(', ']': '[', '}': '{'}
    for value, _ in ts[opening + 1:]:
        if value == ')' and not stack:
            return args + [current] if current or args else []
        if value == ',' and not stack:
            args.append(current)
            current = []
            continue
        if value in ('(', '[', '{'):
            stack.append(value)
        elif value in pairs:
            if not stack or stack.pop() != pairs[value]:
                return []
        current.append(value)
    return []


def analyze(text):
    ts = tokens(text)
    definitions, definition_names = [], set()
    for i, (value, line) in enumerate(ts):
        if value != 'function' or i + 1 >= len(ts) or not WORD.fullmatch(ts[i + 1][0]):
            continue
        j, parts = i + 1, []
        while j < len(ts) and (WORD.fullmatch(ts[j][0]) or ts[j][0] in ('.', ':')):
            definition_names.add(j)
            parts.append(ts[j][0])
            j += 1
        if j < len(ts) and ts[j][0] == '(':
            definitions.append({'name': ''.join(parts), 'line': line,
                                'local': i > 0 and ts[i - 1][0] == 'local'})
    calls, ids, constants = defaultdict(list), [], defaultdict(list)
    for i, (value, line) in enumerate(ts):
        if re.fullmatch(r'[A-Z][A-Z0-9_]*', value) and '_' in value:
            constants[value].append(line)
        if not WORD.fullmatch(value) or value in KEYWORDS or i in definition_names:
            continue
        if i + 1 >= len(ts) or ts[i + 1][0] != '(':
            continue
        j = i
        while j >= 2 and ts[j - 1][0] in ('.', ':') and WORD.fullmatch(ts[j - 2][0]):
            j -= 2
        name = ''.join(t[0] for t in ts[j:i + 1])
        calls[name].append(line)
        if value in ID_ARGS:
            position, kind = ID_ARGS[value]
            args = arguments(ts, i + 1)
            if len(args) > position and len(args[position]) == 1 and re.fullmatch(r'\d+|0[xX][0-9a-fA-F]+', args[position][0]):
                raw = args[position][0]
                ids.append({'kind': kind, 'value': int(raw, 16 if raw.lower().startswith('0x') else 10),
                            'api': value, 'line': line, 'meaning': None})
    return {'functions': definitions,
            'calls': [{'name': k, 'lines': sorted(set(v))} for k, v in sorted(calls.items())],
            'constants': [{'name': k, 'lines': sorted(set(v))} for k, v in sorted(constants.items())],
            'id_references': ids}


def goal_entries(data):
    if len(data) < 16 or data[:8] != b'LUAI\x01\0\0\0':
        raise ValueError('Unsupported LUAINFO header')
    count = struct.unpack_from('<I', data, 8)[0]
    directory_end = 16 + 24 * count
    if directory_end > len(data):
        raise ValueError('Truncated LUAINFO directory')
    entries = []
    for i in range(count):
        ident, battle, logic, _, offset, _ = struct.unpack_from('<IBBHQQ', data, 16 + 24 * i)
        if offset % 2 or not directory_end <= offset < len(data):
            raise ValueError('Invalid LUAINFO name offset')
        end = offset
        while end + 2 <= len(data) and data[end:end + 2] != b'\0\0':
            end += 2
        if end + 2 > len(data):
            raise ValueError('Unterminated LUAINFO name')
        name = data[offset:end].decode('utf-16-le')
        for role, enabled, function in [('logic', logic, name), ('battle', battle, name + '_Activate')]:
            if enabled:
                entries.append({'goal_id': ident, 'role': role, 'function': function})
    return entries


def think_entries(data):
    if len(data) < 64 or data[12:44].split(b'\0')[0] != b'NPC_THINK_PARAM_ST' or data[44:48] != bytes.fromhex('00040700'):
        raise ValueError('Unsupported 1.09 NpcThinkParam layout')
    count = struct.unpack_from('<H', data, 10)[0]
    end = 64 + 24 * count
    if end > len(data):
        raise ValueError('Truncated NpcThinkParam directory')
    rows = defaultdict(set)
    for i in range(count):
        ident, _, offset, _ = struct.unpack_from('<iIQQ', data, 64 + 24 * i)
        if not end <= offset <= len(data) - 148:
            raise ValueError('NpcThinkParam row outside input')
        rows[ident].add(struct.unpack_from('<ii', data, offset))
    return rows


def child(root, relative):
    if not relative or '\\' in relative or ':' in relative or relative.startswith('/') or '..' in relative.split('/'):
        raise ValueError(f'Invalid relative path: {relative}')
    path = root / relative
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f'Path escapes root: {relative}')
    return path


def context(raw, placements):
    registrations, sources, warnings = defaultdict(list), {}, []
    for path in sorted((raw / 'dvdroot_ps4/script').glob('*/*.luainfo')):
        data = path.read_bytes()
        rel = path.relative_to(raw).as_posix()
        sources[rel] = sha(data)
        for entry in goal_entries(data):
            registrations[path.parent.name].append(dict(entry, source=rel))
    param = raw / 'dvdroot_ps4/param/gameparam/gameparam.parambnd.unpacked/NpcThinkParam.param'
    thinks = {}
    if param.is_file():
        data = param.read_bytes()
        sources[param.relative_to(raw).as_posix()] = sha(data)
        thinks = think_entries(data)
    else:
        warnings.append('NpcThinkParam unavailable; no think-row associations.')
    usage = defaultdict(list)
    for think, alternatives in sorted(thinks.items()):
        for logic, battle in sorted(alternatives):
            for role, goal in [('logic', logic), ('battle', battle)]:
                if goal >= 0:
                    usage[(role, goal)].append(think)
    placement_rows = []
    if placements:
        data = placements.read_bytes()
        export = json.loads(data)
        source_names = set()
        for source in export['sources']:
            path = child(raw / 'dvdroot_ps4/map/mapstudio', source['path'])
            if sha(path.read_bytes()) != source['sha256']:
                raise ValueError(f'Stale map placement export: {source["path"]}')
            source_names.add(Path(source['path']).stem)
            sources[path.relative_to(raw).as_posix()] = source['sha256']
        for row in export['placements']:
            if row['map'] not in source_names:
                raise ValueError('Placement has no verified map source')
            placement_rows.append({k: row[k] for k in ('map', 'model', 'think', 'npc')})
        sources['placement_export'] = sha(data)
    else:
        warnings.append('No placement export supplied; character/map usage is unresolved.')
    if not registrations:
        warnings.append('No LUAINFO registrations found.')
    return registrations, usage, placement_rows, sources, warnings


def label(record):
    names = [f['name'] for f in record['functions'] if not f['local']]
    name = next((n for n in names if n.endswith(('_Logic', '_Activate'))), names[0] if names else Path(record['path']).stem)
    name = re.sub(r'([a-z])([A-Z])', r'\1 \2', name)
    return re.sub(r'[_\s]+', ' ', name).strip()


def render(record, source):
    lines = ['-- Generated reference; not a deployable mod.',
             '-- Label (derived from symbols): ' + record['label'],
             '-- Source: ' + record['path'], '-- Source SHA-256: ' + record['sha256'],
             '-- Evidence: lexical extraction and static data associations; no gameplay validation.',
             '-- Original code follows unchanged; source line numbers below exclude this header.']
    for f in record['functions']:
        lines.append(f'-- Function: {f["name"]} (source line {f["line"]})')
    for g in record['goals']:
        lines.append(f'-- Registered {g["role"]} goal: {g["goal_id"]}; function: {g["function"]}; think rows: {g["think_ids"]}')
    for ref in record['id_references']:
        lines.append(f'-- {ref["kind"]}: {ref["value"]} via {ref["api"]} (source line {ref["line"]}); gameplay meaning unresolved.')
    lines.append('-- See adjacent .json for calls, constants, associations and duplicate-source paths.')
    return ('\n'.join(lines) + '\n\n').encode('utf-8') + source


def generate(source, raw, output, placements=None):
    source, raw, output = source.resolve(), raw.resolve(), output.resolve()
    for input_root in (source, raw):
        if output.is_relative_to(input_root) or input_root.is_relative_to(output):
            raise ValueError('Annotated output must be separate from input trees')
    if not source.is_dir() or not raw.is_dir():
        raise ValueError('Decompiled source and raw extraction directories are required')
    output.mkdir(parents=True, exist_ok=True)
    lock = output / '.annotate.lock'
    fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    os.close(fd)
    try:
        return generate_locked(source, raw, output, placements)
    finally:
        lock.unlink(missing_ok=True)


def generate_locked(source, raw, output, placements):
    manifest_path = output / 'manifest.json'
    old = json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.exists() else {}
    # Preflight every previously owned file before changing any output.
    for rel, expected in old.get('outputs', {}).items():
        path = child(output, rel)
        if path.exists() and sha(path.read_bytes()) != expected:
            raise ValueError(f'Edited generated output preserved: {path}')
    regs, usage, placement_rows, context_sources, warnings = context(raw, placements)
    placement_index = defaultdict(list)
    for p in placement_rows:
        if p not in placement_index[(p['map'], p['think'])]:
            placement_index[(p['map'], p['think'])].append(p)
    report_path = source / 'report.json'
    report = json.loads(report_path.read_text(encoding='utf-8')) if report_path.exists() else None
    if report is None:
        warnings.append('Decompile report unavailable; input provenance is unverified.')
    elif not report.get('complete'):
        warnings.append('Decompilation is incomplete; this index covers available successful outputs only.')
    records, payloads, skipped = [], {}, []
    by_hash, definitions = defaultdict(list), defaultdict(list)
    tool_hash = sha(Path(__file__).read_bytes() + Path(lua_readability.__file__).read_bytes())
    cached = old.get('analysis', {}) if old.get('tool_sha256') == tool_hash else {}
    analyses, cache_hits = {}, 0
    for path in sorted((source / 'dvdroot_ps4').rglob('*')):
        if not path.is_file() or path.suffix.lower() not in ('.lua', '.luac', '.hks'):
            continue
        rel = path.relative_to(source).as_posix()
        child(source, rel)
        data = path.read_bytes()
        digest = sha(data)
        if report is not None:
            provenance = report.get('files', {}).get(path.relative_to(source / 'dvdroot_ps4').as_posix())
            if not provenance or provenance.get('status') not in ('decompiled', 'already_text') or provenance.get('output_sha256') != digest:
                skipped.append({'path': rel, 'reason': 'Missing, failed or mismatched decompile provenance'})
                continue
        try:
            previous = cached.get(rel, {})
            if previous.get('sha256') == digest:
                facts = previous['facts']
                cache_hits += 1
            else:
                facts = analyze(data.decode('utf-8-sig'))
                facts['readability'] = lua_readability.recover(data.decode('utf-8'))
        except (UnicodeError, ValueError) as error:
            skipped.append({'path': rel, 'reason': str(error)})
            continue
        analyses[rel] = {'sha256': digest, 'facts': facts}
        record = dict(facts, path=rel, sha256=digest, goals=[], placements=[])
        record['label'] = label(record)
        bundle = path.parent.name
        # Action/HKS scripts are selected by character model, independently of AI goals.
        if path.suffix.lower() == '.hks' and re.fullmatch(r'c\d{4}', path.stem):
            record['placements'] = [p for p in placement_rows if p['model'] == path.stem]
        names = {f['name'] for f in facts['functions'] if not f['local']}
        for registration in regs.get(bundle, []):
            if registration['function'] in names:
                goal = dict(registration, think_ids=sorted(set(usage.get((registration['role'], registration['goal_id']), []))))
                record['goals'].append(goal)
                for think in goal['think_ids']:
                    for (map_id, think_id), rows in placement_index.items():
                        if think_id == think and bundle in (map_id + '.luabnd.unpacked', 'aicommon.luabnd.unpacked'):
                            for row in rows:
                                if row not in record['placements']:
                                    record['placements'].append(row)
        for f in facts['functions']:
            if not f['local']:
                definitions[f['name']].append({'path': rel, 'line': f['line']})
        by_hash[digest].append(rel)
        records.append(record)
        payloads[rel] = data
        if len(records) % 100 == 0:
            print(f'Analyzed {len(records)} scripts...', flush=True)
    outputs = {}
    def write(rel, data):
        path = child(output, rel)
        digest = sha(data)
        if path.exists():
            current = sha(path.read_bytes())
            if rel not in old.get('outputs', {}) and current != digest:
                raise ValueError(f'Unowned output preserved: {path}')
            if current == digest:
                outputs[rel] = digest
                return
        atomic(path, data)
        outputs[rel] = digest
    def json_bytes(value):
        return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    # Prepare all payloads first so collisions cannot cause a partially updated index.
    pending = {}
    for record in records:
        rel = record['path']
        record['identical_sources'] = [p for p in by_hash[record['sha256']] if p != rel]
        record['call_definition_candidates'] = [
            {'call': call['name'], 'definitions': definitions[call['name']]}
            for call in record['calls'] if ':' not in call['name'] and call['name'] in definitions]
        pending[rel] = render(record, payloads[rel])
        readable = record['readability']
        readable_path = 'readable/' + rel
        record['readable_path'] = readable_path
        text = payloads[rel].decode('utf-8')
        rewritten = lua_readability.apply_edits(text, readable['edits'])
        header = ('-- Generated readable reference; do not deploy.\n'
                  f'-- Original source: {rel}\n'
                  f'-- Metadata relative to annotation root: {rel}.json\n'
                  f'-- Local-name recovery: {readable["status"]}; {len(readable["renames"])} bindings renamed.\n'
                  '-- Body line count preserved; metadata lines refer to the original body.\n'
                  '-- Names describe syntax/API usage, not proven gameplay meaning.\n\n')
        pending[readable_path] = header.encode('utf-8') + rewritten.encode('utf-8')
        pending[rel + '.json'] = json_bytes(record)
    index = {'schema_version': VERSION, 'evidence': 'Static lexical facts and baseline associations; candidate definitions are not runtime call resolution.',
             'context_sources': context_sources, 'warnings': warnings, 'skipped': skipped,
             'scripts': records}
    unsupported = [r['path'] for r in records if r['readability']['status'] != 'verified']
    if unsupported:
        warnings.append(f'{len(unsupported)} readable bodies kept unchanged due to unsupported syntax; see per-script readability reasons.')
    pending['index.json'] = json_bytes(index)
    rows = ['path\tlabel\tfunctions\tgoal_ids\tmodels\tnpc_ids\tconstants\tids\treadable_variables\treadable_path']
    for r in records:
        rows.append('\t'.join([r['path'], r['label'], ' '.join(f['name'] for f in r['functions']),
                              ' '.join(str(g['goal_id']) for g in r['goals']),
                              ' '.join(sorted({p['model'] or '' for p in r['placements']})),
                              ' '.join(str(n) for n in sorted({p['npc'] for p in r['placements']})),
                              ' '.join(c['name'] for c in r['constants']),
                              ' '.join(sorted({f"{v['kind']}:{v['value']}" for v in r['id_references']})),
                              ' '.join(v['readable'] for v in r['readability']['renames']), r['readable_path']]))
    pending['search.tsv'] = ('\n'.join(rows) + '\n').encode('utf-8')
    pending['README.txt'] = (
        'Generated private references. Do not deploy or publish game-derived output.\n'
        'Start with search.tsv (text search or spreadsheet); index.json contains full evidence.\n'
        'Each script preserves its original body after an added header; adjacent JSON uses ORIGINAL source lines.\n'
        'Labels are derived from symbols, not verified gameplay descriptions. Original-body copies retain all names.\n'
        'readable/ contains separate scope-aware local-renamed copies; source bodies above remain unchanged.\n'
        'Adjacent source JSON records local renames, reasons and reversible character-offset edits.\n'
        'Renamed copies are reparsed for binding/token equivalence; debug/reflection may observe local names.\n'
        'Unsupported parsing leaves the readable body unchanged, with a reason in metadata.\n'
        'Calls are lexical occurrences; candidate definitions may be shadowed, unused or from other maps.\n'
        'Placements are baseline associations, not proof of live execution; common goals may be overridden per map.\n'
        'Numeric gameplay meanings remain unresolved. Identical contents retain all archive paths.\n'
        'Edited outputs are preserved by refusing regeneration. Use a fresh output folder to recover.\n'
        'Removed source outputs are retained on disk but excluded from the current index.\n'
    ).encode('utf-8')
    for rel, data in pending.items():
        path = child(output, rel)
        if path.exists() and rel not in old.get('outputs', {}) and path.read_bytes() != data:
            raise ValueError(f'Unowned output preserved: {path}')
    for rel, data in pending.items():
        write(rel, data)
    retained = {rel: digest for rel, digest in old.get('outputs', {}).items() if rel not in outputs}
    atomic(manifest_path, json_bytes({'schema_version': VERSION, 'tool_sha256': tool_hash,
                                    'outputs': dict(retained, **outputs), 'analysis': analyses}))
    summary = {'scripts': len(records), 'skipped': len(skipped), 'cached_analyses': cache_hits,
               'registered_scripts': sum(bool(r['goals']) for r in records),
               'placement_scripts': sum(bool(r['placements']) for r in records),
               'renamed_bindings': sum(len(r['readability']['renames']) for r in records),
               'readable_scripts': sum(bool(r['readability']['renames']) for r in records),
               'unsupported_readability': sum(r['readability']['status'] != 'verified' for r in records),
               'warnings': warnings}
    print(json.dumps(summary, indent=2))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'game-unpacked/decompiled')
    parser.add_argument('--raw', type=Path, default=ROOT / 'game-unpacked/raw')
    parser.add_argument('--output', type=Path, default=ROOT / 'game-unpacked/annotated')
    parser.add_argument('--placements', type=Path, help='Optional baseline placement JSON; source MSB hashes must match raw extraction')
    args = parser.parse_args()
    result = generate(args.source, args.raw, args.output, args.placements)
    if result['skipped']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
