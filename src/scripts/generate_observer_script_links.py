"""Join baseline MSB enemy placements to NpcThinkParam and recovered script files."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT.parent / 'modding/mods/character-observer'
TREE = ROOT / 'game-unpacked/raw'
SCRIPTS = ROOT / 'game-unpacked/decompiled'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def think_rows(data):
    if len(data) < 64 or data[12:44].split(b'\0')[0] != b'NPC_THINK_PARAM_ST' or data[44:48] != bytes.fromhex('00040700'):
        raise ValueError('Unsupported 1.09 NpcThinkParam layout')
    count = struct.unpack_from('<H', data, 10)[0]
    end = 64 + 24 * count
    rows = defaultdict(set)
    if end > len(data):
        raise ValueError('Truncated think row directory')
    for i in range(count):
        ident, _, at, _ = struct.unpack_from('<iIQQ', data, 64 + 24 * i)
        if not end <= at <= len(data) - 148:
            raise ValueError('Think row data outside file')
        # Preserve all alternatives: the baseline contains duplicate ID 250100.
        rows[ident].add(struct.unpack_from('<ii', data, at))
    return rows


def wide(data, at):
    if at % 2 or not 16 <= at < len(data):
        raise ValueError('Invalid LUAINFO string offset')
    end = at
    while end + 2 <= len(data) and data[end:end+2] != b'\0\0':
        end += 2
    if end + 2 > len(data):
        raise ValueError('Unterminated LUAINFO string')
    return data[at:end].decode('utf-16-le')


def goals(data):
    if data[:8] != b'LUAI\x01\0\0\0':
        raise ValueError('Unsupported LUAINFO')
    count = struct.unpack_from('<I', data, 8)[0]
    if 16 + 24 * count > len(data):
        raise ValueError('Truncated LUAINFO')
    result = []
    for i in range(count):
        ident, battle, logic, _, name, _ = struct.unpack_from('<IBBHQQ', data, 16 + 24 * i)
        result.append((ident, bool(battle), bool(logic), wide(data, name)))
    return result


def catalog(mod=MOD, tree=TREE, scripts=SCRIPTS):
    placements = json.loads((mod / 'data/map-placements.json').read_text(encoding='utf-8-sig'))
    for source in placements['sources']:
        p = tree / 'dvdroot_ps4/map/mapstudio' / source['path']
        if digest(p.read_bytes()) != source['sha256']:
            raise ValueError(f'Stale placement export: {p}')
    param = tree / 'dvdroot_ps4/param/gameparam/gameparam.parambnd.unpacked/NpcThinkParam.param'
    raw = param.read_bytes()
    original = tree / 'dvdroot_ps4/param/gameparam/gameparam.parambnd.unpacked/NpcThinkParam.param'
    if raw != original.read_bytes():
        raise ValueError('NpcThinkParam differs from the unpacked baseline')
    thinks = think_rows(raw)
    sources = {str(param.resolve()): digest(raw)}
    registrations = {}
    functions = {}
    for folder in sorted((tree / 'dvdroot_ps4/script').glob('*.luabnd.unpacked')):
        regs = []
        for info in sorted(folder.glob('*.luainfo')):
            raw = info.read_bytes()
            sources[str(info.resolve())] = digest(raw)
            regs.extend(goals(raw))
        registrations[folder.name.removesuffix('.luabnd.unpacked')] = regs
        for file in sorted((scripts / folder.relative_to(tree)).glob('*.lua')):
            text = file.read_text(encoding='utf-8')
            sources[str(file.resolve())] = digest(file.read_bytes())
            for name in re.findall(r'^function\s+([\w]+)\s*\(', text, re.M):
                functions.setdefault((folder.name, name), []).append(file.relative_to(scripts).as_posix())
    links = set()
    unresolved = set()
    for p in placements['placements']:
        if p['npc'] < 0:
            continue
        model, think, npc, map_id = p['model'] or '', p['think'], p['npc'], p['map']
        if re.fullmatch(r'c\d{4}', model):
            action = scripts / 'dvdroot_ps4/action/script' / (model + '.hks')
            if action.is_file():
                links.add((npc, map_id, 'Action (model)', action.relative_to(scripts).as_posix(), model, think, -1, ''))
                sources[str(action.resolve())] = digest(action.read_bytes())
        for logic, battle in sorted(thinks.get(think, ())):
            for role, goal in [('Logic', logic), ('Battle', battle)]:
                if goal < 0:
                    continue
                matches = []
                for bundle in [map_id, 'aicommon']:
                    for ident, bat, log, name in registrations.get(bundle, []):
                        if ident == goal and (bat if role == 'Battle' else log):
                            function = name + '_Activate' if role == 'Battle' else name
                            for path in functions.get((bundle + '.luabnd.unpacked', function), []):
                                matches.append((path, function))
                for path, function in matches:
                    links.add((npc, map_id, role, path, model, think, goal, function))
                if not matches:
                    unresolved.add((map_id, npc, think, role, goal))
    return sorted(links), dict(description='Offline baseline placement/goal associations, not live execution.',
        placements=len(placements['placements']), links=len(links), sources=sources,
        duplicate_think_ids=[i for i, values in thinks.items() if len(values)>1],
        unresolved_goals=[list(x) for x in sorted(unresolved)])


def rust(links):
    q = lambda s: json.dumps(s, ensure_ascii=False)
    lines = ['// Generated by src/scripts/generate_observer_script_links.py.', 'pub static LINKS: &[ScriptLink] = &[']
    for npc, map_id, role, path, model, think, goal, function in links:
        lines.append(f'ScriptLink {{ npc: {npc}, map: {q(map_id)}, role: {q(role)}, path: {q(path)}, model: {q(model)}, think: {think}, goal: {goal}, function: {q(function)} }},')
    return '\n'.join(lines + ['];', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--mod', type=Path, default=MOD)
    parser.add_argument('--raw', type=Path, default=TREE)
    parser.add_argument('--decompiled', type=Path, default=SCRIPTS)
    args = parser.parse_args()
    links, provenance = catalog(args.mod.resolve(), args.raw.resolve(), args.decompiled.resolve())
    outputs = [(args.mod / 'src/script_links_generated.rs', rust(links)),
               (args.mod / 'data/script-links-provenance.json', json.dumps(provenance,ensure_ascii=False,indent=2)+'\n')]
    for path, text in outputs:
        if args.check:
            if path.read_text(encoding='utf-8') != text:
                raise ValueError(f'Stale generated catalog: {path}')
        else:
            path.write_text(text,encoding='utf-8',newline='\n')
    print(f'{len(links)} script links; {len(provenance["unresolved_goals"])} unresolved placement/goal combinations.')


if __name__ == '__main__':
    main()
