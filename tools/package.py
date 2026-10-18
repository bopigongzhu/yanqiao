"""Assemble an allowlisted local alpha package. Does not install or publish it."""
from pathlib import Path
import hashlib, json, shutil, sys, zipfile
sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT/'build/0.2.5/x64/bin'
DEST = ROOT/'dist/EShuRu-0.2.5-alpha'

def main():
    if DEST.exists():
        raise SystemExit('Package directory already exists. Preserve or rename it before creating another package: '+str(DEST))
    required = ['EShuRu.exe','EShuRuMaintenance.exe','EShuRuTip.dll','rime.dll','x86/EShuRuTip.dll']
    for name in required:
        if not (BIN/name).is_file():raise SystemExit('Build first; missing '+name)
    summary = ROOT/'verification/native-0.2.5/summary.json'
    if not summary.is_file():raise SystemExit('Run python tools/verify.py first.')
    status = json.loads(summary.read_text(encoding='utf-8'))
    if any(r['exit_code'] for r in status['results']):raise SystemExit('Verification did not pass.')
    for name,digest in status['source_sha256'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:
            raise SystemExit('Source changed since verification: '+name)
    DEST.mkdir(parents=True)
    for name in required:
        target=DEST/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(BIN/name,target)
    for folder in ['data','licenses']:
        shutil.copytree(BIN/folder,DEST/folder)
    (DEST/'installer').mkdir()
    for name in ['install.ps1','uninstall.ps1','diagnose.ps1']:
        shutil.copyfile(ROOT/'installer'/name,DEST/'installer'/name)
    shutil.copyfile(ROOT/'installer/PACKAGE_README.md',DEST/'README.md')
    for name,argument in [('Start-Trial.cmd','--sandbox'),('Settings.cmd','--settings')]:
        (DEST/name).write_bytes(('@echo off\r\nstart "" "%~dp0EShuRu.exe" '+argument+'\r\n').encode('ascii'))
    files={p.relative_to(DEST).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
           for p in sorted(DEST.rglob('*')) if p.is_file()}
    manifest={'version':'0.2.5-alpha','scope':'Local development only; no system or live-API acceptance. See README and third-party notices.',
              'files_sha256':files}
    (DEST/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    archive=DEST.parent/(DEST.name+'.zip')
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for p in sorted(DEST.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(DEST.parent).as_posix())
    (archive.parent/(archive.name+'.sha256')).write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n',encoding='ascii')
    print(json.dumps({'directory':str(DEST),'zip':str(archive),'files':len(files)+1,
                      'zip_MiB':round(archive.stat().st_size/1048576,2)},ensure_ascii=False))

if __name__=='__main__':main()
