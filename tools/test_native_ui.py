"""Exercise only owned hidden native windows, without registering an input method."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import subprocess, time, json, argparse, os, uuid
os.environ.setdefault('ES_SHURU_TEST_SCOPE', uuid.uuid4().hex)

ROOT=Path(__file__).resolve().parents[1]
args=argparse.ArgumentParser()
args.add_argument('--bin',type=Path,default=ROOT/'build/0.2.5/x64/bin')
args.add_argument('--report',type=Path)
options=args.parse_args()
BIN=options.bin.resolve()
u=C.WinDLL('user32',use_last_error=True)
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=W.LPARAM
ENUM=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
u.EnumWindows.argtypes=[ENUM,W.LPARAM]

def window(pid, cls):
    result=[]
    @ENUM
    def visit(h,_):
        p=W.DWORD();u.GetWindowThreadProcessId(h,C.byref(p));buf=C.create_unicode_buffer(128);u.GetClassNameW(h,buf,128)
        if p.value==pid and buf.value==cls:result.append(h)
        return True
    u.EnumWindows(visit,0)
    return result[0] if result else None

def wait(fn, timeout=5):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        value=fn()
        if value:return value
        time.sleep(.025)
    raise AssertionError('Timed out: '+getattr(fn,'__name__','condition'))

def text(h):
    b=C.create_unicode_buffer(4096)
    u.SendMessageW(h,0xD,len(b),C.addressof(b))
    return b.value

def main():
    processes=[];checks=[]
    try:
        broker=subprocess.Popen([str(BIN/'EShuRu.exe'),'--server','--headless'],creationflags=subprocess.CREATE_NO_WINDOW);processes.append(broker)
        broker_window=wait(lambda:window(broker.pid,'EShuRu.Broker'))
        sandbox=subprocess.Popen([str(BIN/'EShuRu.exe'),'--sandbox','--headless'],creationflags=subprocess.CREATE_NO_WINDOW);processes.append(sandbox)
        sandbox_window=wait(lambda:window(sandbox.pid,'EShuRu.Sandbox'))
        edit=wait(lambda:u.GetDlgItem(sandbox_window,2));time.sleep(.7)
        def key(v):u.PostMessageW(edit,0x100,v,0)
        key(ord('N'));key(ord('I'));key(0x20);time.sleep(.3)
        assert text(edit)=='';checks.append('Real pinyin selection stays in sentence buffer')
        key(0x0D);wait(lambda:text(edit)=='\u4f60');checks.append('Chinese sentence enters native edit control')
        time.sleep(.1);key(ord('H'));key(ord('A'));key(ord('O'));key(0x20);time.sleep(.15);key(0x0D)
        wait(lambda:text(edit)=='\u4f60\u597d');checks.append('Second sentence appends exactly once at caret')
        time.sleep(.1);key(ord('W'));key(ord('O'));time.sleep(.15);key(0x1B);time.sleep(.15)
        assert text(edit)=='\u4f60\u597d';checks.append('Escape cancels uncommitted pinyin')
        key(0x10);u.PostMessageW(edit,0x101,0x10,0);time.sleep(.15);key(ord('A'))
        wait(lambda:text(edit)=='\u4f60\u597da');checks.append('Shift toggles English direct input')
        report={'passed':len(checks),'checks':checks,'scope':'Owned hidden sandbox with real shared Rime. Foreground requirement intentionally bypassed in headless test only. No system registration or API calls.'}
        (options.report or BIN/'native-ui-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
    finally:
        for process in reversed(processes):
            for cls in ['EShuRu.Sandbox','EShuRu.Broker']:
                h=window(process.pid,cls)
                if h:u.PostMessageW(h,0x10,0,0)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=3)

if __name__=='__main__':main()
