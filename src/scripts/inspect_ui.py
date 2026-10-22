"""Read-only GFX structure inspection. Game-derived reports belong in .local.

This reads SWF-compatible records; it does not decompile ActionScript, evaluate
timelines, render controls or establish native binding semantics.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
VERSION = 1
DEFAULT_MOVIES = ('optionsetting', 'slider', 'commandlist', 'itemquantityselect',
                  'inventory', 'ingametop', 'fe')
TAG_NAMES = {0: 'End', 1: 'ShowFrame', 2: 'DefineShape', 9: 'SetBackgroundColor',
             12: 'DoAction', 22: 'DefineShape2', 26: 'PlaceObject2',
             28: 'RemoveObject2', 32: 'DefineShape3', 37: 'DefineEditText',
             39: 'DefineSprite', 43: 'FrameLabel', 56: 'ExportAssets',
             57: 'ImportAssets', 59: 'DoInitAction', 69: 'FileAttributes',
             70: 'PlaceObject3', 71: 'ImportAssets2', 76: 'SymbolClass',
             82: 'DoABC', 83: 'DefineShape4', 86: 'DefineSceneAndFrameLabelData',
             1000: 'ExporterInfo', 1009: 'ExternalImage'}


class InvalidMovie(ValueError):
    pass


class Cursor:
    def __init__(self, data, start=0, end=None):
        self.data, self.pos = data, start
        self.end = len(data) if end is None else end

    def take(self, n):
        if n < 0 or self.pos + n > self.end:
            raise InvalidMovie(f'truncated record at byte {self.pos}')
        result = self.data[self.pos:self.pos+n]
        self.pos += n
        return result

    def number(self, n):
        return int.from_bytes(self.take(n), 'little')

    def string(self):
        end = self.data.find(b'\0', self.pos, self.end)
        if end < 0:
            raise InvalidMovie(f'unterminated string at byte {self.pos}')
        value = self.take(end-self.pos).decode('utf-8', errors='replace')
        self.take(1)
        return value


class Bits:
    def __init__(self, cursor):
        self.cursor, self.value, self.left = cursor, 0, 0

    def unsigned(self, count):
        value = 0
        for _ in range(count):
            if not self.left:
                self.value, self.left = self.cursor.number(1), 8
            self.left -= 1
            value = (value << 1) | ((self.value >> self.left) & 1)
        return value

    def signed(self, count):
        value = self.unsigned(count)
        return value - (1 << count) if count and value & (1 << (count-1)) else value


def matrix(cursor):
    b = Bits(cursor)
    scale = [1.0, 1.0]
    rotate = [0.0, 0.0]
    if b.unsigned(1):
        n = b.unsigned(5)
        scale = [b.signed(n)/65536 for _ in range(2)]
    if b.unsigned(1):
        n = b.unsigned(5)
        rotate = [b.signed(n)/65536 for _ in range(2)]
    n = b.unsigned(5)
    translate = [b.signed(n) for _ in range(2)]
    return dict(scale=scale, rotate_skew=rotate, translate_twips=translate)


def color_transform(cursor):
    b = Bits(cursor)
    add, mult, n = b.unsigned(1), b.unsigned(1), b.unsigned(4)
    multipliers = [b.signed(n)/256 for _ in range(4)] if mult else [1.0]*4
    offsets = [b.signed(n) for _ in range(4)] if add else [0]*4
    return dict(rgba_multiply=multipliers, rgba_add=offsets)


def placement(cursor, code):
    flags = cursor.number(1)
    flags2 = cursor.number(1) if code == 70 else 0
    result = dict(depth=cursor.number(2), move=bool(flags & 1), flags=flags,
                  flags2=flags2)
    if flags2 & 8:
        result['class_name'] = cursor.string()
    if flags & 2:
        result['character_id'] = cursor.number(2)
    if flags & 4:
        result['matrix'] = matrix(cursor)
    if flags & 8:
        result['color_transform'] = color_transform(cursor)
    if flags & 16:
        result['ratio'] = cursor.number(2)
    if flags & 32:
        result['name'] = cursor.string()
    if flags & 64:
        result['clip_depth'] = cursor.number(2)
    # Filters/blend/cache/visibility extensions are retained as an opaque tail.
    # Never guess their order or mistake their bytes for an opacity value.
    if cursor.pos < cursor.end:
        result['unparsed_tail_bytes'] = cursor.end-cursor.pos
    return result


def inspect(data):
    if len(data) > 64*1024*1024:
        raise InvalidMovie('movie exceeds the 64 MiB inspection budget')
    c = Cursor(data)
    signature = c.take(3)
    if signature not in (b'GFX', b'FWS'):
        raise InvalidMovie('expected uncompressed GFX/FWS; compressed files need separate decoding')
    version, length = c.number(1), c.number(4)
    if length != len(data):
        raise InvalidMovie('declared length differs from actual file length')
    bits = Bits(c)
    n = bits.unsigned(5)
    bounds = [bits.signed(n) for _ in range(4)]
    rate, frames = c.number(2)/256, c.number(2)
    report = dict(schema_version=VERSION, sha256=hashlib.sha256(data).hexdigest(),
                  header=dict(signature=signature.decode(), version=version,
                              bytes=length, bounds_twips=bounds,
                              frame_rate=rate, frame_count=frames),
                  timelines=[], symbols=[], imports=[], external_images=[],
                  bytecode=[], tag_counts={}, warnings=[],
                  limitations=['Static records only; no playback, rendering or native binding verification.',
                               'Placement updates are records, not resolved per-frame object state.',
                               'Unknown tags and placement tails are reported, not interpreted.',
                               'Scripts require a separate decompiler; bytecode is not recovered source.'])
    counts = Counter()
    records = 0
    sprite_ids = set()

    def timeline(cur, character, depth, declared_frames):
        nonlocal records
        if depth > 32:
            raise InvalidMovie('sprite nesting exceeds 32 levels')
        result = dict(character_id=character, declared_frames=declared_frames,
                      placements=[], removals=[], labels=[])
        report['timelines'].append(result)
        frame = 1
        while cur.pos < cur.end:
            records += 1
            if records > 250000:
                raise InvalidMovie('tag inspection budget exceeded')
            offset = cur.pos
            header = cur.number(2)
            code, size = header >> 6, header & 63
            if size == 63:
                size = cur.number(4)
            start = cur.pos
            cur.take(size)  # Validate the entire record before interpreting it.
            body = Cursor(data, start, start+size)
            counts[code] += 1
            if code == 0:
                if size or cur.pos != cur.end:
                    raise InvalidMovie(f'invalid End record at byte {offset}')
                if frame-1 != declared_frames:
                    report['warnings'].append(f'timeline {character}: declared/observed frame count differs')
                return
            if code == 1:
                if size:
                    raise InvalidMovie('ShowFrame must have an empty payload')
                frame += 1
            elif code == 39:
                sprite, count = body.number(2), body.number(2)
                if sprite in sprite_ids:
                    raise InvalidMovie(f'duplicate sprite ID {sprite}')
                sprite_ids.add(sprite)
                timeline(body, sprite, depth+1, count)
            elif code in (26, 70):
                result['placements'].append(dict(offset=offset, frame=frame,
                                                 **placement(body, code)))
            elif code == 28:
                result['removals'].append(dict(offset=offset, frame=frame, depth=body.number(2)))
            elif code == 43:
                result['labels'].append(dict(frame=frame, name=body.string()))
            elif code in (56, 76):
                for _ in range(body.number(2)):
                    report['symbols'].append(dict(tag=TAG_NAMES[code], character_id=body.number(2), name=body.string()))
            elif code in (57, 71):
                url = body.string()
                if code == 71:
                    body.take(2)
                entries = [dict(character_id=body.number(2), name=body.string())
                           for _ in range(body.number(2))]
                report['imports'].append(dict(url=url, entries=entries))
            elif code == 1009:
                # Scaleform's ExternalImage payload, including resource names.
                image = dict(offset=offset, image_id=body.number(2), id_type=body.number(2),
                             bitmap_format=body.number(2), width=body.number(2),
                             height=body.number(2))
                image['export_name'] = body.take(body.number(1)).decode('utf-8', errors='replace')
                image['file_name'] = body.take(body.number(1)).decode('utf-8', errors='replace')
                if body.pos != body.end:
                    image['unparsed_tail_bytes'] = body.end-body.pos
                report['external_images'].append(image)
            elif code in (12, 59, 82):
                report['bytecode'].append(dict(tag=TAG_NAMES[code], offset=offset,
                                              bytes=size, timeline=character, frame=frame))
        raise InvalidMovie(f'timeline {character} has no End record')

    timeline(c, None, 0, frames)
    report['tag_counts'] = {f'{code}:{TAG_NAMES.get(code, "Unknown")}': count
                            for code, count in sorted(counts.items())}
    return report


def export(game, out, movies):
    game, out = Path(game).resolve(), Path(out).resolve()
    if game == out or game in out.parents or out in game.parents:
        raise ValueError('report output must be outside the original game tree')
    if out.exists():
        raise ValueError('choose a new output directory; existing research is preserved')
    reports = []
    for name in dict.fromkeys(movies):
        if not name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in name):
            raise ValueError('movie names must be basenames without extensions')
        source = game / 'dvdroot_ps4/menu' / f'{name}.gfx'
        if source.stat().st_size > 64*1024*1024:
            raise InvalidMovie('movie exceeds the 64 MiB inspection budget')
        report = inspect(source.read_bytes())
        report['source'] = str(source)
        report['movie'] = name
        reports.append(report)
    # Parse every input first. A failed inspection leaves no partial report set.
    out.mkdir(parents=True)
    lines = ['movie\ttimeline\tframe\tname\tcharacter_id\tdepth\talpha_multiplier\tclip_depth']
    for report in reports:
        (out / (report['movie']+'.json')).write_text(json.dumps(report, indent=2, ensure_ascii=True)+'\n', encoding='utf-8')
        for t in report['timelines']:
            for p in t['placements']:
                alpha = p.get('color_transform', {}).get('rgba_multiply', ['', '', '', ''])[3]
                cells = [report['movie'], t['character_id'], p['frame'], p.get('name', ''),
                         p.get('character_id', ''), p['depth'], alpha, p.get('clip_depth', '')]
                lines.append('\t'.join(str(v).replace('\t', '\\t').replace('\r', '\\r').replace('\n', '\\n') for v in cells))
    (out / 'placements.tsv').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    manifest = dict(schema_version=VERSION, tool='inspect_ui.py',
                    tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    created_utc=datetime.now(timezone.utc).isoformat(),
                    evidence='static inspection; rendering and native semantics unverified',
                    movies=[dict(movie=r['movie'], source=r['source'], sha256=r['sha256']) for r in reports])
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    return reports


def install_decompiler():
    """Install only on explicit request; verify all installed tool files on reuse."""
    from toolkit import LOCK, download, extract_zip, child, save, sha
    spec = LOCK['ui_decompiler']
    target = ROOT/'tools/JPEXS'/spec['version']
    stamp = target/'toolkit-installed.json'
    if stamp.is_file():
        installed = json.loads(stamp.read_text())
        if installed.get('version') != spec['version'] or installed.get('archive_sha256') != spec['sha256']:
            raise ValueError('JPEXS pin changed; existing tool files preserved')
        for relative, expected in installed['files'].items():
            if sha(child(target, relative)) != expected:
                raise ValueError(f'Installed JPEXS file changed; preserved: {relative}')
        if 'ffdec.jar' not in installed['files']:
            raise ValueError('JPEXS manifest has no ffdec.jar')
    else:
        if target.exists() and any(target.iterdir()):
            raise ValueError('Existing JPEXS directory has no provenance stamp; preserved')
        archive = ROOT/'tools/downloads'/f'ffdec_{spec["version"]}.zip'
        download(spec['url'], archive, spec['sha256'])
        extract_zip(archive, target)
        files = {p.relative_to(target).as_posix(): sha(p) for p in target.rglob('*') if p.is_file()}
        if 'ffdec.jar' not in files:
            raise ValueError('JPEXS archive has no ffdec.jar')
        save(stamp, dict(version=spec['version'], archive_sha256=spec['sha256'], files=files))
    return target/'ffdec.jar', spec


def decompile(reports, out):
    """Export private readable references, keeping static/rendering evidence distinct."""
    from toolkit import sha, save
    java = shutil.which('java')
    if not java:
        raise ValueError('JPEXS script/XML export needs Java on PATH')
    jar, spec = install_decompiler()
    prefix = [java, '-Djava.awt.headless=true', f'-Duser.home={out / "tool-user"}',
              '-jar', str(jar), '-onerror', 'abort']
    record = dict(tool=spec, jar_sha256=sha(jar), completed=False,
                  native_rendering_verified=False, movies=[])
    try:
        for report in reports:
            source = Path(report['source'])
            if sha(source) != report['sha256']:
                raise ValueError('Movie changed after structural inspection')
            folder = out/'decompiled'/report['movie']
            folder.mkdir(parents=True, exist_ok=False)
            commands = [prefix+['-export', 'script,text,symbolClass', str(folder), str(source)],
                        prefix+['-swf2xml', str(source), str(folder/(report['movie']+'.xml'))]]
            with (folder/'export.log').open('w', encoding='utf-8') as log:
                for command in commands:
                    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
            if sha(source) != report['sha256']:
                raise ValueError('Original movie changed during export')
            if not (folder/(report['movie']+'.xml')).is_file():
                raise ValueError('JPEXS did not produce the requested XML')
            record['movies'].append(dict(movie=report['movie'], source_sha256=report['sha256'],
                                         commands=commands, files={p.relative_to(folder).as_posix(): sha(p)
                                          for p in folder.rglob('*') if p.is_file()}))
        record['completed'] = True
    finally:
        save(out/'decompile.json', record)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, help='Game root containing dvdroot_ps4; defaults to configured game')
    parser.add_argument('--out', type=Path, help='New directory under .local/ui-research')
    parser.add_argument('--movie', action='append', help='Movie basename without .gfx; repeat to select several')
    parser.add_argument('--decompile', action='store_true', help='Install pinned JPEXS if needed; export private scripts/text/symbols/XML (requires Java)')
    args = parser.parse_args()
    game = args.game
    if game is None:
        game = Path(json.loads((ROOT / '.local/workspace.json').read_text())['game_assets']).parent
    private = (ROOT / '.local/ui-research').resolve()
    out = args.out or private / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    if private not in out.resolve().parents:
        raise ValueError('reports must stay in a child of .local/ui-research (private game-derived output)')
    reports = export(game, out, args.movie or DEFAULT_MOVIES)
    if args.decompile:
        decompile(reports, out.resolve())
    for report in reports:
        print(f"{report['movie']}: {len(report['timelines'])} timelines, {len(report['bytecode'])} bytecode records, {len(report['warnings'])} warnings")
    print(f'Static reports: {out.resolve()}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(f'ERROR: {error}')
