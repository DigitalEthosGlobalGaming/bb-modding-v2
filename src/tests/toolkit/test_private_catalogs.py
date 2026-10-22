"""Private carry-forward catalogs fail closed on changed game or reference bytes."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
import generate_catalogs as catalogs

class PrivateCatalogTests(unittest.TestCase):
    def fixture(self, root):
        game=root/'game'; directory=root/'private'; directory.mkdir()
        paths=['param/gameparam/gameparam.parambnd.dcx','msg/engus/item.msgbnd.dcx',
               'action/statenameid.txt','action/eventnameid.txt','action/variablenameid.txt']
        sources=[]
        for name in paths:
            path=game/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
            sources.append({'path':name,'sha256':hashlib.sha256(b'fixture').hexdigest()})
        common={'title_id':'CUSA03173','game_version':'1.09'}
        payloads={'characters.json':json.dumps(dict(common,sources=sources[:2],characters=[])),
                  'character-references.json':json.dumps(dict(common,action_sources=sources[2:])),
                  'character_definitions_generated.rs':'// fixture',
                  'character_references_generated.rs':'// fixture'}
        for name,text in payloads.items():(directory/name).write_text(text)
        manifest=dict(common,schema_version=1,outputs=list(payloads),files={name:hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in payloads})
        (directory/'manifest.json').write_text(json.dumps(manifest))
        return game,directory

    def test_changed_reference_payload_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            game,directory=self.fixture(Path(temp))
            self.assertIsNotNone(catalogs.private_catalogs(game,directory))
            (directory/'character_definitions_generated.rs').write_text('// edited')
            with self.assertRaisesRegex(ValueError,'input changed'):
                catalogs.private_catalogs(game,directory)

    def test_changed_game_source_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            game,directory=self.fixture(Path(temp))
            (game/'action/statenameid.txt').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'game source mismatch'):
                catalogs.private_catalogs(game,directory)

    def test_wrong_version_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            game,directory=self.fixture(Path(temp))
            path=directory/'manifest.json';manifest=json.loads(path.read_text());manifest['game_version']='1.00';path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'version mismatch'):
                catalogs.private_catalogs(game,directory)
