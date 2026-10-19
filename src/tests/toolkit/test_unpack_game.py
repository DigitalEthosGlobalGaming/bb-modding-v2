import importlib.util
from pathlib import Path
import struct,tempfile,unittest,zlib

spec=importlib.util.spec_from_file_location('unpack_game',Path(__file__).resolve().parents[2]/'scripts/unpack_game.py')
u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)

def dcx(body):
    packed=zlib.compress(body);header=bytearray(76);header[:4]=b'DCX\0';header[40:44]=b'DFLT'
    struct.pack_into('>II',header,28,len(body),len(packed));return bytes(header)+packed

def bnd(body,name='N:\\dev\\weapon.flver'):
    text=name.encode('utf-16-le')+b'\0\0';start=100+len(text)
    header=bytearray(64);header[:4]=b'BND4';header[10]=1;header[48]=1;header[49]=0x74
    struct.pack_into('<I',header,12,1);struct.pack_into('<Q',header,16,64);struct.pack_into('<Q',header,32,36)
    row=bytes([0x40,0,0,0])+b'\xff'*4+struct.pack('<QQIiI',len(body),len(body),start,100,100)
    return bytes(header)+row+text+body

class UnpackTests(unittest.TestCase):
    def test_dcx_roundtrip_and_bounds(self):
        self.assertEqual(u.dcx(dcx(b'model'*100)),b'model'*100)
        broken=bytearray(dcx(b'body'));struct.pack_into('>I',broken,28,2)
        with self.assertRaises(ValueError):u.dcx(broken)
        with self.assertRaises(ValueError):u.dcx(dcx(b'body')[:-3])
        with self.assertRaises(ValueError):u.inflate(zlib.compress(b'12345'),4)

    def test_binder_and_reject_offset(self):
        self.assertEqual(list(u.binder(bnd(b'mesh')))[0][3],b'mesh')
        broken=bytearray(bnd(b'mesh'));struct.pack_into('<I',broken,64+24,0xffffffff)
        with self.assertRaises(ValueError):list(u.binder(broken))

    def test_nested_extract_resume_original_preservation(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'source';output=root/'out';source.mkdir();output.mkdir()
            raw=dcx(bnd(dcx(b'FLVER\0data'),'../../weapon.flver.dcx'));p=source/'sample.partsbnd.dcx';p.write_bytes(raw)
            record=u.process(source,Path(p.name),output)
            self.assertEqual(record['status'],'done');self.assertEqual(len(record['outputs']),2)
            self.assertEqual(p.read_bytes(),raw);self.assertTrue(u.cached(record,p,output))
            leaf=output/record['outputs'][1]['path'];self.assertTrue(leaf.resolve().is_relative_to(output.resolve()))
            self.assertEqual(leaf.read_bytes(),b'FLVER\0data');self.assertEqual(leaf.suffix,'.flver')
            leaf.write_bytes(b'edited');self.assertFalse(u.cached(record,p,output))
            retried=u.process(source,Path(p.name),output);self.assertEqual(retried['status'],'partial');self.assertEqual(leaf.read_bytes(),b'edited')

    def test_leaf_name_never_uses_internal_path(self):
        self.assertNotIn('/',u.leaf_name(1,2,'C:\\dev\\..\\x.flver'))
        self.assertNotIn('\\',u.leaf_name(1,2,'C:\\dev\\..\\x.flver'))

    def test_original_names_and_collision_identity(self):
        self.assertEqual(u.member_names([(4,1004,'N:\\dev\\000104_platoon.lua')]),['000104_platoon.lua'])
        self.assertEqual(u.member_names([(0,100,'a/x.hkx'),(1,101,'b/X.hkx.dcx')]),
                         ['00000_100_x.hkx','00001_101_X.hkx.dcx'])

    def test_output_traversal_rejected_before_write(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'out';root.mkdir()
            with self.assertRaises(ValueError):u.write_asset(root,Path('../escape.bin'),b'data','test',[])
            self.assertFalse((root.parent/'escape.bin').exists())

if __name__=='__main__':unittest.main()
