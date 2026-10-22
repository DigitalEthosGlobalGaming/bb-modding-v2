"""Synthetic checks for evidence extraction and preservation; no game behavior claims."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import annotate_scripts as a


class AnnotationTests(unittest.TestCase):
    def test_comments_strings_and_nested_arguments_do_not_invent_facts(self):
        source = '''-- function Fake() ai:GetNumber(99)
local text = [=[function FakeLong()]=]
local quoted = "ai:GetNumber(88)"
function Caution_Common10100_Logic(ai)
  ai:HasSpecialEffectId(TARGET_SELF, 5410)
  ai:SetNumber(7, other(1, 2))
  ai:GetNumber(dynamic_slot)
  ai:HasSpecialEffectId(target(1, 2), 0x20)
end
local function helper() end
'''
        facts = a.analyze(source)
        self.assertEqual([f['name'] for f in facts['functions']], ['Caution_Common10100_Logic', 'helper'])
        self.assertEqual(facts['functions'][0]['line'], 4)
        self.assertTrue(facts['functions'][1]['local'])
        self.assertEqual([(r['kind'], r['value']) for r in facts['id_references']],
                         [('special_effect', 5410), ('ai_number_slot', 7), ('special_effect', 32)])
        self.assertFalse(any('Fake' in c['name'] for c in facts['calls']))

    def test_goal_names_and_binary_bounds(self):
        name = 'common10000_Logic'.encode('utf-16-le') + b'\0\0'
        data = b'LUAI\x01\0\0\0' + struct.pack('<II', 1, 0)
        data += struct.pack('<IBBHQQ', 10000, 0, 1, 0, 40, 0) + name
        self.assertEqual(a.goal_entries(data)[0], {'goal_id': 10000, 'role': 'logic', 'function': 'common10000_Logic'})
        with self.assertRaises(ValueError):a.goal_entries(data[:39])
        with self.assertRaises(ValueError):a.goal_entries(data[:-2])

    def test_duplicate_think_rows_keep_alternatives(self):
        data = bytearray(112 + 296)
        data[12:30] = b'NPC_THINK_PARAM_ST'
        data[44:48] = bytes.fromhex('00040700')
        struct.pack_into('<H', data, 10, 2)
        for i, goal in enumerate([10000, 250100]):
            struct.pack_into('<iIQQ', data, 64 + i * 24, 250100, 0, 112 + i * 148, 0)
            struct.pack_into('<ii', data, 112 + i * 148, goal, goal)
        self.assertEqual(a.think_entries(data)[250100], {(10000, 10000), (250100, 250100)})

    def test_generation_resume_duplicates_provenance_and_preserve_edits(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, raw, out = root / 'source', root / 'raw', root / 'out'
            raw.mkdir()
            paths = ['dvdroot_ps4/script/a/010100_logic.lua', 'dvdroot_ps4/script/b/010100_logic.lua']
            body = b'function Caution_Common10100_Logic(f1_arg0)\r\n local f1_local0 = f1_arg0:GetNumber(7)\r\nend\r\n'
            report = {'complete': True, 'files': {}}
            for rel in paths:
                p = source / rel
                p.parent.mkdir(parents=True)
                p.write_bytes(body)
                report['files'][rel.removeprefix('dvdroot_ps4/')] = {'status': 'decompiled', 'output_sha256': a.sha(body)}
            (source / 'report.json').write_text(json.dumps(report))
            self.assertEqual(a.generate(source, raw, out)['scripts'], 2)
            target = out / paths[0]
            self.assertTrue(target.read_bytes().endswith(body))
            readable = (out / ('readable/' + paths[0])).read_bytes()
            self.assertIn(b'local ai_number_7 = ai:GetNumber(7)\r\n', readable)
            metadata = json.loads((out / (paths[0] + '.json')).read_text())
            self.assertEqual(metadata['identical_sources'], [paths[1]])
            self.assertEqual(metadata['label'], 'Caution Common10100 Logic')
            self.assertEqual(len(metadata['readability']['renames']), 2)
            before = target.stat().st_mtime_ns
            self.assertEqual(a.generate(source, raw, out)['cached_analyses'], 2)
            self.assertEqual(target.stat().st_mtime_ns, before)
            # A changed input with stale provenance must not enter the current index.
            (source / paths[1]).write_bytes(body + b'-- edit\n')
            self.assertEqual(a.generate(source, raw, out)['skipped'], 1)
            target.write_bytes(b'manual edit')
            with self.assertRaisesRegex(ValueError, 'preserved'):a.generate(source, raw, out)
            self.assertEqual(target.read_bytes(), b'manual edit')
            self.assertEqual((source / paths[0]).read_bytes(), body)

    def test_overlapping_output_and_stale_placements_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, raw = root / 'source', root / 'raw'
            source.mkdir();raw.mkdir()
            with self.assertRaises(ValueError):a.generate(source, raw, source / 'annotated')
            msb = raw / 'dvdroot_ps4/map/mapstudio/m01.msb'
            msb.parent.mkdir(parents=True)
            msb.write_bytes(b'changed')
            placements = root / 'placements.json'
            placements.write_text(json.dumps({'sources': [{'path': 'm01.msb', 'sha256': a.sha(b'original')}], 'placements': []}))
            with self.assertRaisesRegex(ValueError, 'Stale'):a.generate(source, raw, root / 'out', placements)

    def test_verified_registration_think_placement_and_action_joins(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, raw, out = root / 'source', root / 'raw', root / 'out'
            script = source / 'dvdroot_ps4/script/m01.luabnd.unpacked/010100_logic.lua'
            script.parent.mkdir(parents=True)
            script.write_text('function common10000_Logic(ai) end\n')
            action = source / 'dvdroot_ps4/action/script/c1234.hks'
            action.parent.mkdir(parents=True)
            action.write_text('function Update() end\n')
            info = raw / 'dvdroot_ps4/script/m01.luabnd.unpacked/m01.luainfo'
            info.parent.mkdir(parents=True)
            info.write_bytes(b'LUAI\x01\0\0\0' + struct.pack('<II', 1, 0) +
                             struct.pack('<IBBHQQ', 10000, 0, 1, 0, 40, 0) +
                             'common10000_Logic'.encode('utf-16-le') + b'\0\0')
            param = raw / 'dvdroot_ps4/param/gameparam/gameparam.parambnd.unpacked/NpcThinkParam.param'
            param.parent.mkdir(parents=True)
            data = bytearray(88 + 148)
            data[12:30] = b'NPC_THINK_PARAM_ST'
            data[44:48] = bytes.fromhex('00040700')
            struct.pack_into('<H', data, 10, 1)
            struct.pack_into('<iIQQ', data, 64, 123000, 0, 88, 0)
            struct.pack_into('<ii', data, 88, 10000, -1)
            param.write_bytes(data)
            msb = raw / 'dvdroot_ps4/map/mapstudio/m01.msb'
            msb.parent.mkdir(parents=True)
            msb.write_bytes(b'synthetic map')
            placements = root / 'placements.json'
            placement = {'map': 'm01', 'model': 'c1234', 'npc': 123001, 'think': 123000}
            placements.write_text(json.dumps({'sources': [{'path': 'm01.msb', 'sha256': a.sha(msb.read_bytes())}],
                                              'placements': [placement]}))
            summary = a.generate(source, raw, out, placements)
            self.assertEqual(summary['registered_scripts'], 1)
            self.assertEqual(summary['placement_scripts'], 2)
            index = json.loads((out / 'index.json').read_text())
            ai = next(r for r in index['scripts'] if r['goals'])
            self.assertEqual(ai['goals'][0]['think_ids'], [123000])
            self.assertEqual(ai['placements'], [placement])
            self.assertTrue(all(r['placements'] == [placement] for r in index['scripts']))


if __name__ == '__main__':unittest.main()
