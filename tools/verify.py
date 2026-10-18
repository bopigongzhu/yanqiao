"""Run local checks serially; never register a system IME or send cloud requests."""
from pathlib import Path
import datetime, hashlib, json, shutil, subprocess, sys, os, uuid
sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT/'build/0.2.5/x64/bin'
REPORT = ROOT/'verification/native-0.2.5'

def main():
    os.environ['ES_SHURU_TEST_SCOPE'] = uuid.uuid4().hex
    REPORT.mkdir(parents=True, exist_ok=True)
    commands = [
        ('installer-diagnosis', ['powershell.exe', '-NoProfile', '-File', ROOT/'installer/diagnose.ps1', '-RequirePackage']),
        ('ctest', [ROOT/'.tools/python/cmake/data/bin/ctest.exe', '--test-dir', ROOT/'build/0.2.5/x64', '-V']),
        ('tsf-x86', [ROOT/'build/0.2.5/x86/bin/tsf_smoke.exe']),
        ('integration', [BIN/'integration_tests.exe']),
        ('native-ui', [sys.executable, ROOT/'tools/test_native_ui.py']),
        ('tracking', [sys.executable, ROOT/'tools/test_tracking.py']),
    ]
    results = []
    for name, cmd in commands:
        p = subprocess.run([str(x) for x in cmd], cwd=ROOT, capture_output=True, encoding='utf-8', errors='replace', timeout=90)
        (REPORT/(name+'.txt')).write_text(p.stdout+p.stderr, encoding='utf-8')
        print(name, 'PASS' if p.returncode == 0 else 'FAIL', flush=True)
        results.append({'name':name, 'exit_code':p.returncode})
        if p.returncode:
            print(p.stdout+p.stderr)
            raise SystemExit(p.returncode)
    for name in ['integration-report.json','native-ui-report.json','paging-report.json']:
        shutil.copyfile(BIN/name, REPORT/name)
    files = {}
    for folder in ['src','tests','data']:
        for p in (ROOT/folder).rglob('*'):
            if p.is_file(): files[p.relative_to(ROOT).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    (REPORT/'summary.json').write_text(json.dumps({
        'tested_at':datetime.datetime.now().astimezone().isoformat(),
        'results':results, 'source_sha256':files,
        'scope':'Local real-Rime core, IPC, sandbox and isolated secure TSF lifecycle. No system registration, installed-app compatibility, cloud latency or endurance acceptance.'
    },indent=2),encoding='utf-8')

if __name__=='__main__':main()
