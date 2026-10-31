"""Hash-pinned, translation-only CRT candidate comparison; no build/run action."""
import argparse
from collections import Counter
import json
import os
import re
from pathlib import Path
import signal
import subprocess
import sys
import time

try:
    from . import recomp_crt_experiment as prior
except ImportError:
    import recomp_crt_experiment as prior

ROOT, KIT = prior.ROOT, prior.KIT


def checked_candidate(data, descriptor):
    import hashlib
    if hashlib.sha256(data).hexdigest() != descriptor['sha256']:
        raise ValueError('Candidate SHA-256 mismatch')
    image = prior.CRTImage(data)
    for key in ('image_base', 'image_size', 'entry_rva'):
        if getattr(image, key) != descriptor[key]:
            raise ValueError('Candidate metadata mismatch: ' + key)
    return image


def guest_map(base, size):
    regions = [('fixture', 0x400000, 0x400000 + 0x10000),
               ('heap', 0x02000000, 0x0e000000),
               ('stack', 0x0ef00000, 0x0f000000),
               ('return_sentinel', 0x0fdfff00, 0x0fe00000),
               ('PEB_TEB_TLS_reservation', 0x0fe00000, 0x0ff00000),
               ('shims', 0x0ff00000, 0x10000000),
               ('candidate', base, base + size)]
    if base < 0x10000000 or size <= 0 or base + size > 0xffffffff:
        raise ValueError('Candidate cannot fit checked 32-bit arena')
    regions.sort(key=lambda r: r[1])
    if any(a[2] > b[1] for a, b in zip(regions, regions[1:])):
        raise ValueError('Guest map collision')
    arena = (base + size + 4095) & ~4095
    if arena > 0xffffffff:
        raise ValueError('Arena alignment overflows 32 bits')
    return {'arena_size': arena, 'regions': [dict(name=n, begin=b, end=e) for n, b, e in regions],
            'execution_authorized': False,
            'allocation_audit': 'memory.cpp:mem_init calls os_vm_reserve(GUEST_SIZE); platform/os_posix.cpp uses anonymous private RW mmap, no explicit eager memset of full arena. Actual commit/resource limits unmeasured; no high-base allocation attempted.'}


