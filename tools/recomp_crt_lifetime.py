"""Versioned lifetime verification on the runtime-evidenced callback build."""
import argparse
import json
import os
from pathlib import Path
import sys
try:
    from . import recomp_crt_callback as h
except ImportError:
    import recomp_crt_callback as h
r=h.r;p=h.p;c=h.c;ROOT=h.ROOT;WORK=h.WORK;KIT=h.KIT
OUT=WORK/'lifetime-r3'


def identity(revision):
    result=r.verify(revision)
    accepted=json.loads((h.OUT/'accepted.json').read_text())
    if not accepted['translation_gate'] or not accepted['runtime_callback_dispatch_present']:raise ValueError('Callback coverage required')
    c.verify_files(accepted['generated'])
    built=json.loads((h.OUT/'build.json').read_text());c.verify_files(built['generated'])
    result['reviewed_callback_sha256']=p.digest(h.OUT/'accepted.json')
    result['source_sha256']=p.digest(Path(__file__).resolve())
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=('build','child-build','test','run'));parser.add_argument('--revision',type=int,choices=(3,4),default=3);parser.add_argument('--repeat',type=int,default=0);args=parser.parse_args();global OUT;OUT=WORK/('lifetime-r%d'%args.revision);ident=identity(args.revision);OUT.mkdir(exist_ok=True)
    if args.action=='child-build':
        r.e.kit_build().build('macos',['pop_headless','crt_strict_tests','seh_tests'],4,build_dir=h.CMAKE);return
    if args.action=='build':
        step=c.run_bounded([sys.executable,Path(__file__).resolve(),'child-build','--revision',str(args.revision)],OUT/'build.log',600)
        result=dict(identity=ident,step=step)
        if not step['exit_code']:result['binary_sha256']=p.digest(h.BUILD/'recomp/pop_headless')
        p.write_report(OUT/'build.json',result)
        if step['exit_code']:sys.exit(1)
    if args.action=='test':
        step=c.run_bounded([r.e.kit_build().cmake_tool('ctest'),'--test-dir',h.CMAKE,'--output-on-failure','-R','^(crt_strict_tests|seh_tests)$'],OUT/'tests.log',30)
        p.write_report(OUT/'tests.json',dict(identity=ident,step=step))
        if step['exit_code']:sys.exit(1)
    if args.action=='run':
        built=json.loads((OUT/'build.json').read_text());tested=json.loads((OUT/'tests.json').read_text());binary=h.BUILD/'recomp/pop_headless'
        if built['identity']!=ident or tested['identity']!=ident or tested['step']['exit_code'] or p.digest(binary)!=built['binary_sha256']:raise ValueError('Validated lifetime build required')
        scratch=OUT/('run%d'%args.repeat)
        if scratch.exists():raise ValueError('Run exists')
        scratch.mkdir();env={k:v for k,v in os.environ.items() if not k.startswith('RECOMP_')}
        env.update(RECOMP_STRICT_EXECUTION='1',RECOMP_NULL_CHECKS='1',RECOMP_EXE=str(WORK/'original/crt-probe.exe'),RECOMP_NO_MODS='1',RECOMP_PROFILE_DIR=str(scratch/'profile'),RECOMP_FRAMES=str(scratch/'frames'),RECOMP_MAX_SECONDS='5',RECOMP_LOG='1',RECOMP_IMPORT_STATS='1')
        step=c.run_bounded([binary],scratch/'host.log',10,env);log=(scratch/'host.log').read_text()
        events=[l for l in log.splitlines() if 'crt.guard.' in l]
        p.write_report(scratch/'result.json',dict(identity=ident,step=step,complete='crt.smoke.complete 1' in log,events=events,passed=False))
        print('\n'.join(events));print('complete:', 'crt.smoke.complete 1' in log)

if __name__=='__main__':main()
