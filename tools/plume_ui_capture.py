#!/usr/bin/env python3
"""Prepare/compare the bounded Plume UI replay from an existing capture.

New adapter tooling. Never launches the game, arms a capture or changes inputs.
Generated packets, shader output and images must remain in ignored local paths.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SHADERS = {
    "ui-vs.spv": "e77b9b0228285837f7b86f0fd8aad7cc5c82c574d20ac43d948212d592ef898f",
    "ui-ps.spv": "a67b801f0d3b45683d796b599781fa0174e55149458cd36f9b982c08e3005a13",
}


def rows(text):
    output = []
    for line in text.splitlines():
        row = {}
        for token in line.split():
            if "=" in token:
                key, value = token.split("=", 1)
                if key in row:
                    raise ValueError("Duplicate event field")
                row[key] = value
            else:
                if "event" in row:
                    raise ValueError("Ambiguous event")
                row["event"] = token
        if row:
            output.append(row)
    return output


def one(events, event):
    matching = [row for row in events if row.get("event") == event]
    if len(matching) != 1:
        raise ValueError(f"Expected one {event}, got {len(matching)}")
    return matching[0]


def check(condition, message):
    if not condition:
        raise ValueError(message)


def prepare(capture, output):
    capture, output = Path(capture).resolve(), Path(output).resolve()
    check(not output.exists(), "Output already exists; evidence will not be overwritten")
    check(not output.is_relative_to(capture), "Output must be outside the source capture")
    events = rows((capture / "correspondence-events.txt").read_text())
    probe = one(events, "ui_probe_before_draw")
    check(probe["ok"] == "1", "Incomplete UI probe")
    selected = events.index(probe)
    before = [e for e in events[:selected] if e.get("event") == "aux_stage_copy"][-1]
    after = next(e for e in events[selected+1:] if e.get("event") == "aux_stage_copy")
    check(before["boundary"] == after["boundary"] == "after_draw" and
          int(after["sequence"]) == int(before["sequence"]) + 1, "UI draw is not isolated by adjacent snapshots")
    check(all(e["capture"] == probe["capture"] and e["attempt"] == probe["attempt"] for e in (before, after)), "Mixed capture IDs/attempts")
    records = rows((capture / "draw-provenance.txt").read_text())
    record = next(i for i, e in enumerate(records) if e.get("event") == "record" and e.get("sequence") == after["sequence"])
    draw = records[record+1]
    check(draw["event"] == "draw" and all(draw[k] == probe[k] for k in ("pipeline", "vs", "ps", "vs_mod", "ps_mod", "count")), "Draw/probe identity mismatch")
    check(draw["guest_primitive"] == "13" and draw["host_primitive"] == "4" and draw["requested_reset"] == "0" and
          draw["shader_index_dma"] == "0" and draw["guest_index_base"] == "0", "Unsupported primitive/index path")
    pipeline = one(events, "ui_pipeline_creation")
    expected = {"vk_topology": "3", "vk_requested_restart": "0", "vk_polygon_mode": "0", "vk_cull_mode": "2",
                "vk_front_face": "1", "vk_samples": "4", "vk_depth_test": "0", "vk_depth_write": "0",
                "vk_blend_enable": "1", "vk_src_color": "6", "vk_dst_color": "7", "vk_color_op": "0",
                "vk_src_alpha": "6", "vk_dst_alpha": "7", "vk_alpha_op": "0", "vk_color_write_mask": "15"}
    check(pipeline["pipeline"] == probe["pipeline"] and all(pipeline.get(k) == v for k, v in expected.items()), "Unsupported recorded pipeline")
    atlas, sampler, vertex, index = (one(events, name) for name in ("ui_atlas_input", "ui_atlas_sampler", "ui_vertex", "ui_index"))
    check(all(e.get("capture") == probe["capture"] and e.get("attempt") == probe["attempt"]
              for e in (pipeline, atlas, sampler, vertex, index)), "Mixed UI resource capture IDs/attempts")
    check(atlas["host_format"] == "133" and all(atlas[f"mapping_{c}"] == "0" for c in "rgba") and atlas["raw_mapping_applied"] == "0", "Only identity BC1_RGBA atlases supported")
    check(all(sampler[k] == v for k, v in {"mag":"1", "min":"1", "mip_mode":"1", "address_u":"0", "address_v":"0", "address_w":"2", "anisotropy":"1", "unnormalized":"0"}.items()), "Unsupported sampler")
    check(all(float(sampler[k]) == v for k, v in {"min_lod":0, "max_lod":1000, "lod_bias":0, "max_anisotropy":4}.items()), "Unsupported sampler LOD/anisotropy")
    sources = {}

    def read(name, limit=32 << 20):
        path = capture / name
        check(path.stat().st_size <= limit, f"Input too large: {name}")
        data = path.read_bytes()
        sources[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        return data

    read("correspondence-events.txt")
    read("draw-provenance.txt")
    pre_name, post_name = (f"aux-stage-{e['index']}.samples4.rgba8" for e in (before, after))
    pre, post = read(pre_name), read(post_name)
    check(not any(pre), "The first adapter requires an observed zero input crop")
    check(len(pre) == len(post) == int(after["width"]) * int(after["height"]) * 16 and after["samples"] == "4", "Snapshot size/sample mismatch")
    width, height = int(after["resource_width"]), int(after["resource_height"])
    check(int(after["width"]) == width and after["owner_key"] == before["owner_key"] and after["image"] == before["image"], "Partial/changed owner crop unsupported")
    check(draw["c0_image"] == after["image"] and int(draw["viewport_x"]) == int(draw["viewport_y"]) == 0, "Viewport/target identity mismatch")
    check(0 < width <= 2048 and 0 < height <= 2048, "Target bounds")
    shaders = [read(name, 1 << 20) for name in SHADERS]
    check(all(sources[name]["sha256"] == sha for name, sha in SHADERS.items()), "Unsupported captured shader module")
    uniforms = [read(f"ui-uniform-{i}.bin", 4096) for i in range(5)]
    check([len(b) for b in uniforms] == [512,64,96,160,768], "Uniform layout mismatch")
    uniform_events = [e for e in events if e.get("event") == "ui_uniform"]
    check(len(uniform_events) == 5 and {e["slot"] for e in uniform_events} == set(map(str,range(5))) and
          all(e["capture"] == probe["capture"] and e["attempt"] == probe["attempt"] and
              int(e["bytes"]) == len(uniforms[int(e["slot"])]) for e in uniform_events), "Uniform capture identity/extent mismatch")
    geometry = read(f"ui-vertex-{vertex['fetch']}.bin", 1 << 20)
    check(vertex["fetch"] == "95" and len(geometry) == int(vertex["bytes"]) and len(geometry) % 24 == 0, "Unsupported vertex layout")
    raw_index = read("ui-index.bin", 1 << 20)
    prefix, valid, stride = (int(index[k]) for k in ("prefix_bytes", "valid_bytes", "index_stride"))
    check(len(raw_index) == int(index["capture_bytes"]) and stride == 2 and valid % 2 == 0 and prefix+valid <= len(raw_index), "Invalid padded index capture")
    indices = struct.unpack("<" + "H"*(valid//2), raw_index[prefix:prefix+valid])
    check(len(indices) == int(probe["count"]) and len(indices) % 3 == 0 and max(indices) < len(geometry)//24, "Index extent mismatch")
    atlas_data = read("ui-atlas-mips.bin", 4 << 20)
    check(len(atlas_data) == int(atlas["bytes"]), "Atlas byte mismatch")
    levels = [e for e in events if e.get("event") == "ui_atlas_mip"]
    check(len(levels) == int(atlas["mip_count"]) and 0 < len(levels) <= 12, "Mip count mismatch")
    mips, end = [], 0
    for i, mip in enumerate(levels):
        offset, size, w, h = (int(mip[k]) for k in ("offset", "bytes", "width", "height"))
        check(mip["capture"] == probe["capture"] and mip["attempt"] == probe["attempt"] and
              int(mip["mip"]) == i and offset >= end and offset+size <= len(atlas_data) and
              w == max(1,int(atlas["width"])>>i) and h == max(1,int(atlas["height"])>>i) and
              size == ((w+3)//4)*((h+3)//4)*8, "Invalid BC1 mip bounds")
        mips.append((w,h,atlas_data[offset:offset+size])); end = offset+size
    packet = bytearray()

    def words(*values):
        packet.extend(struct.pack("<" + "I"*len(values), *values))

    def blob(data):
        words(len(data)); packet.extend(data)

    words(0x31475042,1,width,height,int(draw["viewport_w"]),int(draw["viewport_h"]),
          *(int(draw[f"scissor_{k}"]) for k in ("x","y","w","h")),4,int(vertex["base"]))
    blob(geometry)
    for data in shaders + uniforms:
        blob(data)
    words(len(indices),*indices)
    words(len(mips))
    for w,h,data in mips:
        words(w,h); blob(data)
    output.mkdir(parents=True)
    (output / "ui.bpg").write_bytes(packet)
    manifest = {"schema":1,"capture_id":probe["capture"],"attempt":probe["attempt"],"sequence":after["sequence"],
                "source_capture":str(capture),"source_files":sources,"packet_sha256":hashlib.sha256(packet).hexdigest(),
                "width":width,"height":height,"samples":4,"reference_crop_height":int(after["height"]),
                "reference_before":pre_name,"reference_after":post_name,
                "observed_pipeline":pipeline,"observed_sampler":sampler,
                "limitations":["One UI pass only, no guest CPU/game execution", "Uncaptured rows below the reference crop are not compared", "Cross-API sample positions/index correspondence unverified"]}
    (output / "manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(f"Prepared UI pass {after['sequence']}: {len(indices)} indices, {len(mips)} BC1 mips; {len(packet)} packet bytes")


def compare(packet_directory, replay_directory):
    packet_directory, replay_directory = Path(packet_directory), Path(replay_directory)
    manifest = json.loads((packet_directory / "manifest.json").read_text())
    check(not replay_directory.resolve().is_relative_to(Path(manifest["source_capture"]).resolve()), "Reports must stay outside the source capture")
    check(hashlib.sha256((packet_directory / "ui.bpg").read_bytes()).hexdigest() == manifest["packet_sha256"], "Packet changed")
    source = Path(manifest["source_capture"])/manifest["reference_after"]
    reference = source.read_bytes()
    check(hashlib.sha256(reference).hexdigest() == manifest["source_files"][manifest["reference_after"]]["sha256"], "Reference changed")
    actual_full = (replay_directory / "samples.rgba8").read_bytes()
    metadata = (replay_directory / "result.txt").read_text().splitlines()
    extent = dict(token.split("=",1) for token in metadata[0].split())
    identity = dict(line.split("=",1) for line in metadata[1:])
    check(all(int(extent[key]) == manifest[key] for key in ("width","height","samples")) and
          identity["packet_sha256"] == manifest["packet_sha256"], "Replay does not identify this packet/extent")
    check(len(actual_full) == manifest["width"]*manifest["height"]*16, "Replay sample byte count differs")
    actual = actual_full[:len(reference)]
    max_sample, sum_sample, differing_samples, rgb_nonzero = 0, 0, 0, [0,0]
    max_resolved, sum_resolved, differing_pixels, above_one = 0, 0, 0, 0
    for offset in range(0,len(reference),16):
        different, over_one = False, False
        for channel in range(4):
            av, rv = 0, 0
            for sample in range(4):
                a, r = actual[offset+sample*4+channel], reference[offset+sample*4+channel]
                error = abs(a-r); max_sample = max(max_sample,error); sum_sample += error
                differing_samples += error != 0; av += a; rv += r
            error = abs(av-rv)/4
            max_resolved = max(max_resolved,error); sum_resolved += error
            different |= error != 0; over_one |= error > 1
        differing_pixels += different; above_one += over_one
        rgb_nonzero[0] += any(actual[offset+s*4+c] for s in range(4) for c in range(3))
        rgb_nonzero[1] += any(reference[offset+s*4+c] for s in range(4) for c in range(3))
    pixels = len(reference)//16
    report = {"capture_id":manifest["capture_id"],"sequence":manifest["sequence"],"compared_pixels":pixels,
              "sample_index_comparison":{"max_channel_error":max_sample,"mean_channel_error":sum_sample/len(reference),"differing_channels":differing_samples},
              "resolved_comparison":{"max_channel_error":max_resolved,"mean_channel_error":sum_resolved/(pixels*4),
                                     "differing_pixels":differing_pixels,"pixels_above_one_channel_error":above_one},
              "rgb_nonzero_pixels":{"plume":rgb_nonzero[0],"reference":rgb_nonzero[1]},
              "native_game_adapter_complete":False,"gameplay_established":False,
              "diagnostic":identity["diagnostic"],
              "exact_crop_match":differing_samples == 0 and identity["diagnostic"] == "none",
              "limitations":manifest["limitations"],
              "output_sha256":hashlib.sha256(actual_full).hexdigest()}
    (replay_directory / "comparison.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="command",required=True)
    p=sub.add_parser("prepare"); p.add_argument("capture"); p.add_argument("output")
    c=sub.add_parser("compare"); c.add_argument("packet_directory"); c.add_argument("replay_directory")
    args=parser.parse_args()
    if args.command=="prepare": prepare(args.capture,args.output)
    else:
        report=compare(args.packet_directory,args.replay_directory)
        if not report["exact_crop_match"]: raise SystemExit(1)


if __name__ == "__main__": main()
