"""Download pinned, project-local build dependencies. No global installations."""
from pathlib import Path
import hashlib, json, urllib.request, zipfile, tarfile, shutil, sys, subprocess
sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / '.tools'
CACHE = TOOLS / 'downloads'
HEADERS = {'User-Agent': 'E-ShuRu-development/0.1'}

def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=120)

def download(url, name, digest=None):
    lock_path = ROOT/'third_party/dependencies.lock.json'
    if not digest and lock_path.exists():
        digest = next((r['sha256'] for r in json.loads(lock_path.read_text(encoding='utf-8')) if r.get('url') == url), None)
    CACHE.mkdir(parents=True, exist_ok=True)
    p = CACHE / name
    if not p.exists():
        print('Downloading', name, flush=True)
        with get(url) as r, p.with_suffix(p.suffix+'.part').open('wb') as w:
            shutil.copyfileobj(r, w)
        p.with_suffix(p.suffix+'.part').replace(p)
    actual = hashlib.sha256(p.read_bytes()).hexdigest()
    if digest and actual != digest.removeprefix('sha256:'):
        raise RuntimeError('Checksum mismatch: '+name)
    return p, actual

def extract(p, target):
    target.mkdir(parents=True, exist_ok=True)
    if p.suffix == '.zip':
        with zipfile.ZipFile(p) as z:
            for n in z.namelist():
                if not (target/n).resolve().is_relative_to(target.resolve()):
                    raise RuntimeError('Unsafe archive member')
            z.extractall(target)
    elif p.suffix == '.7z':
        listing=subprocess.check_output(['tar','-tf',str(p)],encoding='utf-8')
        for n in listing.splitlines():
            if not (target/n).resolve().is_relative_to(target.resolve()):raise RuntimeError('Unsafe archive member')
        subprocess.run(['tar','-xf',str(p),'-C',str(target)],check=True)
    else:
        with tarfile.open(p) as t:
            t.extractall(target, filter='data')

def main():
    records=[]
    if '--toolchain' in sys.argv or len(sys.argv)==1:
        url='https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip'
        archive, sha=download(url, 'llvm-mingw.zip', 'e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666')
        if not (TOOLS/'llvm-mingw-20260922-ucrt-x86_64/bin/clang++.exe').exists(): extract(archive, TOOLS)
        records.append({'name':'llvm-mingw','version':'20260922','url':url,'sha256':sha})
    if '--deps' in sys.argv or len(sys.argv)==1:
        release=json.load(get('https://api.github.com/repos/rime/librime/releases/tags/1.17.0'))
        asset=next(a for a in release['assets'] if a['name'].endswith('Windows-clang-x64.7z'))
        archive,sha=download(asset['browser_download_url'],asset['name'],asset.get('digest'))
        dest=TOOLS/'rime-clang'
        if not dest.exists():extract(archive,dest)
        records.append({'name':'librime','version':'1.17.0','url':asset['browser_download_url'],'sha256':sha})
        third=ROOT/'third_party';third.mkdir(exist_ok=True)
        for name,url in [('rime_api.h','https://raw.githubusercontent.com/rime/librime/33e7814/src/rime_api.h'),('rime-LICENSE','https://raw.githubusercontent.com/rime/librime/33e7814/LICENSE'),('json.hpp','https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp'),('json-LICENSE','https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT')]:
            a,s=download(url,name);shutil.copyfile(a,third/name);records.append({'name':name,'url':url,'sha256':s})
        data=ROOT/'data';data.mkdir(exist_ok=True)
        base='https://raw.githubusercontent.com/rime/rime-pinyin-simp/0c6861ef7420ee780270ca6d993d18d4101049d0/'
        for name,target,digest in [('pinyin_simp.dict.yaml',data/'pinyin_simp.dict.yaml','e341598343a0f0f2035bb1aafc34a7f3bb7887deeecb3f60796262aaa2983e6b'),('LICENSE',third/'pinyin-simp-LICENSE','cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30')]:
            a,s=download(base+name,'pinyin-'+name,digest);shutil.copyfile(a,target);records.append({'name':'pinyin-'+name,'url':base+name,'sha256':s})
        if not (TOOLS/'python/cmake/data/bin/cmake.exe').exists() or not (TOOLS/'python/bin/ninja.exe').exists():
            subprocess.run([sys.executable,'-m','pip','install','--target',str(TOOLS/'python'),'cmake==4.4.4','ninja==1.13.2','--disable-pip-version-check'],check=True)
    manifest=TOOLS/'dependencies.json'
    old=json.loads(manifest.read_text()) if manifest.exists() else []
    by_name={r['name']:r for r in old+records}
    manifest.write_text(json.dumps(list(by_name.values()),indent=2),encoding='utf-8')
    (ROOT/'third_party/dependencies.lock.json').write_text(json.dumps(list(by_name.values()),indent=2),encoding='utf-8')
    print('Dependencies ready',flush=True)

if __name__=='__main__':main()
