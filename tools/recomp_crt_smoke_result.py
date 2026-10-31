"""Recheck and summarize preserved native CRT smoke evidence; never executes it."""
import json
from pathlib import Path
import re
import sys
try:
    from . import recomp_crt_lifetime as l
except ImportError:
    import recomp_crt_lifetime as l
p,c,r=l.p,l.c,l.r
ROOT,WORK=r.ROOT,r.WORK


def main():
    current=l.identity(4)
    out=WORK/'lifetime-r4';built=json.loads((out/'build.json').read_text())
    if built['identity']!=current or p.digest(l.h.BUILD/'recomp/pop_headless')!=built['binary_sha256']:
        raise ValueError('Native lifetime build identity changed')
    tests=json.loads((out/'tests.json').read_text())
    if tests['identity']!=current or tests['step']['exit_code']:raise ValueError('Native tests not passed')
    image=p.CRTImage((WORK/'original/msvcr90.dll').read_bytes())
    exports={e['name']:e for e in image.exports()}
    runs=[]
    for index in range(3):
        path=out/('run%d'%index);raw=json.loads((path/'result.json').read_text());log=(path/'host.log').read_text()
        if raw['identity']!=current or raw['step']['timed_out'] or raw['step']['exit_code']!=1:raise ValueError('Unexpected host termination')
        if 'crt.smoke.complete 1' not in log or not re.search(r'guest exit code:\s+0',log):raise ValueError('Guest did not complete')
        if not re.search(r'logging-only \(\d+\), of which called: 0',log):raise ValueError('Unimplemented service called')
        for reason in (0,1):
            if log.count('crt.guard.dll_entry.end msvcr90.dll reason=%d result=00000001'%reason)!=1:raise ValueError('Actual attach/detach missing')
        abi=re.findall(r'crt.guard.dll_abi entry=([0-9a-f]+) raw_esp=([0-9a-f]+) expected=([0-9a-f]+) nonvolatile=(\d+)',log)
        if len(abi)!=2 or any(int(e,16)!=image.image_base+image.entry_rva or actual!=expected or saved!='1' for e,actual,expected,saved in abi):raise ValueError('Raw DLL-entry ABI failed')
        if re.search(r'crt.guard.(unknown_call|interpreter|unimplemented_import|heap_corruption|heap_owner_corruption|unsupported_heap_flags|invalid_)',log):raise ValueError('Strict execution guard failed')
        snapshots=re.findall(r'crt.guard.lifetime phase=(\w+) reason=(\d+) blocks=(\d+) bytes=(\d+) handles=(\d+) tls=(\d+) owned=(\d+)',log)
        if len(snapshots)!=4:raise ValueError('Missing lifetime observations')
        before,after=snapshots[0],snapshots[-1]
        if before[0:2]!=('before','1') or after[0:2]!=('after','0') or before[4:]!=after[4:] or after[6]!='0':raise ValueError('Heap handles, TLS or owned blocks leaked')
        if int(after[2])-int(before[2])!=2 or int(after[3])-int(before[3])!=78:raise ValueError('Unexplained process memory delta')
        resolved=[]
        for name in ('malloc','realloc','free','_msize','_acmdln','_HUGE','_adjust_fdiv'):
            match=re.search(r'^'+re.escape(name)+r' 0x([0-9a-f]+)$',log,re.M)
            if not match or int(match[1],16)!=image.image_base+exports[name]['rva']:raise ValueError('Resolved export mismatch: '+name)
            resolved.append(dict(name=name,rva=exports[name]['rva'],candidate_sha256=p.digest(WORK/'original/msvcr90.dll'),evidence='PE export table and observed GetProcAddress result',confidence='high target identity; general ABI unverified'))
        runs.append(dict(index=index,guest_exit=0,expected_headless_no_frames_exit=1,native_allocation_smoke_passed=True,raw_dll_entry_abi_preserved=True,owned_allocations_after_detach=0,heap_handles_and_tls_balanced=True,process_cache_delta=dict(blocks=2,requested_bytes=78,interpretation='Matches cached ANSI/UTF-16 command lines, 26+52 bytes; same-process reload growth not tested.'),resolved_exports=resolved,log_sha256=p.digest(path/'host.log')))
    oracle=ROOT/'local/recomp-crt-msvc90-oracle'
    redirect=(oracle/'reference-diagnostic.log').read_text()
    absolute=(oracle/'reference-absolute.log').read_text()
    if '9.0.30729.6161' not in redirect or 'status=c0000135' not in redirect:raise ValueError('Oracle redirect evidence missing')
    if 'CRTProbe\\\\msvcr90.dll' not in absolute or 'at 7A700000: native' not in absolute:raise ValueError('Pinned absolute DLL mapping not evidenced')
    selected=oracle/'bottle-unrestricted/drive_c/CRTProbe/msvcr90.dll'
    if p.digest(selected)!=p.digest(WORK/'original/msvcr90.dll'):raise ValueError('Oracle DLL bytes differ')
    paths=[ROOT/'recomp/patches/msvc90-runtime-r4.patch',ROOT/'recomp/crt-probe/msvc90-data-inventory.json',ROOT/'recomp/crt-probe/src/msvc90_probe.c',Path(__file__).resolve(),ROOT/'reports/recomp-crt-msvc90-accepted-final.json',out/'build.json',out/'tests.json']
    paths+=list(out.glob('run*/result.json'))+list(oracle.glob('reference*.log'))+[oracle/'inputs.json',oracle/'bottle-inputs.json',oracle/'absolute-probe/manifest.json']
    result=dict(date='2026-10-15',candidate_sha256=p.digest(WORK/'original/msvcr90.dll'),identity=current,translation_gate_passed_with_exact_inventory=True,native_allocation_smoke_passed=True,runs=runs,
                independent_abi_parity_established=False,crt_boundary_gate_passed=False,static_blur_integration_attempted=False,
                blocker=dict(kind='Independent matching VC90 baseline unavailable in tested CrossOver configuration',name_load='Wine activation context redirects 9.0.21022.8 to 9.0.30729.6161; native-only load fails c0000135.',absolute_load='Pinned Microsoft DLL maps natively at 0x7a700000, but initialization did not return before isolated bottle termination.',cause='Not established. No claim that this proves DLL incompatibility or validates translated behavior.',cleanup='Only the experiment bottle wineserver was terminated; existing bottles and game were not used.'),
                limitations=['Synthetic dynamic allocation path only; no FILE/locale/FP/C++ EH or static binding acceptance.','OS heap supports observed growable heap calls/flags; fixed heaps, in-place-only reallocation and generated allocation exceptions explicitly unsupported.','Environment block ownership tested against the current fixed guest environment; full environment/NLS behavior unresolved.','Three fresh processes, one attach/detach each; repeated attach in the same process untested.'],
                artifacts=p.fingerprint(paths))
    p.write_report(ROOT/'reports/recomp-crt-msvc90-smoke-result.json',result)
    print('Three native allocation/lifetime runs accepted; independent ABI parity remains blocked.')

if __name__=='__main__':main()
