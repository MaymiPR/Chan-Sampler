# Generated file: do not edit by hand. It is the starter's text, written when the suite is generated and compared
# by the starter's sync check, so a change made here is lost; the plug-in's own code is in the harness beside it.
"""The core of the reference-render harness beside this file: the cache key of a render, the job line of the offline
hosts, the renderer call with its time-out and exit-code check, and the cache loop. The harness <ab>_ref.py holds
what is the plug-in's own (the reference, the renderers, the folders) and calls these functions. numpy only.

    key = cache_key(job, plugin_file, plugin_name, fs, block)        # sha1 hex of everything that decides a render
    ys = render_jobs(jobs, lambda jobfile: [exe, vst3, name, jobfile, 'fs=48000', 'block=512'], work)
    ys = render_cached(jobs, keys, cache_dir, lambda todo: render_jobs(todo, command, work))
    n = latency([exe, vst3, name, os.devnull, 'fs=48000'])

A job is a dict with x and the optional params, norm, auto, raw, reset, state, settle, notes, tune, ext, sc and
salt (the harness documents them). A renderer that exits with another code than 0, or runs longer than the
time-out, raises RuntimeError with its output; the timed-out process is killed. The temporary input, output,
side-chain and job files are removed in every case.
"""
import hashlib
import os
import subprocess
import uuid
import numpy as np
from nulltest_lib import stereo, read_f32, write_f32

TIMEOUT = 600        # seconds one renderer call may run before it is killed


def file_identity(path):
    """What makes a cached render stale when a file changes: the resolved path with the size and the modification
    time of the file, or of every file below it when it is a folder (a VST3 bundle); 'missing' when it is neither."""
    p = os.path.realpath(path)
    if os.path.isdir(p):
        files = []
        for folder, _, names in os.walk(p):
            for name in names:
                s = os.stat(os.path.join(folder, name))
                files.append((os.path.relpath(os.path.join(folder, name), p).replace(os.sep, '/'), s.st_size, s.st_mtime_ns))
        return (p, tuple(sorted(files)))
    if os.path.isfile(p):
        s = os.stat(p)
        return (p, s.st_size, s.st_mtime_ns)
    return (p, 'missing')


def cache_key(job, plugin_file, plugin_name, fs, block, vary=False, bpm=None):
    """The cache key of one render: the input and the side-chain (shape and bytes), every job option that reaches the
    job line (params, norm, auto, at, raw, reset, state with the state file's identity, settle, preroll, notes, tune,
    ext), the salt, the identity of the plug-in file, its host name, the rate, the block size, vary and the tempo.
    An option a job does not use adds nothing, so a job keeps its key when an option is added here; a job with auto
    is keyed as sample exact, which the hosts render since they split the block at a change."""
    h = hashlib.sha1()
    x = stereo(job['x'])
    h.update(repr(('x', x.shape)).encode()); h.update(x.tobytes())
    if job.get('sc') is not None:
        sc = stereo(job['sc'])
        h.update(repr(('sc', sc.shape)).encode()); h.update(sc.tobytes())
    options = [
        ('params', [(str(k), str(v)) for k, v in (job.get('params') or {}).items()]),
        ('norm', [(str(k), float(v)) for k, v in (job.get('norm') or {}).items()]),
        ('auto', [(str(k), int(s), float(v)) for k, s, v in (job.get('auto') or [])]),
        ('raw', bool(job.get('raw'))),
        ('reset', job.get('reset') is not False),
        ('state', file_identity(job['state']) if job.get('state') else None),
        ('settle', int(job.get('settle') or 0)),
        ('notes', [(int(k), float(s), float(n), int(v)) for k, s, n, v in (job.get('notes') or [])]),
        ('tune', [(str(k), float(v)) for k, v in (job.get('tune') or {}).items()]),
        ('ext', bool(job.get('ext'))),
        ('salt', job.get('salt') or None),
        ('plugin', file_identity(plugin_file), str(plugin_name)),
        ('host', int(fs), int(block), bool(vary)),
    ]
    if job.get('auto'): options.append(('auto', 'sample exact'))
    if job.get('at'): options.append(('at', [(float(t), str(s)) for t, s in job['at']]))
    if job.get('preroll'): options.append(('preroll', float(job['preroll'])))
    if bpm is not None: options.append(('bpm', float(bpm)))
    h.update(repr(tuple(options)).encode())
    return h.hexdigest()


