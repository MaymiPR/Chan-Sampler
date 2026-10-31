"""FL render queue: a content-keyed cache, retries, resume, one FL at a time.

A job is a name, a function `build(path, deps=None)` that writes its .flp to
a path (with `deps`, as if the dependency files lived at those paths), and
the files the render depends on (the sample, our VST3 for in-FL pairs). Its
key is the hash of the project built with the dependencies at their bare
file names (so a moved repository keeps its renders), of every dependency's
content and of FL's version. A render is reused when <name>.render.json
beside the WAV holds the same key; an old WAV without that file is adopted
when the project rebuilt with the old sample path is byte-identical to the
.flp on disk and no dependency is newer than the WAV (`adopt=True` trusts
the WAV regardless, for a plugin rebuild that changed nothing). Everything
else is rendered, each failure retried, and the WAV checked before it is
trusted: complete, and as long as the project (`build.min_seconds`, the end
of its last note; FL now and then stops a render early, so a short render
counts as a failure and is rendered again).

<out>/queue.json holds the state of the last run, so an interrupted run
continues where it stopped (`status()` says what is left). A lock file keeps
two queues from rendering at once, and a running FL GUI pauses the queue
instead of swallowing the render.

    from lib import flqueue
    q = flqueue.Queue(OUT, deps=[paths.PLUGIN_VST3])
    q.add(name, build_fn, deps=[sample_path])      # build_fn(path, deps=None) writes the .flp
    wavs = q.run()                                  # name -> wav path; q.failed lists the rest

    python -m lib.flqueue cases.fl_cases cases.arp_cases [--kind pair|fl] [--only d_cut] [--dry]
                          [--adopt] [--no-plugin-dep] [--status] [--retries 2] [--timeout 300]
    python -m lib.flqueue --selftest
"""
import ctypes, hashlib, json, os, re, struct, subprocess, sys, time

ATTEMPTS = 3          # one render plus two retries
TIMEOUT = 300         # seconds per FL render
SIDECAR = '.render.json'


class RenderError(RuntimeError):
    def __init__(self, reason, detail=''):
        super().__init__(f'{reason}: {detail}' if detail else reason)
        self.reason = reason


# ----------------------------------------------------------------------------- FL

def fl_exe():
    from lib import paths
    return paths.FL_EXE


def fl_version(exe=None):
    """FL's file version (26.1.7.5654) from the PE version resource; size and
    mtime when that fails."""
    exe = exe or fl_exe()
    try:
        ver = ctypes.windll.version
        size = ver.GetFileVersionInfoSizeW(exe, None)
        if size:
            buf = ctypes.create_string_buffer(size)
            ver.GetFileVersionInfoW(exe, 0, size, buf)
            p = ctypes.c_void_p(); n = ctypes.c_uint()
            if ver.VerQueryValueW(buf, '\\', ctypes.byref(p), ctypes.byref(n)):
                fixed = ctypes.string_at(p.value, n.value)
                ms, ls = struct.unpack_from('<II', fixed, 8)
                return f'{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}'
    except Exception:
        pass
    st = os.stat(exe)
    return f'size{st.st_size}_mtime{int(st.st_mtime)}'


def fl_processes():
    """The pid of every FL64.exe (the GUI or a render)."""
    if sys.platform != 'win32':
        return []
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq FL64.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0] == 'FL64.exe':
            pids.append(int(parts[1]))
    return pids


def pid_alive(pid):
    out = subprocess.run(['tasklist', '/FI', f'PID eq {pid}', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
    return f'"{pid}"' in out


def kill_tree(pid):
    subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], capture_output=True)


# FL's command-line render opens FL's window and takes the focus each time.
# On Windows it is started on a desktop of its own that nobody looks at, so
# renders don't take over the screen (the WAV is byte-identical);
# FLQUEUE_VISIBLE=1 renders on the user's desktop as before.
HIDDEN_DESKTOP = 'ChanSamplerRenders'
_desktop = None


