"""Recheck immutable synthetic static-DLL and exact-CRT execution evidence.

This reports scoped behavior; it never authorizes Blur integration or substitutes
for Windows/compiler parity, static TLS, dependency cycles or thread callbacks.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
try:
    from . import recomp_crt_compare as c
except ImportError:
    import recomp_crt_compare as c
p = c.prior
ROOT = p.ROOT
KIT = ROOT / 'local/recomp-kit-static-binding'
CHAIN = ROOT / 'local/recomp-static-chain-r1'
CRT = ROOT / 'local/recomp-crt-msvc90-static-r2'


def checked_run(work, prefix, marker):
    native_path = work / 'run0/host.log'
    reference_path = work / 'reference.log'
    native, reference = native_path.read_text(), reference_path.read_text()
    step = json.loads((work / 'run0/run.json').read_text())
    refstep = json.loads((work / 'reference-run.json').read_text())
    if step['timed_out'] or step['exit_code'] != 1:
        raise ValueError('Expected precise memory-only headless exit result')
    if refstep['timeout'] or refstep['exit'] != 0:
        raise ValueError('Reference did not complete')
    if not re.search(r'^guest exit code:\s+0$', native, re.M):
        raise ValueError('Guest did not exit successfully')
    if 'undeliverable calls: none' not in native or not re.search(
            r'^logging-only \(\d+\), of which called: 0$', native, re.M):
        raise ValueError('Runtime fallback gate failed')
    if marker not in native or marker not in reference:
        raise ValueError('Completion marker absent')
    events = lambda s: [line for line in s.splitlines() if line.startswith(prefix)]
    abi = re.findall(r'crt.guard.dll_abi entry=([0-9a-f]+) raw_esp=([0-9a-f]+) '
                     r'expected=([0-9a-f]+) nonvolatile=(\d+)', native)
    if not abi or any(raw != expected or preserved != '1'
                      for _, raw, expected, preserved in abi):
        raise ValueError('Raw DLL-entry ABI gate failed')
    return native, reference, dict(native_events=events(native), reference_events=events(reference),
        dll_entry_abi_checks=len(abi), native_host_exit=1, native_guest_exit=0,
        evidence=p.fingerprint([native_path, reference_path, work/'run0/run.json',
                                work/'reference-run.json']))


def summarize():
    if subprocess.check_output(['git','-C',str(KIT),'rev-parse','HEAD'],text=True).strip() != p.PIN:
        raise ValueError('Kit pin changed')
    patch = ROOT / 'recomp/patches/msvc90-runtime-static-r1.patch'
    if subprocess.check_output(['git','-C',str(KIT),'diff','--binary','HEAD']) != patch.read_bytes():
        raise ValueError('Static-runtime patch changed')
    native, reference, chain = checked_run(CHAIN, 'slice.', 'slice.exe.complete')
    expected = ['slice.b.attach.static','slice.a.attach.static','slice.exe.start',
                'slice.exe.complete','slice.a.detach.process','slice.b.detach.process']
    if chain['native_events'] != expected or chain['reference_events'] != expected:
        raise ValueError('Static dependency or process detach order differs')
    if chain['dll_entry_abi_checks'] != 4:
        raise ValueError('Expected one attach and detach per synthetic DLL')
    for key in ('main','a','b'):
        if not c.strict_gate(0,json.loads((CHAIN/(key+'-statistics.json')).read_text()),[]):
            raise ValueError('Synthetic translation gate failed')
    accepted_path = ROOT/'local/recomp-crt-msvc90-execution/callback-round1/accepted.json'
    accepted = json.loads(accepted_path.read_text())
    if not accepted['translation_gate'] or not accepted['runtime_callback_dispatch_present']:
        raise ValueError('Reviewed whole-CRT translation required')
    c.verify_files(accepted['generated'])
    for old, digest in accepted['generated'].items():
        emitted = CRT/'build/recomp/gen/aux-msvcr90'/Path(old).name
        if p.digest(emitted) != digest:
            raise ValueError('Accepted CRT generated source changed: '+str(emitted))
    native, reference, crt = checked_run(CRT, 'crt.', 'crt.static.complete 1')
    if not c.strict_gate(0,json.loads((CRT/'statistics.json').read_text()),[]):
        raise ValueError('CRT fixture translation gate failed')
    if crt['dll_entry_abi_checks'] != 2 or 'crt.smoke.complete 1' not in native:
        raise ValueError('Static reference plus dynamic load must attach/detach only once')
    keys = ['crt.HUGE.low','crt.HUGE.high','crt.adjust_fdiv.value','crt.msize32','crt.msize96',
            'crt.locale.initial','crt.locale.C','crt.file.missing.errno',
            'crt.file.binary.complete','crt.static.complete','crt.smoke.complete']
    observations = {}
    for key in keys:
        a = re.search('^'+re.escape(key)+r' (.+)$',native,re.M)
        b = re.search('^'+re.escape(key)+r' (.+)$',reference,re.M)
        if not a or not b or a[1] != b[1]:
            raise ValueError('Observation differs: '+key)
        observations[key] = a[1]
    dll = CRT/'original/msvcr90.dll'
    if p.digest(dll) != 'ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185':
        raise ValueError('Exact Microsoft DLL identity changed')
    exports = {entry['name']:entry for entry in p.CRTImage(dll.read_bytes()).exports()}
    symbols=[]
    for log,base in [(native,0x78520000),(reference,int(re.search(r'^crt.module (0x[0-9a-f]+)',reference,re.M)[1],16))]:
        for name,addr in re.findall(r'^([A-Za-z_][A-Za-z_0-9]*) (0x[0-9a-f]+)$',log,re.M):
            if name in exports:
                if int(addr,16)-base != exports[name]['rva']:
                    raise ValueError('Export RVA mismatch: '+name)
                symbols.append(dict(name=name,executable_sha256=p.digest(dll),rva=hex(exports[name]['rva']),
                    evidence='native' if base==0x78520000 else 'reference',
                    confidence='high: observed resolved address matches exact PE export RVA'))
    testlog = (CRT/'tests-final.log').read_text()
    if '100% tests passed' not in testlog or '3/3' not in testlog:
        raise ValueError('Focused native tests required')
    eh = ROOT/'local/recomp-crt-msvc90-eh-r1/corrected-link.json'
    eh_result=json.loads(eh.read_text())
    if not eh_result['exit'] or 'type_info' not in eh_result['stderr'] or '??_7type_info@@6B@' in exports:
        raise ValueError('Exception compiler-support blocker changed')
    return dict(scope='New static PE loader implementation and synthetic probe results; no Blur CRT replacement',
        kit_pin=p.PIN,patch_sha256=p.digest(patch),chain=chain,crt=crt,observations=observations,
        symbols=symbols,inputs=p.fingerprint([patch,ROOT/'recomp/patches/msvc90-static-binding.patch',
            ROOT/'recomp/dll-probe/probe.c',ROOT/'recomp/dll-probe/crt_static_probe.c',
            ROOT/'recomp/dll-probe/exception_probe.cpp',CRT/'original/crt-probe.exe',dll,
            CRT/'tests-final.log',eh,accepted_path,
            CRT/'build/recomp/pop_headless',CHAIN/'build/recomp/pop_headless',
            CRT/'build/recomp/dll_binding_tests',Path(__file__).resolve()]),
        exception_probe=dict(executed=False,link_exit=eh_result['exit'],
            blocker='Clang MSVC-x86 object requires type_info::vftable; exact MSVCR90 does not export it; matching compiler support object/library not present in local inputs'),
        limitations=['CrossOver reference is not a Windows OS baseline',
            'Cyclic configured guest DLL dependencies and auxiliary static TLS explicitly rejected',
            'Thread attach/detach notifications and full loader-lock/concurrency behavior unverified',
            'Broader FP, NLS, callbacks/errors and exception behavior remain open'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=ROOT/'reports/recomp-static-dll-result.json')
    args=parser.parse_args()
    result=summarize()
    p.write_report(args.out,result)
    print('Static DLL chain and exact CRT load-time-import checks passed; C++ compiler-support gate open.')

if __name__=='__main__':
    main()
