"""Authored synthetic fixtures for the bounded, read-only UI inspector."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import inspect_ui as ui
import hud_layout as swf


def movie(records, frames=1):
    # Authored 100x100 pixel stage; no game-derived payloads.
    w = swf.Writer()
    w.unsigned(13, 5)
    for v in (0, 2000, 0, 2000):
        w.unsigned(v, 13)
    body = w.bytes()+struct.pack('<HH', 30*256, frames)+records
    return b'GFX\x0b'+struct.pack('<I', len(body)+8)+body


END = swf.tag(1, b'')+swf.tag(0, b'')


class Inspection(unittest.TestCase):
    def test_decompiler_records_success_failure_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); game = root/'game'; menu = game/'dvdroot_ps4/menu'
            menu.mkdir(parents=True); source = menu/'demo.gfx'; source.write_bytes(movie(END))
            jar = root/'ffdec.jar'; jar.write_bytes(b'authored tool fixture')
            out = root/'reports'; reports = ui.export(game, out, ['demo'])
            def run(command, **kwargs):
                if '-swf2xml' in command:
                    Path(command[-1]).write_text('<swf/>')
            with patch.object(ui.shutil, 'which', return_value='java'), \
                 patch.object(ui, 'install_decompiler', return_value=(jar, {'version': 'fixture'})), \
                 patch.object(ui.subprocess, 'run', side_effect=run) as process:
                ui.decompile(reports, out)
                self.assertEqual(process.call_count, 2)
                self.assertIn('-Djava.awt.headless=true', process.call_args.args[0])
            record = json.loads((out/'decompile.json').read_text())
            self.assertTrue(record['completed'])
            self.assertFalse(record['native_rendering_verified'])
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), reports[0]['sha256'])
            failed = root/'failed'; failed.mkdir()
            with patch.object(ui.shutil, 'which', return_value='java'), \
                 patch.object(ui, 'install_decompiler', return_value=(jar, {'version': 'fixture'})), \
                 patch.object(ui.subprocess, 'run', side_effect=ui.subprocess.TimeoutExpired('java', 120)):
                with self.assertRaises(ui.subprocess.TimeoutExpired):
                    ui.decompile(reports, failed)
            self.assertFalse(json.loads((failed/'decompile.json').read_text())['completed'])

    def test_missing_java_does_not_download_a_tool(self):
        with patch.object(ui.shutil, 'which', return_value=None), \
             patch.object(ui, 'install_decompiler') as install:
            with self.assertRaisesRegex(ValueError, 'Java'):
                ui.decompile([], Path('unused'))
            install.assert_not_called()

    def test_named_placement_alpha_mask_and_frame_update(self):
        color = swf.Writer()
        color.unsigned(0, 1); color.unsigned(1, 1); color.unsigned(10, 4)
        for value in (256, 256, 256, 128):
            color.unsigned(value, 10)
        first = bytes([2|4|8|32|64])+struct.pack('<HH', 1, 7)
        first += swf.write_matrix((None, None, (40, -20)))+color.bytes()+b'Example\0'+struct.pack('<H', 3)
        records = swf.tag(26, first)+swf.tag(43, b'Idle\0')+swf.tag(1, b'')
        # A frame update has no character/name. Preserve it as an update record.
        records += swf.tag(26, bytes([1|4])+struct.pack('<H', 1)+swf.write_matrix((None, None, (80, 0))))+END
        data = movie(records, frames=2)
        r = ui.inspect(data)
        t = r['timelines'][0]
        a, b = t['placements']
        self.assertEqual(a['name'], 'Example')
        self.assertEqual(a['matrix']['translate_twips'], [40, -20])
        self.assertEqual(a['color_transform']['rgba_multiply'], [1, 1, 1, 0.5])
        self.assertEqual(a['clip_depth'], 3)
        self.assertEqual(b['frame'], 2)
        self.assertNotIn('character_id', b)
        self.assertEqual(t['labels'], [dict(frame=1, name='Idle')])
        self.assertEqual(r['sha256'], hashlib.sha256(data).hexdigest())

    def test_nested_timeline_imports_symbols_and_opaque_script(self):
        records = swf.tag(39, struct.pack('<HH', 7, 1)+END)
        records += swf.tag(71, b'font.swf\0\x01\x00'+struct.pack('<HH', 1, 4)+b'FontName\0')
        records += swf.tag(76, struct.pack('<HH', 1, 7)+b'Example\0')
        records += swf.tag(82, b'\x00'*8)+swf.tag(999, b'unknown')+END
        r = ui.inspect(movie(records))
        self.assertEqual(r['timelines'][1]['character_id'], 7)
        self.assertEqual(r['imports'][0]['url'], 'font.swf')
        self.assertEqual(r['symbols'][0]['name'], 'Example')
        self.assertEqual(r['bytecode'][0]['bytes'], 8)
        self.assertEqual(r['tag_counts']['999:Unknown'], 1)

    def test_external_image_identity_and_place3_image_flag(self):
        image = struct.pack('<HHHHH', 4, 0, 13, 32, 64)+b'\x03Art\x07art.tga'
        # GFX image-flag placement without a class-name flag has no class string.
        placed = b'\x06\x10'+struct.pack('<HH', 1, 4)+swf.write_matrix((None, None, (0, 0)))
        r = ui.inspect(movie(swf.tag(1009, image)+swf.tag(70, placed)+END))
        self.assertEqual(r['external_images'][0]['file_name'], 'art.tga')
        self.assertEqual(r['external_images'][0]['height'], 64)
        self.assertEqual(r['timelines'][0]['placements'][0]['character_id'], 4)

    def test_truncated_malformed_and_compressed_inputs_are_rejected(self):
        data = movie(END)
        for n in range(len(data)):
            with self.assertRaises(ui.InvalidMovie):
                ui.inspect(data[:n])
        for records in [swf.tag(26, b'\x20\x01\x00name'),
                        swf.tag(39, struct.pack('<HH', 7, 1)),
                        struct.pack('<HI', (39 << 6)|63, 0xffffffff),
                        swf.tag(0, b'')+END]:
            with self.assertRaises(ui.InvalidMovie):
                ui.inspect(movie(records))
        with self.assertRaises(ui.InvalidMovie):
            ui.inspect(b'CFX'+data[3:])

    def test_depth_budget(self):
        records = END
        for i in range(34):
            records = swf.tag(39, struct.pack('<HH', i+1, 1)+records)+END
        with self.assertRaisesRegex(ui.InvalidMovie, 'nesting'):
            ui.inspect(movie(records))

    def test_export_preserves_inputs_existing_reports_and_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); game = root/'game'; menu = game/'dvdroot_ps4/menu'
            menu.mkdir(parents=True); source = menu/'demo.gfx'; data = movie(END)
            source.write_bytes(data)
            out = root/'reports'
            ui.export(game, out, ['demo'])
            self.assertEqual(source.read_bytes(), data)
            manifest = json.loads((out/'manifest.json').read_text())
            self.assertEqual(manifest['movies'][0]['sha256'], hashlib.sha256(data).hexdigest())
            for target, names in [(out, ['demo']), (root/'bad', ['../demo']),
                                  (game/'reports', ['demo']), (root, ['demo'])]:
                with self.assertRaises(ValueError):
                    ui.export(game, target, names)
            source.write_bytes(b'broken')
            with self.assertRaises(ui.InvalidMovie):
                ui.export(game, root/'failed', ['demo'])
            self.assertFalse((root/'failed').exists())


if __name__ == '__main__':
    unittest.main()
