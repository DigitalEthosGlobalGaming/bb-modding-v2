import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('script_links', Path(__file__).resolve().parents[2] / 'scripts/generate_observer_script_links.py')
links = importlib.util.module_from_spec(spec)
spec.loader.exec_module(links)


class ScriptLinkTests(unittest.TestCase):
    def test_duplicate_think_ids_preserve_alternatives_and_bounds(self):
        raw = bytearray(64 + 48 + 296)
        raw[12:30] = b'NPC_THINK_PARAM_ST'
        raw[44:48] = bytes.fromhex('00040700')
        struct.pack_into('<H',raw,10,2)
        for i,goal in enumerate([10000,250100]):
            struct.pack_into('<iIQQ',raw,64+24*i,250100,0,112+148*i,0)
            struct.pack_into('<ii',raw,112+148*i,goal,goal)
        self.assertEqual(links.think_rows(raw)[250100], {(10000,10000),(250100,250100)})
        struct.pack_into('<Q',raw,72,len(raw))
        with self.assertRaises(ValueError):links.think_rows(raw)

    def test_luainfo_rejects_truncated_and_invalid_strings(self):
        with self.assertRaises(ValueError):links.goals(b'LUAI\x01\0\0\0'+struct.pack('<II',1,0))
        with self.assertRaises(ValueError):links.wide(b'\0'*20,19)

    @unittest.skipUnless(__import__("os").environ.get("BB_OBSERVER_MOD"), "Set BB_OBSERVER_MOD for private reference validation")
    def test_real_npc_think_goal_namespaces_and_functions(self):
        catalog,provenance=links.catalog(mod=Path(__import__("os").environ["BB_OBSERVER_MOD"]))
        matches=[l for l in catalog if l[0]==112401]
        self.assertTrue(any(l[2]=='Battle' and l[5]==112400 and l[6]==112000 and
                            l[3].endswith('/112000_battle.lua') and l[7]=='DrownedBeast112000Battle_Activate'
                            for l in matches))
        self.assertTrue(any(l[2]=='Logic' and l[7]=='common10000_Logic' for l in matches))
        self.assertIn(250100,provenance['duplicate_think_ids'])


if __name__ == '__main__':unittest.main()
