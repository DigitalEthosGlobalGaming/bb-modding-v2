"""Exercise isolation and provenance with authored dummy files, not game data."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from prepare_ui_trial import prepare, verify_package
from toolkit import save, sha


class UiTrial(unittest.TestCase):
    def test_component_observation_is_explicit_and_independent_of_slot_tracing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cfg, stage, package, _ = self.fixture(root)
            with patch('prepare_ui_trial.native_menu_assets.build', return_value=b'generated fixture'):
                prepare(cfg, stage, package, root/'trial', components=True)
            env = json.loads((root/'trial/environment.json').read_text())
            self.assertEqual(env['BB_MODS_NATIVE_UI_COMPONENT_TRACE'], '1')
            self.assertEqual(env['BB_MODS_NATIVE_UI_TRACE'], '0')
            record = json.loads((root/'trial/trial.json').read_text())
            self.assertTrue(record['native_ui_component_trace'])
            self.assertFalse(record['native_rendering_verified'])

    def test_extra_mods_are_copied_into_the_trial_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cfg, stage, package, _ = self.fixture(root)
            mod = root/'menu'
            (mod/'target/release').mkdir(parents=True)
            (mod/'mod.toml').write_text('[mod]\nid = "bbmod.menu"\ndll = "bbmod_menu.dll"\nmanage = true\n')
            (mod/'target/release/bbmod_menu.dll').write_bytes(b'built dll')
            with patch('prepare_ui_trial.native_menu_assets.build', return_value=b'generated fixture'):
                prepare(cfg, stage, package, root/'trial', extra_mods=[mod])
            self.assertEqual((root/'trial/bbmods/bbmod.menu/bbmod_menu.dll').read_bytes(), b'built dll')
            self.assertIn('manage = true', (root/'trial/bbmods/bbmod.menu/mod.toml').read_text())
            with self.assertRaises(ValueError):
                (mod/'target/release/bbmod_menu.dll').unlink()
                prepare(cfg, stage, package, root/'trial2', extra_mods=[mod])

    def fixture(self, root):
        runtime, game, stage, package = [root/name for name in ('installed', 'game', 'stage', 'package')]
        files = {runtime/'run.py': b'print("authored fixture")',
                 runtime/'scripts/helper.py': b'# fixture',
                 runtime/'bin/extra.dll': b'original dependency',
                 runtime/'bbport.ini': b'output_res=1920x1080\ndebug_menu=1\n',
                 runtime/'input.ini': b'# controller settings',
                 runtime/'user/savedata/slot': b'original savedata',
                 game/'eboot.bin': b'authored fixture',
                 game/'dvdroot_ps4/menu/ingametop.gfx': b'authored movie fixture',
                 game/'dvdroot_ps4/menu/optionsetting.gfx': b'authored movie fixture',
                 stage/'bin/bb-probe.exe': b'new runtime',
                 package/'ui_examples.dll': b'example DLL',
                 package/'mod.toml': b'[mod]\nid="example.ui-examples"\n'}
        for p, contents in files.items():
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(contents)
        save(stage/'manifest.json', dict(files={'bin/bb-probe.exe': sha(stage/'bin/bb-probe.exe')}))
        save(package/'build-record.json', dict(mod_id='example.ui-examples', automated_tests_passed=True,
             files={n: sha(package/n) for n in ('ui_examples.dll', 'mod.toml')}))
        return dict(runtime_directory=str(runtime), game_assets=str(game/'dvdroot_ps4')), stage, package, files

    def test_copies_settings_and_saves_and_redirects_writes_to_trial(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cfg, stage, package, originals = self.fixture(root)
            out = root/'trial'
            with patch('prepare_ui_trial.native_menu_assets.build', return_value=b'generated fixture'):
                prepare(cfg, stage, package, out)
            for p, contents in originals.items():
                self.assertEqual(p.read_bytes(), contents)
            self.assertEqual((out/'user/savedata/slot').read_bytes(), b'original savedata')
            self.assertEqual((out/'bin/bb-probe.exe').read_bytes(), b'new runtime')
            self.assertEqual((out/'bin/extra.dll').read_bytes(), b'original dependency')
            self.assertIn('debug_menu=0', (out/'bbport.ini').read_text())
            self.assertIn('output_res=1920x1080', (out/'bbport.ini').read_text())
            env = json.loads((out/'environment.json').read_text())
            for key in ('BB_DATA_DIR', 'BB_USER_DIR', 'BB_GPU_USER_DIR', 'BB_CONFIG', 'BB_CODEMODS_DIR', 'BB_MODS_LOG'):
                self.assertTrue(Path(env[key]).is_relative_to(out))
            self.assertEqual(env['BB_ONLINE'], '0')
            self.assertEqual(env['BB_MODS_NATIVE_UI_TRACE'], '0')
            self.assertEqual(env['BB_MODS_NATIVE_UI_COMPONENT_TRACE'], '0')
            record = json.loads((out/'trial.json').read_text())
            self.assertFalse(record['launched'])
            for name, digest in record['files'].items():
                self.assertEqual(sha(out/name), digest)
            # Changing the copied save cannot modify the original save.
            (out/'user/savedata/slot').write_bytes(b'trial only')
            self.assertEqual((Path(cfg['runtime_directory'])/'user/savedata/slot').read_bytes(), b'original savedata')
            traced = root/'traced'
            with patch('prepare_ui_trial.native_menu_assets.build', return_value=b'generated fixture'):
                prepare(cfg, stage, package, traced, trace=True)
            self.assertEqual(json.loads((traced/'environment.json').read_text())['BB_MODS_NATIVE_UI_TRACE'], '1')
            self.assertTrue(json.loads((traced/'trial.json').read_text())['native_ui_trace'])

    def test_changed_build_and_untested_package_fail_before_creating_trial(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cfg, stage, package, _ = self.fixture(root)
            for folder, filename in ((stage, 'bin/bb-probe.exe'), (package, 'ui_examples.dll')):
                path = folder/filename; original = path.read_bytes(); path.write_bytes(b'changed')
                with self.assertRaisesRegex(ValueError, 'changed'):
                    prepare(cfg, stage, package, root/'trial')
                self.assertFalse((root/'trial').exists())
                path.write_bytes(original)
            manifest = package/'build-record.json'
            record = json.loads(manifest.read_text()); record['automated_tests_passed'] = False; save(manifest, record)
            with self.assertRaisesRegex(ValueError, 'test UI-Examples'):
                prepare(cfg, stage, package, root/'trial')

    def test_existing_protected_paths_and_manifest_escape_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cfg, stage, package, _ = self.fixture(root)
            for out in (root, Path(cfg['runtime_directory'])/'trial', stage/'trial', package/'trial'):
                with self.assertRaises(ValueError):
                    prepare(cfg, stage, package, out)
            existing = root/'existing'; existing.mkdir()
            with self.assertRaisesRegex(ValueError, 'existing trials'):
                prepare(cfg, stage, package, existing)
            save(stage/'manifest.json', dict(files={'../outside': 'bogus'}))
            with self.assertRaisesRegex(ValueError, 'escapes'):
                verify_package(stage, 'manifest.json')


if __name__ == '__main__':
    unittest.main()
