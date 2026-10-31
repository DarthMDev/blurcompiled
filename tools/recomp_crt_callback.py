"""Replay a runtime-evidenced CRT callback through strict whole-module recovery."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys

try:
    from . import recomp_crt_runtime as r
    from . import recomp_crt_acceptance as a
except ImportError:
    import recomp_crt_runtime as r
    import recomp_crt_acceptance as a
p,c=r.p,r.c
ROOT,WORK,KIT=r.ROOT,r.WORK,r.KIT
OUT=WORK/'callback-round1'
BUILD=OUT/'build'
CMAKE=WORK/'build/cmake/macos'


def identity():
    result=r.verify(2)
    result['callback_sources']=p.fingerprint([Path(__file__).resolve(),OUT/'evidence.json',OUT/'discovered.txt',OUT/'statistics.json',ROOT/'recomp/crt-probe/msvc90-data-inventory.json'])
    return result


def accept():
    inv=json.loads((ROOT/'recomp/crt-probe/msvc90-data-inventory.json').read_text())
    im=p.CRTImage((WORK/'original/msvcr90.dll').read_bytes())
    table=(OUT/'generated/table.c').read_text();match=re.search(r'func_addrs\[\] = \{(.*?)\};',table,re.S)
    entries={int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',match.group(1))}
    roots={im.image_base+im.entry_rva}|{im.image_base+e['rva'] for e in im.exports() if e['kind']=='code'}
    if 0x7858c76d not in entries:raise ValueError('Runtime callback absent from dispatch')
    report=dict(identity={'candidate_sha256':p.digest(WORK/'original/msvcr90.dll')},step=json.loads((OUT/'translation-step.json').read_text()),statistics=json.loads((OUT/'statistics.json').read_text()),root_gaps=sorted(roots-entries))
    result=a.review(report,inv,im,ROOT)
    result.update(identity=identity(),generated=p.fingerprint([f for f in (OUT/'generated').iterdir() if f.is_file()]),runtime_callback_dispatch_present=True)
    p.write_report(OUT/'accepted.json',result)


def validated():
    accepted=json.loads((OUT/'accepted.json').read_text())
    if accepted['identity']!=identity():raise ValueError('Callback experiment changed')
    c.verify_files(accepted['generated'])
    return accepted


def build():
    validated();gen=BUILD/'recomp/gen'
    if gen.exists():raise ValueError('Callback build exists')
    original=json.loads((WORK/'translation.json').read_text());c.verify_files(original['generated'])
    shutil.copytree(WORK/'fixture-generated',gen);shutil.copytree(OUT/'generated',gen/'aux-msvcr90');shutil.copyfile(KIT/'runtime/x86.h',gen/'x86.h')
    module=r.e.kit_build();defines=module.game_defines(WORK,BUILD)+['-DRECOMP_VIDEO=OFF','-DCMAKE_C_FLAGS=-DRECOMP_NULL_CHECKS=1','-DCMAKE_CXX_FLAGS=-DRECOMP_NULL_CHECKS=1']
    for name in ('sdl3','glslang'):
        source=ROOT/'recomp/build/cmake/macos-stub/_deps'/(name+'-src');defines.append('-DFETCHCONTENT_SOURCE_DIR_'+name.upper()+'='+str(source))
    module.configure('macos',defines,build_dir=CMAKE)
    module.build('macos',['pop_headless','crt_strict_tests','seh_tests'],4,build_dir=CMAKE)
    p.write_report(OUT/'build.json',dict(identity=identity(),binary_sha256=p.digest(BUILD/'recomp/pop_headless'),generated=p.fingerprint([f for f in gen.rglob('*') if f.is_file()])))


def test():
    validated();built=json.loads((OUT/'build.json').read_text())
    if built['identity']!=identity():raise ValueError('Build changed')
    step=c.run_bounded([r.e.kit_build().cmake_tool('ctest'),'--test-dir',CMAKE,'--output-on-failure','-R','^(crt_strict_tests|seh_tests)$'],OUT/'tests.log',30)
    p.write_report(OUT/'tests.json',dict(identity=identity(),step=step))
    if step['exit_code']:sys.exit(1)


def run(number):
    validated();built=json.loads((OUT/'build.json').read_text());tested=json.loads((OUT/'tests.json').read_text());binary=BUILD/'recomp/pop_headless'
    if tested['step']['exit_code'] or built['identity']!=identity() or p.digest(binary)!=built['binary_sha256']:raise ValueError('Validated binary required')
    c.verify_files(built['generated']);scratch=OUT/('run%d'%number)
    if scratch.exists():raise ValueError('Run exists')
    scratch.mkdir();env={k:v for k,v in os.environ.items() if not k.startswith('RECOMP_')}
    env.update(RECOMP_STRICT_EXECUTION='1',RECOMP_NULL_CHECKS='1',RECOMP_EXE=str(WORK/'original/crt-probe.exe'),RECOMP_NO_MODS='1',RECOMP_PROFILE_DIR=str(scratch/'profile'),RECOMP_FRAMES=str(scratch/'frames'),RECOMP_MAX_SECONDS='5',RECOMP_LOG='2',RECOMP_IMPORT_STATS='1')
    step=c.run_bounded([binary],scratch/'host.log',10,env);log=(scratch/'host.log').read_text()
    p.write_report(scratch/'result.json',dict(identity=identity(),step=step,complete='crt.smoke.complete 1' in log,events=[l for l in log.splitlines() if 'crt.guard.' in l],passed=False,notice='Completion is a measured fixture result; lifetime and original-runtime parity require independent review.'))
    print(log[-7000:])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=('accept','build','test','run'));parser.add_argument('--repeat',type=int,default=0);args=parser.parse_args()
    {'accept':accept,'build':build,'test':test,'run':lambda:run(args.repeat)}[args.action]()

if __name__=='__main__':main()
