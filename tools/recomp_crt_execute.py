"""Isolated synthetic Microsoft CRT probe. No installed-game writes or Blur run."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace

try:
    from . import recomp_crt_compare as c
except ImportError:
    import recomp_crt_compare as c
p=c.prior
ROOT=p.ROOT
KIT=ROOT/'local/recomp-kit-msvc90-execution'
WORK=ROOT/'local/recomp-crt-msvc90-execution'
BUILD=WORK/'build'
PATCH=ROOT/'recomp/patches/msvc90-execution-guards.patch'
ACCEPT=ROOT/'reports/recomp-crt-msvc90-accepted-final.json'


def inputs():
    c.verified(SimpleNamespace(work=ROOT/'local/recomp-crt-msvc90-translation'))
    accepted=json.loads(ACCEPT.read_text())
    if accepted['translation_gate'] is not True:raise ValueError('Reviewed translation gate required')
    c.verify_files(accepted['inputs'])
    original=json.loads((ROOT/'reports/recomp-crt-msvc90-noreturn-round1.json').read_text())
    c.verify_files(original['artifacts'])
    if subprocess.check_output(['git','-C',str(KIT),'rev-parse','HEAD'],text=True).strip()!=p.PIN:raise ValueError('Kit pin mismatch')
    if subprocess.check_output(['git','-C',str(KIT),'diff','--binary'])!=PATCH.read_bytes():raise ValueError('Execution patch changed')
    return dict(kit_commit=p.PIN,patch_sha256=p.digest(PATCH),acceptance_sha256=p.digest(ACCEPT),
                source_inputs=p.fingerprint([Path(__file__).resolve(),ROOT/'tools/recomp_crt_compare.py',ROOT/'tools/recomp_crt_experiment.py',ROOT/'recomp/crt-probe/src/msvc90_probe.c',PATCH]))


def verified():
    m=json.loads((WORK/'manifest.json').read_text())
    if inputs()!=m['identity']:raise ValueError('Execution identity changed')
    c.verify_files(m['staged'])
    return m


def command(cmd,log,timeout,env=None):
    r=c.run_bounded(cmd,log,timeout,env)
    if r['exit_code']!=0:raise ValueError('Command failed: '+r['log'])
    return r


def prepare(args):
    identity=inputs()
    if WORK.exists():raise ValueError('Work root exists')
    original=WORK/'original';original.mkdir(parents=True)
    dll=ROOT/'local/recomp-crt-msvc90-translation/original/msvcr90.dll'
    shutil.copyfile(dll,original/'msvcr90.dll')
    steps=[]
    steps.append(command(['/opt/homebrew/opt/llvm/bin/llvm-dlltool','-m','i386','-k','-d',p.SOURCE/'kernel32.def','-l',WORK/'kernel32.lib'],WORK/'dlltool.log',30))
    steps.append(command(['/opt/homebrew/opt/llvm/bin/clang','--target=i686-pc-windows-msvc','-O2','-march=i386','-ffreestanding','-fno-stack-protector','-fno-builtin','-fno-asynchronous-unwind-tables','-c',ROOT/'recomp/crt-probe/src/msvc90_probe.c','-o',WORK/'probe.obj'],WORK/'clang.log',60))
    linker=Path.home()/'.rustup/toolchains/stable-aarch64-apple-darwin/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/lld-link'
    steps.append(command([linker,'/machine:x86','/entry:start','/subsystem:console','/nodefaultlib','/base:0x400000','/fixed','/dynamicbase:no','/safeseh:no','/timestamp:0','/out:'+str(original/'crt-probe.exe'),WORK/'probe.obj',WORK/'kernel32.lib'],WORK/'link.log',60))
    exe=p.CRTImage((original/'crt-probe.exe').read_bytes())
    old=ROOT/'local/recomp-crt-msvc90-translation'
    config=(old/'game.toml').read_text().replace(p.digest(old/'original/crt-probe.exe'),p.digest(original/'crt-probe.exe'))
    old_exe=p.CRTImage((old/'original/crt-probe.exe').read_bytes())
    config=config.replace('entry_point = '+hex(old_exe.image_base+old_exe.entry_rva),'entry_point = '+hex(exe.image_base+exe.entry_rva))
    config=config.replace('listings = "analysis/msvcr90.dll"','listings = "'+str(old/'analysis/msvcr90.dll')+'"')
    (WORK/'game.toml').write_text(config);(WORK/'globals.toml').write_text('# Synthetic fixture; no recovered globals.\n[globals]\n')
    p.write_report(WORK/'manifest.json',dict(identity=identity,staged=p.fingerprint(list(original.iterdir())+[WORK/'game.toml',WORK/'globals.toml']),steps=steps))


def export(args):
    verified();analysis=WORK/'analysis';project=WORK/'ghidra'
    if analysis.exists():raise ValueError('Export exists')
    analysis.mkdir();project.mkdir()
    home=ROOT/'local/tools/ghidra_12.1.4_PUBLIC';java=ROOT/'local/tools/jdk-21.0.12.1+1/Contents/Home'
    env=dict(os.environ,JAVA_HOME=str(java),MAXMEM='2G');env['PATH']=str(java/'bin')+os.pathsep+env.get('PATH','')
    step=command([home/'support/analyzeHeadless',project,'fixture','-import',WORK/'original/crt-probe.exe','-analysisTimeoutPerFile','120','-max-cpu','4','-scriptPath',ROOT/'tools/ghidra','-postScript','ExportPinnedRecompListings.java',analysis,p.digest(WORK/'original/crt-probe.exe')],WORK/'export.log',180,env)
    summary=analysis/'crt-probe.exe/summary.txt'
    if 'sha256='+p.digest(WORK/'original/crt-probe.exe') not in summary.read_text():raise ValueError('Export hash mismatch')
    p.write_report(WORK/'export.json',dict(step=step,inputs=p.fingerprint([f for f in analysis.rglob('*') if f.is_file()])))


def translate(args):
    verified();c.verify_files(json.loads((WORK/'export.json').read_text())['inputs'])
    out=WORK/'fixture-generated'
    if out.exists():raise ValueError('Fixture translation exists')
    step=command([sys.executable,KIT/'tools/recomp/translate.py','--game',WORK,'--out',out,'--report',WORK/'fixture-statistics.json'],WORK/'fixture-translate.log',120)
    stats=json.loads((WORK/'fixture-statistics.json').read_text())
    if not c.strict_gate(0,stats,[]):raise ValueError('Fixture strict gate failed')
    p.write_report(WORK/'translation.json',dict(step=step,statistics=stats,generated=p.fingerprint([f for f in out.iterdir() if f.is_file()])))


def kit_build():
    spec=importlib.util.spec_from_file_location('execution_kit_build',KIT/'tools/build.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def build(args):
    verified();tr=json.loads((WORK/'translation.json').read_text());c.verify_files(tr['generated'])
    gen=BUILD/'recomp/gen'
    if gen.exists():raise ValueError('Published output exists')
    shutil.copytree(WORK/'fixture-generated',gen)
    shutil.copytree(ROOT/'local/recomp-crt-msvc90-noreturn/round1/generated',gen/'aux-msvcr90')
    shutil.copyfile(KIT/'runtime/x86.h',gen/'x86.h')
    module=kit_build();defines=module.game_defines(WORK,BUILD)+['-DRECOMP_VIDEO=OFF','-DCMAKE_C_FLAGS=-DRECOMP_NULL_CHECKS=1','-DCMAKE_CXX_FLAGS=-DRECOMP_NULL_CHECKS=1']
    for name in ('sdl3','glslang'):
        source=ROOT/'recomp/build/cmake/macos-stub/_deps'/(name+'-src')
        if not source.is_dir():raise ValueError('Cached dependency missing')
        defines.append('-DFETCHCONTENT_SOURCE_DIR_'+name.upper()+'='+str(source))
    # All native configuration and build commands go through kit tools/build.py.
    with (WORK/'build.log').open('x') as stream:
        import contextlib
        with contextlib.redirect_stdout(stream),contextlib.redirect_stderr(stream):
            # tools/build.py inherits file descriptors, so outer subprocess logs
            # capture its compiler output in the action log as well.
            module.configure('macos',defines,build_dir=BUILD/'cmake/macos')
            module.build('macos',['pop_headless','crt_strict_tests','seh_tests'],4,build_dir=BUILD/'cmake/macos')
    binary=BUILD/'recomp/pop_headless'
    if not binary.is_file():raise ValueError('No host binary')
    p.write_report(WORK/'build.json',dict(identity=inputs(),binary_sha256=p.digest(binary),generated=p.fingerprint([f for f in gen.rglob('*') if f.is_file()])))


def run(args):
    verified();built=json.loads((WORK/'build.json').read_text());binary=BUILD/'recomp/pop_headless'
    if built['identity']!=inputs() or p.digest(binary)!=built['binary_sha256']:raise ValueError('Build changed')
    c.verify_files(built['generated'])
    scratch=WORK/('run%d'%args.round)
    if scratch.exists():raise ValueError('Run exists')
    scratch.mkdir()
    env={k:v for k,v in os.environ.items() if not k.startswith('RECOMP_')}
    env.update(RECOMP_STRICT_EXECUTION='1',RECOMP_NULL_CHECKS='1',RECOMP_EXE=str(WORK/'original/crt-probe.exe'),RECOMP_NO_MODS='1',RECOMP_PROFILE_DIR=str(scratch/'profile'),RECOMP_FRAMES=str(scratch/'frames'),RECOMP_MAX_SECONDS='5',RECOMP_LOG='2',RECOMP_IMPORT_STATS='1')
    step=c.run_bounded([binary],scratch/'host.log',10,env)
    log=(scratch/'host.log').read_text()
    p.write_report(scratch/'result.json',dict(identity=inputs(),step=step,complete='crt.smoke.complete 1' in log,guard_diagnostics=[l for l in log.splitlines() if 'crt.guard.' in l],dll_executed='crt.guard.dll_entry.begin' in log,passed=False,notice='Diagnostic run only; completion alone does not prove attach/ABI/ownership/cleanup.'))
    print(log[-5000:])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=('prepare','export','translate','build','run'));parser.add_argument('--round',type=int,default=0)
    args=parser.parse_args()
    try:globals()[args.action](args)
    except (OSError,ValueError,subprocess.SubprocessError) as e:parser.exit(1,'Execution probe stopped: %s\n'%e)

if __name__=='__main__':main()