class _Hidden:
    """A process started on the hidden desktop, with Popen's wait / returncode / pid."""
    def __init__(self, cmd):
        from ctypes import wintypes as W

        class STARTUPINFO(ctypes.Structure):
            _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR),
                        ('dwX', W.DWORD), ('dwY', W.DWORD), ('dwXSize', W.DWORD), ('dwYSize', W.DWORD),
                        ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                        ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                        ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]

        class PROCESS_INFORMATION(ctypes.Structure):
            _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]

        self._k32 = ctypes.WinDLL('kernel32', use_last_error=True)
        si = STARTUPINFO(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + HIDDEN_DESKTOP
        pi = PROCESS_INFORMATION()
        line = ctypes.create_unicode_buffer(subprocess.list2cmdline(cmd))
        if not self._k32.CreateProcessW(None, line, None, None, False, 0, None, None, ctypes.byref(si), ctypes.byref(pi)):
            raise OSError(ctypes.get_last_error(), 'CreateProcess on the hidden desktop failed')
        self._k32.CloseHandle(pi.hThread)
        self._handle, self.pid, self.returncode = pi.hProcess, pi.dwProcessId, None

    def wait(self, timeout=None):
        from ctypes import wintypes as W
        if self._k32.WaitForSingleObject(self._handle, 0xFFFFFFFF if timeout is None else int(timeout * 1000)) != 0:
            raise subprocess.TimeoutExpired('FL64.exe', timeout)
        code = W.DWORD(); self._k32.GetExitCodeProcess(self._handle, ctypes.byref(code))
        self._k32.CloseHandle(self._handle)
        self.returncode = code.value
        return self.returncode


def start_fl(cmd):
    """Start an FL command line, on the hidden desktop when there is one."""
    global _desktop
    if sys.platform == 'win32' and os.environ.get('FLQUEUE_VISIBLE') != '1':
        try:
            if _desktop is None:
                u32 = ctypes.WinDLL('user32', use_last_error=True)
                u32.CreateDesktopW.restype = ctypes.c_void_p
                _desktop = u32.CreateDesktopW(HIDDEN_DESKTOP, None, None, 0, 0x01FF, None) or 0   # kept for the process's life
            if _desktop:
                return _Hidden(cmd)
        except OSError:
            pass
    return subprocess.Popen(cmd)


def validate_wav(path, min_frames=1, min_seconds=0.0):
    """(frames, sample_rate) of a complete WAV; RenderError('bad_wav') otherwise,
    or RenderError('short_render') when it holds less than min_seconds (FL now
    and then stops a render early and writes a complete but short file)."""
    try:
        b = open(path, 'rb').read()
    except OSError as e:
        raise RenderError('no_output', str(e))
    if len(b) < 44 or b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise RenderError('bad_wav', f'{os.path.basename(path)}: not a RIFF WAVE ({len(b)} bytes)')
    i, fmt, data_len = 12, None, None
    while i + 8 <= len(b):
        cid = b[i:i + 4]; n = struct.unpack('<I', b[i + 4:i + 8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', b[i + 8:i + 24])
        elif cid == b'data':
            data_len = min(n, len(b) - i - 8)
            if n > len(b) - i - 8:
                raise RenderError('bad_wav', f'{os.path.basename(path)}: data chunk truncated ({n} declared, {len(b) - i - 8} present)')
        i += 8 + n + (n & 1)
    if fmt is None or data_len is None:
        raise RenderError('bad_wav', f'{os.path.basename(path)}: no fmt or data chunk')
    frames = data_len // max(1, fmt[1] * fmt[5] // 8)
    if frames < min_frames:
        raise RenderError('bad_wav', f'{os.path.basename(path)}: {frames} frames, expected at least {min_frames}')
    if frames < min_seconds * fmt[2]:
        raise RenderError('short_render', f'{os.path.basename(path)}: {frames / fmt[2]:.2f} s, the project lasts {min_seconds:.2f} s')
    return frames, fmt[2]


def render_fl(project, wav, timeout=TIMEOUT, exe=None):
    """One FL command-line render (`FL64.exe /R /Ewav project`). Raises
    RenderError with reason fl_running, timeout, no_output or bad_wav."""
    exe = exe or fl_exe()
    if fl_processes():
        raise RenderError('fl_running', 'FL Studio is open (a running FL swallows the command-line render)')
    if os.path.exists(wav):
        os.remove(wav)
    p = start_fl([exe, '/R', '/Ewav', project])
    try:
        p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_tree(p.pid)
        raise RenderError('timeout', f'{os.path.basename(project)} took more than {timeout} s')
    if not os.path.exists(wav):
        raise RenderError('no_output', f'FL did not render {os.path.basename(project)} (exit {p.returncode})')
    validate_wav(wav)
    return wav


# ----------------------------------------------------------------------------- hashing

_hash_cache = {}


def file_hash(path):
    """sha256 of a file, remembered per (path, size, mtime) for the process."""
    st = os.stat(path)
    k = (os.path.abspath(path), st.st_size, st.st_mtime_ns)
    if k not in _hash_cache:
        h = hashlib.sha256()
        with open(path, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                h.update(chunk)
        _hash_cache[k] = h.hexdigest()
    return _hash_cache[k]


def paths_in(flp_bytes, base):
    """Absolute paths ending in the file name `base` as FL stores them (UTF-16)
    and as our state XML stores them (UTF-8), in order of appearance."""
    found = []
    b16 = re.escape(base.encode('utf-16le'))
    for m in re.finditer(rb'[A-Za-z]\x00:\x00[\\/]\x00(?:(?:[^\x00][\x00]){0,400}?[\\/]\x00)?' + b16, flp_bytes):
        found.append(m.group(0).decode('utf-16le'))
    for m in re.finditer(rb'[A-Za-z]:[\\/](?:[^\x00"<>|]{0,400}?[\\/])?' + re.escape(base.encode('utf-8')), flp_bytes):
        found.append(m.group(0).decode('utf-8', 'replace'))
    return found


def job_key(canonical_bytes, deps, fl_ver):
    h = hashlib.sha256()
    h.update(b'flp:' + hashlib.sha256(canonical_bytes).hexdigest().encode())
    for d in deps:
        h.update(f'dep:{os.path.basename(d)}:{file_hash(d)}'.encode())
    h.update(f'fl:{fl_ver}'.encode())
    return h.hexdigest()[:32]


# ----------------------------------------------------------------------------- queue

class Job:
    def __init__(self, name, build, deps=(), meta=None, min_seconds=None):
        self.name, self.build, self.deps, self.meta = name, build, [os.path.normpath(d) for d in deps if d], meta or {}
        # A render shorter than this is cut off (the build function says how
        # long its project lasts: the end of its last note).
        self.min_seconds = getattr(build, 'min_seconds', 0.0) if min_seconds is None else min_seconds
        self.key = self.state = self.error = self.reason = self.flp_bytes = None
        self.attempts = 0
        self.seconds = 0.0


class Queue:
    def __init__(self, out, deps=(), renderer=None, fl_ver=None, log=print):
        self.out = os.path.abspath(out)
        os.makedirs(self.out, exist_ok=True)
        self.deps = [os.path.normpath(d) for d in deps if d]
        self.renderer = renderer or render_fl
        self.fl_ver = fl_ver or (fl_version() if renderer is None else 'test')
        self.log = log
        self.jobs = []
        self.failed = {}

    def add(self, name, build, deps=(), meta=None, min_seconds=None):
        self.jobs.append(Job(name, build, list(deps) + self.deps, meta, min_seconds))
        return self

    def flp(self, name):
        return os.path.join(self.out, name + '.flp')

    def wav(self, name):
        return os.path.join(self.out, name + '.wav')

    def sidecar(self, name):
        return os.path.join(self.out, name + SIDECAR)

    def plan(self, adopt=False):
        """Build every .flp, compute the keys, decide cached / adopt / stale / missing."""
        for j in self.jobs:
            for d in j.deps:
                if not os.path.exists(d):
                    raise FileNotFoundError(f'{j.name}: dependency {d}')
            flp = self.flp(j.name)
            old = open(flp, 'rb').read() if os.path.exists(flp) else None
            j.flp_bytes = self._build(j)
            j.key = job_key(self._build(j, [os.path.basename(d) for d in j.deps]), j.deps, self.fl_ver)
            wav = self.wav(j.name)
            j.state, j.reason = 'missing', 'no render'
            if os.path.exists(wav):
                side = self._read_sidecar(j.name)
                if side and side.get('key') == j.key:
                    try:
                        validate_wav(wav, min_seconds=j.min_seconds)
                        j.state = 'cached'
                    except RenderError as e:
                        j.state, j.reason = 'stale', str(e)
                elif side:
                    j.state, j.reason = 'stale', self._why(side, j)
                else:
                    try:
                        validate_wav(wav, min_seconds=j.min_seconds)
                        wav_t = os.stat(wav).st_mtime
                        newer = [os.path.basename(d) for d in j.deps if os.stat(d).st_mtime > wav_t]
                        if old is None or self._rebuilt_like(j, old) != old:
                            j.state, j.reason = 'stale', 'project changed (no record of the old render)'
                        elif newer and not adopt:
                            j.state, j.reason = 'stale', 'newer than the render: ' + ', '.join(newer)
                        else:
                            j.state = 'adopt'
                    except RenderError as e:
                        j.state, j.reason = 'stale', str(e)
        return self.jobs

    def _build(self, j, deps=None):
        tmp = self.flp(j.name) + '.new'
        if deps is None:
            j.build(tmp)
        else:
            j.build(tmp, deps=deps)
        b = open(tmp, 'rb').read()
        os.remove(tmp)
        return b

    def _rebuilt_like(self, j, old):
        """The project built with the dependency paths the old .flp holds (a
        moved repository), for an exact comparison."""
        deps = []
        for d in j.deps:
            found = paths_in(old, os.path.basename(d))
            deps.append(found[0] if found else d)
        return self._build(j, deps)

    def _why(self, side, j):
        if side.get('fl') != self.fl_ver:
            return f'FL {side.get("fl")} -> {self.fl_ver}'
        old = side.get('deps', {})
        changed = [os.path.basename(d) for d in j.deps if old.get(os.path.basename(d)) != file_hash(d)]
        return 'changed: ' + ', '.join(changed) if changed else 'project changed'

    def _read_sidecar(self, name):
        try:
            return json.load(open(self.sidecar(name), encoding='utf-8'))
        except (OSError, ValueError):
            return None

    def _write_sidecar(self, j, adopted=False):
        frames, sr = validate_wav(self.wav(j.name))
        rec = dict(key=j.key, fl=self.fl_ver, deps={os.path.basename(d): file_hash(d) for d in j.deps},
                   frames=frames, rate=sr, seconds=round(j.seconds, 2), attempts=j.attempts,
                   adopted=adopted, time=time.strftime('%Y-%m-%d %H:%M:%S'), meta=j.meta)
        json.dump(rec, open(self.sidecar(j.name), 'w', encoding='utf-8'), indent=1)

    # -- the run
    def run(self, attempts=ATTEMPTS, timeout=TIMEOUT, adopt=False, dry=False, wait_fl=True):
        self.plan(adopt=adopt)
        todo = [j for j in self.jobs if j.state in ('missing', 'stale')]
        counts = {s: sum(1 for j in self.jobs if j.state == s) for s in ('cached', 'adopt', 'stale', 'missing')}
        self.log(f'flqueue: {len(self.jobs)} jobs, {counts["cached"]} cached, {counts["adopt"]} adopted, '
                 f'{counts["stale"]} stale, {counts["missing"]} new -> {len(todo)} to render (FL {self.fl_ver})')
        for j in self.jobs:
            if j.state == 'stale':
                self.log(f'  stale  {j.name}: {j.reason}')
        for j in self.jobs:
            if j.state == 'adopt' and not dry:
                j.attempts = 0
                self._write_sidecar(j, adopted=True)
                j.state = 'cached'
        if dry:
            return {j.name: self.wav(j.name) for j in self.jobs if j.state in ('cached', 'adopt')}
        if todo:
            self._lock()
            try:
                self._render_all(todo, attempts, timeout, wait_fl)
            finally:
                self._unlock()
        self.failed = {j.name: j.error for j in self.jobs if j.state == 'failed'}
        self._save_state()
        done = [j for j in self.jobs if j.state == 'cached']
        rendered = [j for j in done if j.attempts]
        total = sum(j.seconds for j in rendered)
        mean = total / len(rendered) if rendered else 0.0
        msg = f'flqueue: {len(rendered)} rendered in {total:.0f} s'
        if rendered:
            msg += f' ({mean:.1f} s each), {len(done) - len(rendered)} reused (about {mean * (len(done) - len(rendered)) / 60:.0f} min saved)'
        if self.failed:
            msg += f', {len(self.failed)} FAILED: ' + ', '.join(self.failed)
        self.log(msg)
        return {j.name: self.wav(j.name) for j in done}

    def _render_all(self, todo, attempts, timeout, wait_fl):
        n = len(todo)
        for i, j in enumerate(todo, 1):
            j.error = None
            for a in range(1, attempts + 1):
                self._wait_for_fl(wait_fl)
                j.attempts = a
                t0 = time.time()
                try:
                    open(self.flp(j.name), 'wb').write(j.flp_bytes)
                    self.renderer(self.flp(j.name), self.wav(j.name), timeout=timeout)
                    validate_wav(self.wav(j.name), min_seconds=j.min_seconds)
                    j.seconds = time.time() - t0
                    self._write_sidecar(j)
                    j.state = 'cached'
                    self.log(f'  [{i}/{n}] {j.name} {j.seconds:.1f} s' + (f' (attempt {a})' if a > 1 else ''))
                    break
                except RenderError as e:
                    j.error = str(e)
                    self.log(f'  [{i}/{n}] {j.name} attempt {a} failed: {e}')
                    time.sleep(2)
            else:
                j.state = 'failed'
            self._save_state()

    def _wait_for_fl(self, wait_fl):
        said = False
        while fl_processes():
            if not wait_fl:
                raise RenderError('fl_running', 'FL Studio is open')
            if not said:
                self.log('  FL Studio is open: waiting for it to close (a running FL swallows the render)')
                said = True
            time.sleep(10)

    # -- state, lock
    def state_path(self):
        return os.path.join(self.out, 'queue.json')

    def _save_state(self):
        rec = dict(time=time.strftime('%Y-%m-%d %H:%M:%S'), fl=self.fl_ver,
                   jobs={j.name: dict(state=j.state, attempts=j.attempts, seconds=round(j.seconds, 2), error=j.error) for j in self.jobs})
        json.dump(rec, open(self.state_path(), 'w', encoding='utf-8'), indent=1)

    def status(self):
        try:
            rec = json.load(open(self.state_path(), encoding='utf-8'))
        except (OSError, ValueError):
            self.log('flqueue: no run recorded in ' + self.out)
            return None
        jobs = rec['jobs']
        counts = {}
        for v in jobs.values():
            counts[v['state']] = counts.get(v['state'], 0) + 1
        self.log(f'flqueue: last run {rec["time"]} (FL {rec["fl"]}): ' + ', '.join(f'{v} {k}' for k, v in sorted(counts.items())))
        for name, v in jobs.items():
            if v['state'] != 'cached':
                self.log(f'  {v["state"]:7s} {name}: {v.get("error") or ""}')
        return rec

    def _lock(self):
        path = os.path.join(self.out, '.flqueue.lock')
        if os.path.exists(path):
            try:
                other = json.load(open(path))
                if pid_alive(other['pid']):
                    raise RenderError('locked', f'another queue (pid {other["pid"]}, since {other["time"]}) renders into {self.out}')
            except (OSError, ValueError, KeyError):
                pass
        json.dump(dict(pid=os.getpid(), time=time.strftime('%Y-%m-%d %H:%M:%S')), open(path, 'w'))

    def _unlock(self):
        try:
            os.remove(os.path.join(self.out, '.flqueue.lock'))
        except OSError:
            pass


# ----------------------------------------------------------------------------- the harness cases

def jobs_from_cases(cases, kind='pair', only=None, plugin_dep=True):
    """(out_dir, deps, [(name, build, deps)]) for the harness case dicts:
    kind 'pair' = lib.nullfl (FL's Sampler and our VST3 in one project),
    'fl' = lib.flrender (FL's Sampler alone)."""
    from lib import paths
    if kind == 'pair':
        from lib import nullfl as M
        deps = [paths.PLUGIN_VST3] if plugin_dep and getattr(paths, 'PLUGIN_VST3', None) else []
    else:
        from lib import flrender as M
        deps = []
    jobs = []
    for c in cases:
        if only and not any(o in c['name'] for o in only):
            continue
        jobs.append((c['name'], M.project(c), [paths.signal(c['sample'])]))
    return M.OUT, deps, jobs


def queue_for(cases, kind='pair', only=None, plugin_dep=True, **kw):
    out, deps, jobs = jobs_from_cases(cases, kind, only, plugin_dep)
    q = Queue(out, deps=deps, **kw)
    for name, build, d in jobs:
        q.add(name, build, d)
    return q


# ----------------------------------------------------------------------------- self-test

def selftest():
    import tempfile, shutil
    tmp = tempfile.mkdtemp(prefix='flqueue_')
    try:
        calls = {}

        def fake(project, wav, timeout=0):
            name = os.path.basename(project)[:-4]
            calls[name] = calls.get(name, 0) + 1
            if name == 'b' and calls[name] == 1:
                raise RenderError('no_output', 'FL did not render b (simulated)')
            if name == 'c':
                open(wav, 'wb').write(b'RIFF\0\0\0\0WAVE')      # a truncated file every time
                raise RenderError('bad_wav', 'simulated')
            frames = 100
            if name == 'd' and calls[name] == 1:
                frames = 10                                     # complete but cut off: FL stopped early
            fmt = struct.pack('<HHIIHH', 1, 2, 44100, 44100 * 4, 4, 16)
            raw = b'\0' * (frames * 4)
            body = b'WAVE' + b'fmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(raw)) + raw
            open(wav, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)

        sample = os.path.join(tmp, 'sample.bin'); open(sample, 'wb').write(b'sample v1')

        def build(text, dep=sample):   # like the harness: the sample path is written into the project
            def write(path, deps=None):
                open(path, 'wb').write(text + b' @ ' + (deps[0] if deps else dep).encode())
            return write

        out = os.path.join(tmp, 'renders')
        logs = []
        q = Queue(out, deps=[sample], renderer=fake, log=logs.append)
        for n in 'abc':
            q.add(n, build(f'project {n}'.encode()))
        dbuild = build(b'project d')
        dbuild.min_seconds = 50 / 44100        # 50 frames: the first render (10) is too short
        q.add('d', dbuild)
        wavs = q.run(attempts=2)
        assert sorted(wavs) == ['a', 'b', 'd'] and list(q.failed) == ['c'], (wavs, q.failed)
        assert calls == {'a': 1, 'b': 2, 'c': 2, 'd': 2}, calls
        assert not os.path.exists(os.path.join(out, 'c' + SIDECAR))
        assert q._read_sidecar('d')['frames'] == 100
        # a short render on disk, record or not, is not trusted
        q.jobs[3].min_seconds = 200 / 44100
        q.plan()
        assert q.jobs[3].state == 'stale' and 'lasts' in q.jobs[3].reason, q.jobs[3].reason
        try:
            validate_wav(os.path.join(out, 'd.wav'), min_seconds=200 / 44100)
            raise AssertionError('short render accepted')
        except RenderError as e:
            assert e.reason == 'short_render'

        # second run: a and b cached, c tried again, nothing else rendered
        q2 = Queue(out, deps=[sample], renderer=fake, log=logs.append)
        for n in 'abc':
            q2.add(n, build(f'project {n}'.encode()))
        q2.run(attempts=1)
        assert calls == {'a': 1, 'b': 2, 'c': 3, 'd': 2}, calls
        assert [j.state for j in q2.jobs] == ['cached', 'cached', 'failed']

        # a changed project re-renders only itself; a changed dependency re-renders all
        q3 = Queue(out, deps=[sample], renderer=fake, log=logs.append)
        q3.add('a', build(b'project a v2')).add('b', build(b'project b'))
        q3.run()
        assert calls['a'] == 2 and calls['b'] == 2, calls
        assert 'changed' in q3.jobs[0].reason, q3.jobs[0].reason
        open(sample, 'wb').write(b'sample v2')
        q4 = Queue(out, deps=[sample], renderer=fake, log=logs.append)
        q4.add('a', build(b'project a v2')).add('b', build(b'project b'))
        q4.run()
        assert calls['a'] == 3 and calls['b'] == 3, calls

        # adoption: a WAV without a record, project unchanged, dependency older -> reused
        os.remove(os.path.join(out, 'b' + SIDECAR))
        os.utime(sample, (time.time() - 100, time.time() - 100))
        q5 = Queue(out, deps=[sample], renderer=fake, log=logs.append)
        q5.add('b', build(b'project b'))
        q5.run()
        assert calls['b'] == 3 and q5.jobs[0].state == 'cached' and q5._read_sidecar('b')['adopted'], calls
        # the same with a newer dependency -> stale -> rendered
        os.remove(os.path.join(out, 'b' + SIDECAR))
        os.utime(sample, (time.time() + 10, time.time() + 10))   # clearly after the WAV (file times tick in ms)
        q6 = Queue(out, deps=[sample], renderer=fake, log=logs.append)
        q6.add('b', build(b'project b'))
        q6.run()
        assert calls['b'] == 4 and 'newer than the render' in q6.jobs[0].reason, q6.jobs[0].reason
        # a moved repository: the dependency's path inside the project changes, the key does not
        moved = os.path.join(tmp, 'moved'); os.makedirs(moved)
        s2 = os.path.join(moved, 'sample.bin'); shutil.copy2(sample, s2)
        gone = r'C:\Users\someone\Downloads\old repo\nulltest\signals\sample.bin'
        found = paths_in(b'x' + gone.encode('utf-16le') + b'y<STATE samplePath="' + gone.encode() + b'">', 'sample.bin')
        assert found == [gone, gone], found
        q8 = Queue(out, deps=[s2], renderer=fake, log=logs.append)
        q8.add('a', build(b'project a v2', s2))
        q8.plan()
        assert q8.jobs[0].state == 'cached', (q8.jobs[0].state, q8.jobs[0].reason)   # the key ignores where sample.bin lives
        os.remove(os.path.join(out, 'a' + SIDECAR))
        open(os.path.join(out, 'a.flp'), 'wb').write(b'project a v2 @ ' + gone.encode())   # an old project from the old place
        os.utime(s2, (time.time() - 100, time.time() - 100))
        q9 = Queue(out, deps=[s2], renderer=fake, log=logs.append)
        q9.add('a', build(b'project a v2', s2))
        q9.plan()
        assert q9.jobs[0].state == 'adopt', (q9.jobs[0].state, q9.jobs[0].reason)   # rebuilt with the old path, identical
        # a dry run leaves the old project file alone
        q7 = Queue(out, deps=[sample], renderer=fake, log=logs.append)
        q7.add('a', build(b'project a v3'))
        q7.run(dry=True)
        assert open(os.path.join(out, 'a.flp'), 'rb').read() == b'project a v2 @ ' + gone.encode() and calls['a'] == 3

        # status and lock
        q6.status()
        q6._lock()
        try:
            Queue(out, renderer=fake, log=logs.append)._lock()
            raise AssertionError('second lock accepted')
        except RenderError as e:
            assert e.reason == 'locked'
        finally:
            q6._unlock()
        assert validate_wav(os.path.join(out, 'a.wav')) == (100, 44100)
        print('flqueue selftest: PASS (' + str(len(logs)) + ' log lines)')
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ----------------------------------------------------------------------------- CLI

def main(argv=None):
    import argparse, importlib
    ap = argparse.ArgumentParser(description='Render the FL cases of one or more case modules through the queue.')
    ap.add_argument('modules', nargs='*', help='case modules with CASES (cases.fl_cases ...)')
    ap.add_argument('--kind', choices=['pair', 'fl'], default='pair', help='pair: lib.nullfl projects (renders_fl); fl: lib.flrender (renders)')
    ap.add_argument('--only', nargs='*', help='name substrings')
    ap.add_argument('--dry', action='store_true', help='plan only: what is cached, stale, new')
    ap.add_argument('--adopt', action='store_true', help='trust every existing WAV whose record is missing (a rebuild that changed nothing)')
    ap.add_argument('--no-plugin-dep', action='store_true', help='leave our VST3 out of the key')
    ap.add_argument('--retries', type=int, default=ATTEMPTS - 1)
    ap.add_argument('--timeout', type=float, default=TIMEOUT)
    ap.add_argument('--no-wait', action='store_true', help='fail instead of waiting while FL Studio is open')
    ap.add_argument('--status', action='store_true', help='show the last run of this output folder')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.status and not a.modules:
        from lib import nullfl, flrender
        Queue(nullfl.OUT if a.kind == 'pair' else flrender.OUT).status()
        return 0
    cases = []
    for m in a.modules:
        cases += importlib.import_module(m).CASES
    q = queue_for(cases, a.kind, a.only, not a.no_plugin_dep)
    if a.status:
        q.status()
        return 0
    try:
        q.run(attempts=a.retries + 1, timeout=a.timeout, adopt=a.adopt, dry=a.dry, wait_fl=not a.no_wait)
    except RenderError as e:        # locked, or FL open with --no-wait: the run did not start
        print(f'flqueue: {e}')
        return 2
    return 1 if q.failed else 0


if __name__ == '__main__':
    sys.exit(main())
