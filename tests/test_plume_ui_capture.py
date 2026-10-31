"""Synthetic capture normalization fixtures; no supplied game data."""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location("plume_ui_capture",Path(__file__).resolve().parents[1]/"tools/plume_ui_capture.py")
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)


def fixture(root):
    root.mkdir()
    events=[]
    def row(event,**fields):
        events.append("capture=synthetic attempt=1 "+event+" "+" ".join(f"{k}={v}" for k,v in fields.items()))
    common=dict(boundary="after_draw",owner_key=10,image=20,width=8,height=8,resource_width=8,resource_height=8,samples=4)
    row("aux_stage_copy",index=0,sequence=1,**common)
    row("ui_atlas_sampler",mag=1,min=1,mip_mode=1,address_u=0,address_v=0,address_w=2,anisotropy=1,unnormalized=0,min_lod=0,max_lod=1000,lod_bias=0,max_anisotropy=4)
    row("ui_atlas_mip",mip=0,offset=0,bytes=8,width=4,height=4)
    row("ui_atlas_input",host_format=133,mapping_r=0,mapping_g=0,mapping_b=0,mapping_a=0,raw_mapping_applied=0,bytes=8,mip_count=1,width=4,height=4)
    for i,n in enumerate([512,64,96,160,768]):
        row("ui_uniform",slot=i,bytes=n);(root/f"ui-uniform-{i}.bin").write_bytes(bytes(n))
    row("ui_vertex",fetch=95,base=4096,bytes=72)
    row("ui_index",prefix_bytes=2,valid_bytes=6,index_stride=2,capture_bytes=8)
    pipeline={"vk_topology":3,"vk_requested_restart":0,"vk_polygon_mode":0,"vk_cull_mode":2,"vk_front_face":1,"vk_samples":4,
              "vk_depth_test":0,"vk_depth_write":0,"vk_blend_enable":1,"vk_src_color":6,"vk_dst_color":7,"vk_color_op":0,
              "vk_src_alpha":6,"vk_dst_alpha":7,"vk_alpha_op":0,"vk_color_write_mask":15}
    row("ui_pipeline_creation",pipeline=3,**pipeline)
    draw=dict(pipeline=3,vs=4,ps=5,vs_mod=7,ps_mod=262151,count=3)
    row("ui_probe_before_draw",ok=1,**draw)
    row("aux_stage_copy",index=1,sequence=2,**common)
    (root/"correspondence-events.txt").write_text("\n".join(events)+"\n")
    draw.update(guest_primitive=13,host_primitive=4,requested_reset=0,shader_index_dma=0,guest_index_base=0,c0_image=20,
                viewport_x=0,viewport_y=0,viewport_w=8,viewport_h=8,scissor_x=0,scissor_y=0,scissor_w=8,scissor_h=8)
    (root/"draw-provenance.txt").write_text("record sequence=2 epoch=1\ndraw "+" ".join(f"{k}={v}" for k,v in draw.items())+"\n")
    for i in range(2):(root/f"aux-stage-{i}.samples4.rgba8").write_bytes(bytes(8*8*16))
    (root/"ui-vertex-95.bin").write_bytes(bytes(72))
    (root/"ui-index.bin").write_bytes(struct.pack("<4H",99,0,1,2))
    (root/"ui-atlas-mips.bin").write_bytes(bytes(8))
    shaders={}
    for i,name in enumerate(tool.SHADERS):
        data=struct.pack("<5I",0x07230203,0x10000,i,1,0);(root/name).write_bytes(data)
        shaders[name]=hashlib.sha256(data).hexdigest()
    return shaders


class CapturePacketTests(unittest.TestCase):
    def test_duplicate_fields_and_events_rejected(self):
        with self.assertRaises(ValueError):tool.rows("capture=a capture=b probe")
        with self.assertRaises(ValueError):tool.rows("capture=a probe extra")
        with self.assertRaises(ValueError):tool.one([{"event":"probe"}]*2,"probe")

    def _run(self,mutate=None):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);source=base/"source";shaders=fixture(source)
            if mutate:mutate(source)
            with patch.dict(tool.SHADERS,shaders,clear=True),redirect_stdout(io.StringIO()):
                tool.prepare(source,base/"packet")
            packet=(base/"packet/ui.bpg").read_bytes()
            self.assertEqual(packet[:8],b"BPG1\x01\0\0\0")
            self.assertEqual(struct.unpack_from("<4I",packet,8),(8,8,8,8))

    def test_complete_synthetic_packet(self):self._run()

    def test_mixed_capture_resource_rejected(self):
        def mutate(p):
            f=p/"correspondence-events.txt";f.write_text(f.read_text().replace("capture=synthetic attempt=1 ui_vertex","capture=other attempt=1 ui_vertex"))
        with self.assertRaisesRegex(ValueError,"Mixed UI resource"):self._run(mutate)

    def test_nonzero_initial_crop_rejected(self):
        def mutate(p):(p/"aux-stage-0.samples4.rgba8").write_bytes(bytes([1])+bytes(1023))
        with self.assertRaisesRegex(ValueError,"observed zero input"):self._run(mutate)

    def test_bad_index_capture_rejected(self):
        def mutate(p):(p/"ui-index.bin").write_bytes(struct.pack("<4H",99,0,1,3))
        with self.assertRaisesRegex(ValueError,"Index extent"):self._run(mutate)

    def test_unknown_pipeline_rejected(self):
        def mutate(p):
            f=p/"correspondence-events.txt";f.write_text(f.read_text().replace("vk_depth_test=0","vk_depth_test=1"))
        with self.assertRaisesRegex(ValueError,"Unsupported recorded pipeline"):self._run(mutate)

    def test_existing_destination_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            with self.assertRaisesRegex(ValueError,"Output already exists"):tool.prepare(p,p)


if __name__=="__main__":unittest.main()
