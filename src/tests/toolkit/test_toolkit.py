import importlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
import toolkit as t
import decompile_lua as d
import generate_profiles as profiles

class ToolkitTests(unittest.TestCase):
    def test_zip_traversal_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);archive=root/'bad.zip'
            with zipfile.ZipFile(archive,'w') as z:z.writestr('../outside','bad')
            with self.assertRaises(ValueError):t.extract_zip(archive,root/'out')
            self.assertFalse((root/'outside').exists())

    def test_absolute_drive_and_parent_paths_rejected(self):
        for path in ['../escape','C:/escape','/escape','a\\b']:
            with self.assertRaises(ValueError):t.child(Path.cwd(),path)

    def test_invalid_sfo_and_wrong_root_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'param.sfo';p.write_bytes(b'not sfo')
            with self.assertRaises(ValueError):t.sfo(p)
            with self.assertRaisesRegex(ValueError,'containing CUSA03173'):t.validate(Path(temp),Path(temp))

    def test_decompile_resume_preserves_edits(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'a.lua';dest=root/'out/a.lua';source.write_bytes(b'return 1\n')
            record=d.decompile(source,dest,root/'unused.dll',None)
            self.assertEqual(d.decompile(source,dest,root/'unused.dll',record),record)
            dest.write_bytes(b'local edit')
            with self.assertRaises(ValueError):d.decompile(source,dest,root/'unused.dll',record)
            self.assertEqual(dest.read_bytes(),b'local edit');self.assertEqual(source.read_bytes(),b'return 1\n')

    def test_profile_input_bounds(self):
        for raw in [b'',b'\x7fELF',bytes.fromhex('4f153d1d')+bytes(28)]:
            with self.assertRaises((ValueError,struct.error)):profiles.segments(raw)

    def test_rollback_refuses_changed_installation(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);runtime=root/'runtime';backup=root/'.local/backups/test';runtime.mkdir();backup.mkdir(parents=True)
            dest=runtime/'mod.dll';dest.write_bytes(b'new')
            saved=backup/'mod.dll';saved.write_bytes(b'old')
            t.save(backup/'manifest.json',{'runtime':str(runtime),'files':[{'path':'mod.dll','before':t.sha(saved),'after':t.sha(dest)}]})
            dest.write_bytes(b'user change')
            with patch.object(t,'ROOT',root),patch.object(t,'require_game_closed'),self.assertRaisesRegex(ValueError,'Installed file changed'):t.rollback(backup)
            self.assertEqual(dest.read_bytes(),b'user change')
            dest.write_bytes(b'new')
            with patch.object(t,'ROOT',root),patch.object(t,'require_game_closed'):t.rollback(backup)
            self.assertEqual(dest.read_bytes(),b'old')

if __name__=='__main__':unittest.main()