def job_line(job, fin, fout, fsc=None):
    """One line of a jobs file: the input and output files, then every option of the job as the hosts read it."""
    parts = [f'"{fin}"', f'"{fout}"']
    for k, v in (job.get('params') or {}).items(): parts.append(f'"param:{k}={v}"')
    for k, v in (job.get('norm') or {}).items(): parts.append(f'"norm:{k}={v}"')
    for k, s, v in (job.get('auto') or []): parts.append(f'"auto:{k}@{int(s)}={v}"')
    for t, s in (job.get('at') or []): parts.append(f'"at={float(t)!r}:{s}"')
    if job.get('raw'): parts.append('raw=1')
    if job.get('reset') is False: parts.append('reset=0')
    if job.get('state'): parts.append(f'"state={job["state"]}"')
    if job.get('settle'): parts.append(f'settle={int(job["settle"])}')
    if job.get('preroll'): parts.append(f'preroll={float(job["preroll"])!r}')
    if job.get('notes'):
        parts.append('notes=' + ';'.join(f'{int(k)}:{float(s)!r}:{float(n)!r}:{int(v)}' for k, s, n, v in job['notes']))
    for k, v in (job.get('tune') or {}).items(): parts.append(f'"tune:{k}={float(v)!r}"')
    if job.get('ext'): parts.append('ext=1')
    if fsc: parts.append(f'"sc={fsc}"')
    return ' '.join(parts)


def run(cmd, timeout=None):
    """The finished process of a renderer command; RuntimeError when it exits with another code than 0 or runs longer
    than timeout seconds (default TIMEOUT; subprocess.run kills it then)."""
    timeout = TIMEOUT if timeout is None else timeout
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f'timed out after {timeout:g} s and was killed:\n' + ' '.join(cmd)) from None
    if r.returncode != 0:
        raise RuntimeError(f'exit code {r.returncode}:\n' + ' '.join(cmd) + '\n' + r.stdout + r.stderr)
    return r


def render_jobs(jobs, command, work, timeout=None):
    """The renders of jobs, as (2, n) float32 arrays: writes each input (and side-chain) and the jobs file into work,
    runs command(jobs file) (the renderer's argument list) and reads the outputs. An unknown parameter, tuning key
    or plug-in ends the renderer with another code than 0: run() raises with its output and its error text."""
    os.makedirs(work, exist_ok=True)
    tag = uuid.uuid4().hex[:8]
    lines, outs, temp = [], [], []
    try:
        for i, j in enumerate(jobs):
            fin = os.path.join(work, f'{tag}_{i}_in.f32'); fout = os.path.join(work, f'{tag}_{i}_out.f32')
            temp += [fin, fout]
            write_f32(fin, j['x'])
            fsc = None
            if j.get('sc') is not None:
                fsc = os.path.join(work, f'{tag}_{i}_sc.f32'); temp.append(fsc)
                write_f32(fsc, j['sc'])
            lines.append(job_line(j, fin, fout, fsc)); outs.append(fout)
        jobfile = os.path.join(work, f'{tag}_jobs.txt'); temp.append(jobfile)
        with open(jobfile, 'w') as h: h.write('\n'.join(lines) + '\n')
        cmd = command(jobfile)
        r = run(cmd, timeout)
        res = []
        for fout in outs:
            if not os.path.exists(fout):
                raise RuntimeError('render failed:\n' + ' '.join(cmd) + '\n' + r.stdout + r.stderr)
            res.append(read_f32(fout))
        return res
    finally:
        for f in temp:
            if os.path.exists(f): os.remove(f)


def render_cached(jobs, keys, cache_dir, render):
    """The renders of jobs with a cache of .npy files: keys holds one cache key per job (None: never cached), and
    render(list of jobs) renders the ones the cache does not have. A list with a chained job (reset=False: it depends
    on the job before it) is never cached; chained_keys() gives the keys for that rule."""
    os.makedirs(cache_dir, exist_ok=True)
    have = {}
    for i, k in enumerate(keys):
        p = os.path.join(cache_dir, k + '.npy') if k else None
        if p and os.path.exists(p): have[i] = np.load(p)
    todo = [i for i in range(len(jobs)) if i not in have]
    new = render([jobs[i] for i in todo]) if todo else []
    for i, y in zip(todo, new):
        have[i] = y
        if keys[i]: np.save(os.path.join(cache_dir, keys[i] + '.npy'), y)
    return [have[i] for i in range(len(jobs))]


def chained_keys(jobs, key):
    """key(job) for every job, or None for all of them when one job keeps the previous job's instance."""
    chained = any(j.get('reset') is False for j in jobs)
    return [None if chained else key(j) for j in jobs]


def latency(cmd, timeout=None):
    """The latency in samples a host prints when it loads a plug-in (cmd: the host with os.devnull as its jobs file)."""
    r = run(cmd, timeout)
    if 'latency ' not in r.stdout: raise RuntimeError(r.stdout + r.stderr)
    return int(r.stdout.split('latency ')[1].split()[0])
