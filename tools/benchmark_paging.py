"""Compare existing binaries using identical isolated IPC paging; no keys, cloud or live host input."""
from pathlib import Path
import argparse, ctypes as C, hashlib, json, os, re, struct, subprocess, time, uuid, winreg
from ctypes import wintypes as W

p = argparse.ArgumentParser()
p.add_argument('--bin', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
p.add_argument('--rounds', type=int, default=3)
args = p.parse_args()
assert 1 <= args.rounds <= 20
root = args.bin.resolve()
token = uuid.uuid4().hex
env = os.environ.copy(); env['ES_SHURU_TEST_SCOPE'] = token
sid = re.search(rb'S-1-\d+(?:-\d+)+', subprocess.check_output(
    ['whoami.exe', '/user', '/fo', 'csv', '/nh'], creationflags=subprocess.CREATE_NO_WINDOW)).group().decode()
scope = sid + '.test.' + token
k = C.WinDLL('kernel32', use_last_error=True)
u = C.WinDLL('user32', use_last_error=True)
ps = C.WinDLL('psapi', use_last_error=True)
k.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
k.CreateFileW.restype = W.HANDLE
k.CloseHandle.argtypes = [W.HANDLE]
k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]; k.OpenProcess.restype = W.HANDLE
k.ProcessIdToSessionId.argtypes = [W.DWORD, C.POINTER(W.DWORD)]
k.GetNamedPipeServerProcessId.argtypes = [W.HANDLE, C.POINTER(W.ULONG)]
k.PeekNamedPipe.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.c_void_p, C.POINTER(W.DWORD), C.c_void_p]
k.ReadFile.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]
k.WriteFile.argtypes = k.ReadFile.argtypes
k.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4
k.GetProcessHandleCount.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
ENUM = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
u.EnumWindows.argtypes = [ENUM, W.LPARAM]

class Memory(C.Structure):
    _fields_ = [('cb', W.DWORD), ('faults', W.DWORD)] + [(n, C.c_size_t) for n in
        ['peak_working', 'working', 'peak_paged', 'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private']]
ps.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(Memory), W.DWORD]

def snapshot(process):
    memory = Memory(); memory.cb = C.sizeof(memory)
    assert ps.GetProcessMemoryInfo(process, C.byref(memory), memory.cb)
    clocks = [W.FILETIME() for _ in range(4)]
    assert k.GetProcessTimes(process, *[C.byref(t) for t in clocks])
    ticks = lambda t: (t.dwHighDateTime << 32) + t.dwLowDateTime
    handles = W.DWORD(); assert k.GetProcessHandleCount(process, C.byref(handles))
    return {'private_MiB': memory.private / 1048576, 'working_set_MiB': memory.working / 1048576,
            'handles': handles.value, 'cpu_seconds': (ticks(clocks[2]) + ticks(clocks[3])) / 1e7}

def close_owned(child):
    @ENUM
    def close(h, _):
        pid = W.DWORD(); u.GetWindowThreadProcessId(h, C.byref(pid))
        name = C.create_unicode_buffer(128); u.GetClassNameW(h, name, 128)
        if pid.value == child.pid and name.value == 'EShuRu.Broker':
            u.PostMessageW(h, 0x10, 0, 0)
        return True
    u.EnumWindows(close, 0)
    try: child.wait(timeout=5)
    except subprocess.TimeoutExpired: child.terminate(); child.wait(timeout=3)

