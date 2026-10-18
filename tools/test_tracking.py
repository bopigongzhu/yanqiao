"""Owned native edit + broker tracking and optional sustained load. Never sends cloud requests."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import argparse, json, os, subprocess, time, uuid

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--bin', type=Path, default=ROOT/'build/0.2.5/x64/bin')
p.add_argument('--seconds', type=int, default=0)
p.add_argument('--report', type=Path, default=ROOT/'verification/native-0.2.5/tracking.json')
args = p.parse_args()
BIN = args.bin.resolve()
env = os.environ.copy(); env['ES_SHURU_TEST_SCOPE'] = uuid.uuid4().hex
u = C.WinDLL('user32', use_last_error=True)
k = C.WinDLL('kernel32', use_last_error=True)
ps = C.WinDLL('psapi', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [W.HANDLE]
u.SetProcessDpiAwarenessContext(W.HANDLE(-4))
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]; u.SendMessageW.restype = W.LPARAM
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.GetDlgItem.argtypes = [W.HWND, C.c_int]; u.GetDlgItem.restype = W.HWND
u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
ENUM = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
u.EnumWindows.argtypes = [ENUM, W.LPARAM]
MON = C.WINFUNCTYPE(W.BOOL, W.HANDLE, W.HDC, C.POINTER(W.RECT), W.LPARAM)
u.EnumDisplayMonitors.argtypes = [W.HDC, C.c_void_p, MON, W.LPARAM]
k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]; k.OpenProcess.restype = W.HANDLE
k.CloseHandle.argtypes = [W.HANDLE]
k.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4
k.GetProcessHandleCount.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]

class Memory(C.Structure):
    _fields_ = [('cb', W.DWORD), ('faults', W.DWORD)] + [(n, C.c_size_t) for n in
        ['peak_working', 'working', 'peak_paged', 'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private']]
ps.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(Memory), W.DWORD]

def snapshot(handle):
    memory = Memory(); memory.cb = C.sizeof(memory)
    assert ps.GetProcessMemoryInfo(handle, C.byref(memory), memory.cb)
    times = [W.FILETIME() for _ in range(4)]
    assert k.GetProcessTimes(handle, *[C.byref(t) for t in times])
    ticks = lambda t: (t.dwHighDateTime << 32) + t.dwLowDateTime
    handles = W.DWORD(); assert k.GetProcessHandleCount(handle, C.byref(handles))
    return {'private_MiB': memory.private/1048576, 'working_set_MiB': memory.working/1048576,
            'handles': handles.value, 'cpu_seconds': (ticks(times[2])+ticks(times[3]))/1e7}

def window(pid, cls):
    found = []
    @ENUM
    def visit(h, _):
        ident = W.DWORD(); u.GetWindowThreadProcessId(h, C.byref(ident))
        name = C.create_unicode_buffer(128); u.GetClassNameW(h, name, 128)
        if ident.value == pid and name.value == cls: found.append(h)
        return True
    u.EnumWindows(visit, 0)
    return found[0] if found else None

def wait(fn, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = fn()
        if value: return value
        time.sleep(.02)
    raise AssertionError('Timed out: ' + str(fn))

def rect(h):
    r = W.RECT(); assert u.GetWindowRect(h, C.byref(r))
    return [r.left, r.top, r.right, r.bottom]

def text(h):
    out = C.create_unicode_buffer(16384)
    u.SendMessageW(h, 0xD, len(out), C.addressof(out)); return out.value

def main():
    children = []; handles = []; checks = []; samples = []
    try:
        for switches in [('--server', '--headless'), ('--sandbox', '--headless')]:
            children.append(subprocess.Popen([str(BIN/'EShuRu.exe'), *switches], env=env,
                                             creationflags=subprocess.CREATE_NO_WINDOW))
        broker, host = children
        panel = wait(lambda: window(broker.pid, 'EShuRu.Panel'))
        native = wait(lambda: window(host.pid, 'EShuRu.Sandbox'))
        edit = wait(lambda: u.GetDlgItem(native, 2)); time.sleep(.8)
        seed = ''.join('Synthetic line %03d\r\n' % i for i in range(100))
        def seed_edit():
            value = C.create_unicode_buffer(seed)
            u.SendMessageW(edit, 0xC, 0, C.addressof(value))
            u.SendMessageW(edit, 0xB1, 0, 0); u.SendMessageW(edit, 0xB7, 0, 0)
        def key(v): u.PostMessageW(edit, 0x100, v, 0)
        def move(x, y): u.SetWindowPos(native, None, x, y, 860, 580, 0x14)
        seed_edit(); move(100, 100)
        key(ord('N')); key(ord('I')); key(0x20); time.sleep(.25)
        first = rect(panel)
        move(180, 140)
        wait(lambda: rect(panel)[0] == first[0]+80 and rect(panel)[1] == first[1]+40)
        checks.append('Panel follows parent window movement without another key')
        u.SendMessageW(edit, 0xB6, 0, 20); time.sleep(.25)
        u.SendMessageW(edit, 0xB6, 0, -20); time.sleep(.25)
        assert text(edit) == seed
        checks.append('Scrolling out and back does not prematurely insert or lose selected draft')
        monitors = []
        @MON
        def monitor(h, dc, r, data):
            monitors.append([r.contents.left, r.contents.top, r.contents.right, r.contents.bottom]); return True
        u.EnumDisplayMonitors(None, None, monitor, 0)
        for m in monitors:
            move(m[0]+50, m[1]+50)
            wait(lambda: m[0] <= rect(panel)[0] < m[2])
        checks.append('Active draft follows across attached monitors')
        key(0x0D); wait(lambda: text(edit) == '\u4f60'+seed)
        checks.append('Moved and scrolled draft commits once at original selection')
        time.sleep(.1); seed_edit(); key(ord('N')); key(ord('I')); time.sleep(.2)
        u.SendMessageW(edit, 0xB1, 5, 5); time.sleep(.25)
        assert text(edit) == seed
        key(0x0D); time.sleep(.15)
        assert '\u4f60' not in text(edit)
        checks.append('Changed host selection cancels old draft')
        if args.seconds:
            seed_edit(); key(ord('N')); key(ord('I')); time.sleep(.2)
            for child in children:
                handle = k.OpenProcess(0x410, False, child.pid); assert handle; handles.append(handle)
            start = time.monotonic(); next_sample = 0; cycles = 0
            while time.monotonic()-start < args.seconds:
                m = monitors[(cycles//40) % len(monitors)]
                move(m[0]+50+(cycles % 5)*10, m[1]+50+(cycles % 3)*10)
                # Exercise actual Direct2D painting on the owned hidden panel.
                u.SendMessageW(panel, 0xF, 0, 0)
                if cycles % 20 == 0:
                    key(0x1B); time.sleep(.025); key(ord('N')); key(ord('I'))
                elapsed = time.monotonic()-start
                if elapsed >= next_sample:
                    samples.append({'seconds': elapsed, 'broker': snapshot(handles[0]), 'host': snapshot(handles[1])})
                    print(json.dumps({'progress_seconds': round(elapsed), 'broker': samples[-1]['broker']}), flush=True)
                    next_sample += 10
                assert broker.poll() is None and host.poll() is None
                cycles += 1; time.sleep(.1)
            samples.append({'seconds': time.monotonic()-start, 'broker': snapshot(handles[0]), 'host': snapshot(handles[1])})
            checks.append('Sustained movement, repaint and composition replacement complete')
        report = {'passed': len(checks), 'checks': checks, 'requested_seconds': args.seconds, 'samples': samples,
                  'logical_processors': os.cpu_count(), 'monitors': monitors,
                  'scope': 'Owned hidden native edit and broker, real Rime and Direct2D; no API calls or third-party host TSF automation. Sustained sample is not eight-hour endurance.'}
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps({'passed': len(checks), 'report': str(args.report)}), flush=True)
    finally:
        for handle in handles: k.CloseHandle(handle)
        for child in reversed(children):
            for cls in ['EShuRu.Sandbox', 'EShuRu.Broker']:
                h = window(child.pid, cls)
                if h: u.PostMessageW(h, 0x10, 0, 0)
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.terminate(); child.wait(timeout=3)

if __name__ == '__main__': main()
