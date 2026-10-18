from pathlib import Path
import subprocess, shutil, sys, json
from build_readings import build as build_readings
sys.stdout.reconfigure(encoding='utf-8')

ROOT=Path(__file__).resolve().parents[1]
TOOLS=ROOT/'.tools'
ARCH='x86_64'
LLVM=TOOLS/'llvm-mingw-20260922-ucrt-x86_64/bin'
CMAKE=TOOLS/'python/cmake/data/bin/cmake.exe'
NINJA=TOOLS/'python/bin/ninja.exe'
BUILD=ROOT/'build/0.2.5/x64'

def run(args, **kw):
    print('>', ' '.join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args],check=True,**kw)

def main():
    run([CMAKE,'-S',ROOT,'-B',BUILD,'-G','Ninja',f'-DCMAKE_MAKE_PROGRAM={NINJA}',f'-DCMAKE_CXX_COMPILER={LLVM / (ARCH+"-w64-mingw32-clang++.exe")}', '-DCMAKE_BUILD_TYPE=Release'])
    run([CMAKE,'--build',BUILD,'--parallel','2'])
    out=BUILD/'bin'
    shutil.copyfile(TOOLS/'rime-clang/dist/lib/rime.dll',out/'rime.dll')
    shutil.copytree(ROOT/'data',out/'data',dirs_exist_ok=True)
    build_readings(ROOT/'data/pinyin_simp.dict.yaml',out/'data/word-readings.idx')
    license_dir=out/'licenses';license_dir.mkdir(exist_ok=True)
    for p in (ROOT/'third_party').glob('*LICENSE'):shutil.copyfile(p,license_dir/p.name)
    shutil.copytree(ROOT/'third_party/licenses',license_dir,dirs_exist_ok=True)
    for name in ['THIRD_PARTY_NOTICES.md','dependencies.lock.json']:
        shutil.copyfile(ROOT/'third_party'/name,license_dir/name)
    build32=ROOT/'build/0.2.5/x86'
    run([CMAKE,'-S',ROOT,'-B',build32,'-G','Ninja',f'-DCMAKE_MAKE_PROGRAM={NINJA}',f'-DCMAKE_CXX_COMPILER={LLVM / "i686-w64-mingw32-clang++.exe"}', '-DCMAKE_BUILD_TYPE=Release'])
    run([CMAKE,'--build',build32,'--target','EShuRuTip','tsf_smoke','--parallel','2'])
    (out/'x86').mkdir(exist_ok=True)
    shutil.copyfile(build32/'bin/EShuRuTip.dll',out/'x86/EShuRuTip.dll')
    run([out/'EShuRu.exe','--deploy'],cwd=out)
    if '--no-tests' not in sys.argv:
        run([TOOLS/'python/cmake/data/bin/ctest.exe','--test-dir',BUILD,'--output-on-failure'])
        run([build32/'bin/tsf_smoke.exe'])

if __name__=='__main__':main()
