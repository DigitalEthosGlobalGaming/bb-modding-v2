"""Check native movie dependencies and the independently clipped fourth icon."""
import struct
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import native_menu_assets as assets

swf = assets.swf
ROOT = Path(__file__).resolve().parents[3]


class NativeAssets(unittest.TestCase):
    @unittest.skipUnless(__import__("os").environ.get("BB_GAME_ROOT"), "Set BB_GAME_ROOT for private game asset validation")
    def test_stock_top_capacity_matches_live_adapter(self):
        from inspect_ui import inspect
        root = Path(__import__('os').environ['BB_GAME_ROOT'])
        source = root / 'dvdroot_ps4/menu/optionsetting.gfx'
        original = source.read_bytes()
        report = inspect(original)
        timelines = {t['character_id']: t for t in report['timelines']}
        top = next(p['character_id'] for p in report['timelines'][0]['placements'] if p.get('name') == 'Top')
        names = {p.get('name') for p in timelines[top]['placements'] if p['frame'] == 1}
        rows = {name for name in names if name and name.startswith('Item_')}
        self.assertEqual(rows, {f'Item_{i}_0' for i in range(5)})
        self.assertIn('#define MH_MENU_VISIBLE_ROWS 5', (ROOT / 'src/host/src/native_menu.h').read_text())
        self.assertEqual(source.read_bytes(), original)

    @unittest.skipUnless(__import__("os").environ.get("BB_GAME_ROOT"), "Set BB_GAME_ROOT for private game asset validation")
    def test_stock_art_preserved_and_fourth_icon_clipped(self):
        original = (Path(__import__('os').environ['BB_GAME_ROOT']) / 'dvdroot_ps4/menu/ingametop.gfx').read_bytes()
        changed = assets.build(original)
        _, before, _ = swf.definitions(original)
        _, after, _ = swf.definitions(changed)
        # Every existing sprite except the containing Top stays byte-identical.
        for character in before:
            if character != 74:
                self.assertEqual(original[slice(*before[character])], changed[slice(*after[character])])
        placed = swf.placements(changed, *after[74])
        icon = placed['BBModIcon']
        records = [(c, b, n) for c, _, b, n in swf.tags(changed, *after[icon]) if c in (26, 70)]
        self.assertEqual(len(records), 2)
        _, clip, size = records[0]
        self.assertTrue(changed[clip] & 64)
        self.assertEqual(struct.unpack_from('<H', changed, clip + size - 2)[0], 2)
        _, art, _ = records[1]
        self.assertEqual(swf.place_fields(changed, art, 26)[0], 38)
        self.assertEqual(struct.unpack_from('<H', changed, art+1)[0], 2)
        # The mask cannot cover the earlier Inventory/Status icons.
        mask_id = swf.place_fields(changed, clip, 26)[0]
        mask = next(b for c, _, b, _ in swf.tags(changed, swf.stage_start(changed), len(changed))
                    if c == 2 and struct.unpack_from('<H', changed, b)[0] == mask_id)
        r = swf.Reader(changed, mask+2)
        bits = r.unsigned(5)
        self.assertEqual(tuple(r.signed(bits) for _ in range(4)), (3600, 5200, -160, 1442))
        # Required FileAttributes remain ahead of character definitions.
        first_before = next(swf.tags(original, swf.stage_start(original), len(original)))
        first_after = next(swf.tags(changed, swf.stage_start(changed), len(changed)))
        self.assertEqual(first_before[0], first_after[0])


if __name__ == '__main__':
    unittest.main()