def main():
    child = None; pipe = None; process = None
    try:
        settings_path = Path(os.environ['LOCALAPPDATA']) / 'EShuRu/settings.json'
        settings = json.loads(settings_path.read_text(encoding='utf-8')) if settings_path.exists() else {}
        assert settings.get('remember_words', True), 'Benchmark requires learning enabled; settings were not changed.'
        child = subprocess.Popen([str(root / 'EShuRu.exe'), '--server', '--headless'], env=env,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        session = W.DWORD(); assert k.ProcessIdToSessionId(os.getpid(), C.byref(session))
        pipe_name = '\\\\.\\pipe\\EShuRu.v1.' + scope + '.' + str(session.value)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            pipe = k.CreateFileW(pipe_name, 0xC0000000, 0, None, 3, 0, None)
            if pipe != W.HANDLE(-1).value: break
            pipe = None
            assert child.poll() is None, 'Owned broker exited'
            time.sleep(.02)
        assert pipe, 'Owned pipe unavailable'
        pid = W.ULONG(); assert k.GetNamedPipeServerProcessId(pipe, C.byref(pid)) and pid.value == child.pid
        process = k.OpenProcess(0x410, False, child.pid); assert process
        def send(message):
            data = json.dumps({'v': 1, **message}).encode()
            frame = struct.pack('<I', len(data)) + data; n = W.DWORD()
            assert k.WriteFile(pipe, frame, len(frame), C.byref(n), None) and n.value == len(frame)
        def receive():
            deadline = time.monotonic() + 5
            def read(count):
                data = bytearray()
                while len(data) < count:
                    assert time.monotonic() < deadline and child.poll() is None, 'IPC response timed out'
                    available = W.DWORD()
                    assert k.PeekNamedPipe(pipe, None, 0, None, C.byref(available), None)
                    if not available.value:
                        time.sleep(.0005); continue
                    size = min(available.value, count-len(data))
                    buf = C.create_string_buffer(size); n = W.DWORD()
                    assert k.ReadFile(pipe, buf, size, C.byref(n), None) and n.value
                    data.extend(buf.raw[:n.value])
                return data
            length = struct.unpack('<I', read(4))[0]; assert 0 < length <= 65536
            return json.loads(read(length))
        send({'type': 'hello', 'pid': os.getpid()})
        ready = receive(); assert ready['type'] == 'ready' and ready['engine']
        sequence = 0; identity = uuid.uuid4().hex; times = []
        def key(vk, char=''):
            nonlocal sequence
            sequence += 1; start = time.perf_counter()
            send({'type': 'key', 'session': identity, 'seq': sequence, 'vk': vk,
                  'char': char, 'ctrl': False, 'shift': False})
            while True:
                message = receive()
                if message['type'] == 'state' and message['seq'] == sequence:
                    times.append((time.perf_counter()-start)*1000)
                    return message
        digest = lambda state: hashlib.sha256(json.dumps(state['candidates'], ensure_ascii=False).encode()).hexdigest()
        expected = []; samples = []; start = time.monotonic()
        def sample(round_no, direction, page):
            samples.append({'seconds': time.monotonic()-start, 'round': round_no, 'direction': direction,
                            'page': page, **snapshot(process)})
        for round_no in range(args.rounds):
            state = key(ord('S'), 's'); assert state['pending'] and state['page'] == 0
            sample(round_no, 'first', 0)
            page = 0
            while True:
                assert state['page'] == page and state['candidates'] and page < 4096
                if round_no == 0: expected.append(digest(state))
                else: assert page < len(expected) and digest(state) == expected[page]
                if page % 32 == 0 or state['last_page']: sample(round_no, 'forward', page)
                if state['last_page']: break
                page += 1; state = key(0x22)
            assert len(expected) == page + 1
            while page > 0:
                state = key(0x21); page -= 1
                assert state['page'] == page and digest(state) == expected[page]
                if page % 32 == 0: sample(round_no, 'backward', page)
            key(0x1B); time.sleep(.15); sample(round_no, 'idle', 0)
            print(json.dumps({'round': round_no + 1, 'pages': len(expected), 'private_MiB': samples[-1]['private_MiB']}), flush=True)
        times.sort(); elapsed = samples[-1]['seconds']-samples[0]['seconds']
        cpu = samples[-1]['cpu_seconds']-samples[0]['cpu_seconds']
        report = {'binary': str(root / 'EShuRu.exe'), 'binary_sha256': hashlib.sha256((root/'EShuRu.exe').read_bytes()).hexdigest(),
                  'rounds': args.rounds, 'pages': len(expected), 'operations': len(times),
                  'page_sequence_sha256': hashlib.sha256(''.join(expected).encode()).hexdigest(),
                  'ipc_operation_p95_ms': times[(len(times)-1)*95//100], 'ipc_operation_max_ms': max(times),
                  'logical_processors': os.cpu_count(), 'sample_seconds': elapsed,
                  'cpu_machine_percent': cpu / elapsed / os.cpu_count() * 100,
                  'sampled_peak_private_MiB': max(s['private_MiB'] for s in samples), 'samples': samples,
                  'scope': 'One owned headless broker, empty isolated frequency, 3 forward/backward traversals of s; no painting, real host TSF or cloud. IPC timings include Python polling and are not key-to-paint latency.'}
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps({k: v for k, v in report.items() if k not in ['samples', 'binary']}), flush=True)
    finally:
        if pipe: k.CloseHandle(pipe)
        if process: k.CloseHandle(process)
        if child: close_owned(child)
        # Only this UUID's test marker/files, never the user's normal pause state or weights.
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\EShuRu\Runtime', 0, winreg.KEY_SET_VALUE) as key:
                winreg.DeleteValue(key, 'AutostartPaused.' + scope)
        except FileNotFoundError: pass
        for suffix in ['', '.bak', '.tmp', '.bak.tmp']:
            (Path(os.environ['LOCALAPPDATA']) / 'EShuRu' / ('test-frequency-' + scope + '.dat' + suffix)).unlink(missing_ok=True)

if __name__ == '__main__': main()
