"""Versioned native service revisions for the isolated CRT fixture."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

try:
    from . import recomp_crt_execute as e
except ImportError:
    import recomp_crt_execute as e
p,c,ROOT,KIT,WORK,BUILD=e.p,e.c,e.ROOT,e.KIT,e.WORK,e.BUILD


def verify(revision):
    c.verified(SimpleNamespace(work=ROOT/'local/recomp-crt-msvc90-translation'))
    base=json.loads((WORK/'manifest.json').read_text());c.verify_files(base['staged'])
    accepted=json.loads(e.ACCEPT.read_text());c.verify_files(accepted['inputs'])
    built=json.loads((WORK/'build.json').read_text());c.verify_files(built['generated'])
    patch=ROOT/('recomp/patches/msvc90-runtime-r%d.patch'%revision)
    if subprocess.check_output(['git','-C',str(KIT),'rev-parse','HEAD'],text=True).strip()!=p.PIN:raise ValueError('Kit pin changed')
    if subprocess.check_output(['git','-C',str(KIT),'diff','--binary'])!=patch.read_bytes():raise ValueError('Runtime patch mismatch')
    return dict(kit_commit=p.PIN,patch_sha256=p.digest(patch),acceptance_sha256=p.digest(e.ACCEPT),base_manifest_sha256=p.digest(WORK/'manifest.json'),
                sources=p.fingerprint([Path(__file__).resolve(),ROOT/'tools/recomp_crt_execute.py',patch]))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=('build','test','run','child-build'));parser.add_argument('--revision',type=int,required=True)
    args=parser.parse_args();rev=args.revision
    if not 1<=rev<=8:parser.error('At most eight versioned revisions')
    identity=verify(rev);out=WORK/('runtime-r%d'%rev);out.mkdir(exist_ok=True)
    if args.action=='child-build':
        module=e.kit_build();module.build('macos',['pop_headless','crt_strict_tests','seh_tests'],4,build_dir=BUILD/'cmake/macos');return
    if args.action=='build':
        step=c.run_bounded([sys.executable,Path(__file__).resolve(),'child-build','--revision',str(rev)],out/'build.log',600)
        result=dict(identity=identity,step=step)
        if step['exit_code']==0:result['binary_sha256']=p.digest(BUILD/'recomp/pop_headless')
        p.write_report(out/'build.json',result)
        if step['exit_code']:sys.exit(1)
    if args.action=='test':
        if json.loads((out/'build.json').read_text())['identity']!=identity:raise ValueError('Build inputs changed')
        command=[e.kit_build().cmake_tool('ctest'),'--test-dir',BUILD/'cmake/macos','--output-on-failure','-R','^(crt_strict_tests|seh_tests)$']
        step=c.run_bounded(command,out/'tests.log',30)
        p.write_report(out/'tests.json',dict(identity=identity,step=step))
        if step['exit_code']:sys.exit(1)
    if args.action=='run':
        built=json.loads((out/'build.json').read_text());tested=json.loads((out/'tests.json').read_text())
        binary=BUILD/'recomp/pop_headless'
        if built['identity']!=identity or tested['identity']!=identity or tested['step']['exit_code'] or p.digest(binary)!=built['binary_sha256']:raise ValueError('Validated build required')
        env={k:v for k,v in os.environ.items() if not k.startswith('RECOMP_')}
        env.update(RECOMP_STRICT_EXECUTION='1',RECOMP_NULL_CHECKS='1',RECOMP_EXE=str(WORK/'original/crt-probe.exe'),RECOMP_NO_MODS='1',RECOMP_PROFILE_DIR=str(out/'profile'),RECOMP_FRAMES=str(out/'frames'),RECOMP_MAX_SECONDS='5',RECOMP_LOG='2',RECOMP_IMPORT_STATS='1')
        step=c.run_bounded([binary],out/'host.log',10,env);log=(out/'host.log').read_text()
        p.write_report(out/'run.json',dict(identity=identity,step=step,complete='crt.smoke.complete 1' in log,passed=False,events=[l for l in log.splitlines() if 'crt.guard.' in l],notice='Diagnostic run; manually audit ABI/attach/lifetime/ownership before acceptance.'))
        print(log[-7000:])

if __name__=='__main__':main()