def run_bounded(command, log, timeout, env=None):
    log = prior.safe_output(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    timed_out = False
    with log.open('x') as stream:
        proc = subprocess.Popen([str(p) for p in command], cwd=ROOT, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(proc.pid, signal.SIGKILL)
            code = proc.wait()
    return dict(command=[str(p) for p in command], log=str(log.relative_to(ROOT)),
                exit_code=code, timed_out=timed_out, seconds=round(time.monotonic()-start, 2),
                timeout_seconds=timeout, child_cleanup='dedicated process group killed on timeout')


def verify_files(inputs):
    for name, sha in inputs.items():
        path = ROOT / name
        if path.is_symlink() or path.resolve() != path or prior.digest(path) != sha:
            raise ValueError('Comparison input changed: ' + name)


def metadata(data):
    import pefile
    image = prior.CRTImage(data)
    pe = pefile.PE(data=data)
    sha = __import__('hashlib').sha256(data).hexdigest()
    evidence = dict(sha256=sha, evidence='PE export table and section permissions', confidence='high for metadata; ABI/behavior unverified')
    exports = [dict(e, **evidence) for e in image.exports()]
    registry = prior.source_registry()
    dependencies = []
    for group in image.imports():
        rows = [dict(name=n, **registry.get((group['dll'].lower(), n.replace('ordinal:', 'ord')),
                                          {'status': 'not_found_by_literal_scan'})) for n in group['functions']]
        dependencies.append(dict(dll=group['dll'], functions=rows, counts=dict(Counter(r['status'] for r in rows))))
    tls = None
    if hasattr(pe, 'DIRECTORY_ENTRY_TLS'):
        s = pe.DIRECTORY_ENTRY_TLS.struct
        callbacks=[];at=s.AddressOfCallBacks-image.image_base
        if s.AddressOfCallBacks:
            for i in range(image.image_size//4):
                v=image.rva_u32(at+4*i)
                if not v: break
                callbacks.append(v)
            else: raise ValueError('Unterminated TLS callbacks')
        tls=dict(template_start=s.StartAddressOfRawData,template_end=s.EndAddressOfRawData,
                 index_address=s.AddressOfIndex,zero_fill=s.SizeOfZeroFill,characteristics=s.Characteristics,callbacks=callbacks)
    versions=[]
    for v in getattr(pe,'VS_FIXEDFILEINFO',[]):
        versions.append(dict(file_version=[v.FileVersionMS>>16,v.FileVersionMS&65535,v.FileVersionLS>>16,v.FileVersionLS&65535],
                             product_version=[v.ProductVersionMS>>16,v.ProductVersionMS&65535,v.ProductVersionLS>>16,v.ProductVersionLS&65535]))
    manifests=[]
    resources=getattr(pe,'DIRECTORY_ENTRY_RESOURCE',None)
    for t in resources.entries if resources else []:
        if t.id==24:
            for n in t.directory.entries:
                for l in n.directory.entries:
                    s=l.data.struct
                    manifests.append(dict(id=n.id,language=l.id,rva=s.OffsetToData,text=pe.get_data(s.OffsetToData,s.Size).decode('utf-8','strict')))
    relocations=Counter(e.type for b in getattr(pe,'DIRECTORY_ENTRY_BASERELOC',[]) for e in b.entries)
    delay=[]
    for d in getattr(pe,'DIRECTORY_ENTRY_DELAY_IMPORT',[]):
        delay.append(dict(dll=d.dll.decode(),functions=[x.name.decode() if x.name else 'ordinal:%d'%x.ordinal for x in d.imports]))
    config=getattr(pe,'DIRECTORY_ENTRY_LOAD_CONFIG',None)
    return dict(sha256=sha,machine=image.machine,image_base=image.image_base,image_size=image.image_size,
                entry=dict(rva=image.entry_rva,sha256=sha,evidence='PE AddressOfEntryPoint',confidence='high metadata; startup behavior unverified'),
                versions=versions,sections=image.sections,directories=image.directories,exports=exports,
                export_count=len(exports),imports=image.imports(),dependencies=dependencies,tls=tls,delay_imports=delay,
                relocation_types=dict(relocations),load_configuration=config.struct.dump_dict() if config else None,
                manifests=manifests,warnings=pe.get_warnings(),
                dependency_limit='Literal entries audited against imports_init/kernel32_wide registration sources. Handler candidate is implementation presence, not behavior verification; argument counts do not establish ABI. Dynamic requests remain obligations.')


def prepare(args):
    prior.check_kit()
    if args.work.exists(): raise ValueError('Comparison work root exists')
    descriptor=json.loads(args.candidate.read_text())
    data=(ROOT/descriptor['path']).read_bytes()
    image=checked_candidate(data,descriptor)
    saved=json.loads(args.ancestry.read_text())
    prior.verify_baseline(saved['baseline']); verify_files(saved['parent_evidence'])
    candidate=metadata(data)
    crossover=metadata((prior.WORK/'original/msvcr90.dll').read_bytes())
    blur=metadata((ROOT/'local/recomp-blur/original/Blur.exe').read_bytes())
    requested=next(g['functions'] for g in blur['imports'] if g['dll'].lower()=='msvcr90.dll')
    by_name={e['name']:e for e in candidate['exports']};old={e['name']:e for e in crossover['exports']}
    missing=sorted(set(requested)-by_name.keys())
    bindings=[dict(import_name=n,**by_name[n]) for n in requested if n in by_name]
    differences=[dict(name=n,microsoft=by_name[n]['kind'],crossover=old[n]['kind']) for n in requested if n in by_name and n in old and by_name[n]['kind']!=old[n]['kind']]
    if missing: raise ValueError('Missing Blur exports: '+str(missing))
    mapping=guest_map(image.image_base,image.image_size)
    args.work.mkdir();original=args.work/'original';original.mkdir()
    (original/'msvcr90.dll').write_bytes(data)
    fixture=prior.WORK/'original/crt-probe.exe'
    (original/'crt-probe.exe').write_bytes(fixture.read_bytes())
    fi=prior.CRTImage(fixture.read_bytes())
    if fi.image_base!=0x400000 or fi.image_size>0x10000: raise ValueError('Fixture map mismatch')
    config=(prior.SOURCE/'game.toml.in').read_text().replace('@EXE_SHA256@',prior.digest(fixture)).replace('@EXE_ENTRY@',hex(fi.image_base+fi.entry_rva)).replace('@DLL_SHA256@',descriptor['sha256'])
    config=config.replace('guest_size = 0x10100000','guest_size = '+hex(mapping['arena_size'])).replace('base = 0x10000000','base = '+hex(image.image_base)).replace('size = 0xa9000','size = '+hex(image.image_size))
    roots=sorted({image.image_base+image.entry_rva}|{image.image_base+e['rva'] for e in candidate['exports'] if e['kind']=='code'})
    config=config.replace('entry_points = [0x1000d140]','entry_points = ['+', '.join(hex(v) for v in roots)+']')
    (args.work/'game.toml').write_text(config);(args.work/'globals.toml').write_text('# Translation-only synthetic fixture.\n[globals]\n')
    sys.path.insert(0,str(KIT/'tools'));import game_config;game_config.load(args.work)
    sources=[Path(__file__).resolve(),Path(prior.__file__).resolve(),ROOT/'tools/ghidra/ExportPinnedRecompListings.java',prior.SOURCE/'game.toml.in']
    environment=__import__('importlib.metadata',fromlist=['version'])
    manifest=dict(kit_commit=prior.PIN,candidate=descriptor,baseline=saved['baseline'],ancestry=str(args.ancestry.relative_to(ROOT)),
                  parent_evidence=saved['parent_evidence'],inputs=prior.fingerprint(sources+[args.candidate,args.ancestry,ROOT/descriptor['path']]+list(original.iterdir())+[args.work/'game.toml',args.work/'globals.toml']),
                  python=sys.version,interpreter=sys.executable,packages={n:environment.version(n) for n in ('pefile','capstone','unicorn','tomli','pytest','ninja')},
                  runtime_registry_sources=prior.fingerprint(list((KIT/'runtime').glob('*.cpp'))),execution_authorized=False)
    prior.write_report(args.work/'manifest.json',manifest)
    prior.write_report(args.out,dict(manifest=manifest,microsoft=candidate,crossover=crossover,blur_manifests=blur['manifests'],
                                    blur_import_count=len(requested),missing_blur_exports=missing,blur_bindings=bindings,classification_differences=differences,
                                    guest_map=mapping,translation_roots=roots,dll_executed=False,host_built=False))


def verified(args):
    prior.check_kit()
    manifest=json.loads((args.work/'manifest.json').read_text())
    if manifest['kit_commit']!=prior.PIN or manifest['execution_authorized'] is not False: raise ValueError('Invalid comparison manifest')
    prior.verify_baseline(manifest['baseline'])
    verify_files(manifest['inputs']);verify_files(manifest['parent_evidence']);verify_files(manifest['runtime_registry_sources'])
    checked_candidate((args.work/'original/msvcr90.dll').read_bytes(),manifest['candidate'])
    return manifest


def export(args):
    manifest=verified(args);analysis=args.work/'analysis';project=args.work/'ghidra'
    if analysis.exists() or project.exists(): raise ValueError('Export attempt already exists')
    analysis.mkdir();project.mkdir()
    home=args.ghidra_home;java=args.java_home
    env=dict(os.environ,JAVA_HOME=str(java),MAXMEM='4G');env['PATH']=str(java/'bin')+os.pathsep+env.get('PATH','')
    step=run_bounded([home/'support/analyzeHeadless',project,'msvc90-comparison','-import',args.work/'original/msvcr90.dll',
                      '-analysisTimeoutPerFile','840','-max-cpu','4','-scriptPath',ROOT/'tools/ghidra','-postScript',
                      'ExportPinnedRecompListings.java',analysis,manifest['candidate']['sha256']],args.out.with_suffix('.log'),900,env)
    summary=analysis/'msvcr90.dll/summary.txt'
    success=step['exit_code']==0 and summary.is_file() and 'sha256='+manifest['candidate']['sha256'] in summary.read_text()
    log=(ROOT/step['log']).read_text()
    if 'analysis timed out' in log.lower() or 'analysis timeout' in log.lower(): success=False
    report=dict(kit_commit=prior.PIN,candidate_sha256=manifest['candidate']['sha256'],step=step,success=success,
                ghidra_properties_sha256=prior.digest(home/'Ghidra/application.properties'),java=str(java),dll_executed=False)
    if success:
        report['listing_inputs']=prior.fingerprint([p for p in analysis.rglob('*') if p.is_file()])
        starts={int(p.stem,16) for p in (analysis/'msvcr90.dll/functions').glob('*.asm')}
        report['entry_listing_present']=manifest['candidate']['image_base']+manifest['candidate']['entry_rva'] in starts
    verified(args);prior.write_report(args.out,report)
    return 0 if success else 1


def strict_gate(code, stats, root_gaps):
    if code!=0 or not stats or root_gaps: return False
    keys=('failures','unmodelled','unmodelled_allowed','table_gaps_allowed','recover_errors','table_gaps','table_sites_undecoded')
    if not all(k in stats for k in keys): return False
    if stats.get('functions_total') != stats.get('functions_ok'): return False
    return not any(stats.get(k) for k in keys)


def translate(args):
    manifest=verified(args);exp=json.loads(args.export_report.read_text())
    if not exp.get('success') or exp['candidate_sha256']!=manifest['candidate']['sha256'] or exp['kit_commit']!=prior.PIN: raise ValueError('Pinned successful export required')
    verify_files(exp['listing_inputs'])
    out=args.work/'strict';upstream=args.out.with_name(args.out.stem+'-upstream.json')
    if out.exists() or upstream.exists(): raise ValueError('Translation attempt already exists')
    step=run_bounded([sys.executable,KIT/'tools/recomp/translate.py','--game',args.work,'--module','msvcr90','--out',out,'--report',upstream],args.out.with_suffix('.log'),120)
    stats=json.loads(upstream.read_text()) if upstream.is_file() else None
    log=(ROOT/step['log']).read_text()
    image=prior.CRTImage((args.work/'original/msvcr90.dll').read_bytes())
    roots={image.image_base+image.entry_rva}|{image.image_base+e['rva'] for e in image.exports() if e['kind']=='code'}
    table=out/'table.c'
    entries=set()
    if table.is_file():
        match=re.search(r'func_addrs\[\] = \{(.*?)\};',table.read_text(),re.S)
        if match: entries={int(v,16) for v in re.findall(r'0x([0-9a-fA-F]+)',match.group(1))}
    root_gaps=sorted(roots-entries)
    report=dict(kit_commit=prior.PIN,candidate_sha256=manifest['candidate']['sha256'],step=step,strict=True,
                export_report_sha256=prior.digest(args.export_report),statistics=stats,
                diagnostics=[l for l in log.splitlines() if any(s in l for s in ('failed:','dispatches to','jump tables:','recovery errors','could not be modelled','TranslateError','FAILED','unmodelled'))],
                translation_gate=strict_gate(step['exit_code'],stats,root_gaps),root_dispatch_gaps=root_gaps,expected_code_roots=len(roots),dll_executed=False,host_built=False,
                notice='Exit status alone cannot pass; inspect complete statistics and generated entry/export dispatch coverage at checkpoint.')
    verified(args);prior.write_report(args.out,report)
    return 0 if step['exit_code']==0 else 1


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('prepare','export','translate'))
    p.add_argument('--work',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--candidate',type=Path);p.add_argument('--ancestry',type=Path)
    p.add_argument('--export-report',type=Path)
    p.add_argument('--ghidra-home',type=Path,default=ROOT/'local/tools/ghidra_12.1.4_PUBLIC')
    p.add_argument('--java-home',type=Path,default=ROOT/'local/tools/jdk-21.0.12.1+1/Contents/Home')
    a=p.parse_args();a.work=prior.safe_output(a.work);a.out=prior.safe_output(a.out)
    if a.out.exists(): p.error('Report exists; preserve evidence')
    for name in ('candidate','ancestry','export_report'):
        value=getattr(a,name)
        if value: setattr(a,name,prior.safe_output(value))
    try: return {'prepare':prepare,'export':export,'translate':translate}[a.action](a) or 0
    except (OSError,ValueError,subprocess.SubprocessError) as e: p.exit(1,'Comparison refused/failed: %s\n'%e)

if __name__=='__main__': sys.exit(main())
