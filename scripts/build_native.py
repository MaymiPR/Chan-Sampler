"""Builds the null tests' native helpers: every plugins/<Id>/nulltest/native/*.c and *.cpp into a shared library at
build/native/<Id>/<Stem>.dll (Windows, MSVC through vcvars64) or build/native/<Id>/<Stem>.so (Linux and macOS, cc or
c++; MSVC's __declspec(dllexport) is defined away there). The Python scripts load them from build/native/. A C++
helper compiles as C++17 with its plug-in's Source/ on the include path, so it can wrap a block of the plug-in's own
DSP (an include of dsp/<Block>.h). Standard library only.

    python scripts/build_native.py                 # every helper
    python scripts/build_native.py <Id>            # the helpers of one plug-in
    python scripts/build_native.py --list          # what would be built, and where

A helper that a fit compares with a fitted constant keeps the floating-point mode it was fitted with: FAST_MATH lists
the ones built with fast floating point (MSVC /fp:fast, cc -ffast-math), as (plug-in id, helper stem) pairs.
"""
import argparse, glob, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAST_MATH = set()


def sources(only=None):
    out = []
    for src in sorted(glob.glob(os.path.join(ROOT, 'plugins', '*', 'nulltest', 'native', '*.c')) +
                      glob.glob(os.path.join(ROOT, 'plugins', '*', 'nulltest', 'native', '*.cpp'))):
        plugin = os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(src))))
        if only and plugin not in only:
            continue
        stem = os.path.splitext(os.path.basename(src))[0]
        ext = '.dll' if os.name == 'nt' else '.so'
        out.append((plugin, stem, src, os.path.join(ROOT, 'build', 'native', plugin, stem + ext)))
    return out


def msvc_env():
    """the environment of vcvars64.bat of the newest Visual Studio, or None"""
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft Visual Studio',
                           'Installer', 'vswhere.exe')
    if not os.path.isfile(vswhere):
        return None
    path = subprocess.run([vswhere, '-latest', '-products', '*', '-property', 'installationPath'],
                          capture_output=True, text=True).stdout.strip()
    bat = os.path.join(path, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
    if not os.path.isfile(bat):
        return None
    out = subprocess.run(f'"{bat}" >nul && set', shell=True, capture_output=True, text=True).stdout
    return dict(line.split('=', 1) for line in out.splitlines() if '=' in line)


def build(items):
    env = msvc_env() if os.name == 'nt' else None
    if os.name == 'nt' and env is None:
        sys.exit('no Visual Studio with the C++ tools found (vswhere.exe, vcvars64.bat)')
    cl = None
    if env is not None:
        cl = shutil.which('cl', path=next((v for k, v in env.items() if k.upper() == 'PATH'), None))
        if cl is None:
            sys.exit('cl.exe is not on the PATH vcvars64.bat sets')
    failed = 0
    for plugin, stem, src, out in items:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        fast = (plugin, stem) in FAST_MATH
        cpp = src.endswith('.cpp')
        include = os.path.join(ROOT, 'plugins', plugin, 'Source')
        if os.name == 'nt':
            work = tempfile.mkdtemp(prefix='native_')
            cmd = [cl, '/nologo', '/O2', '/D_USE_MATH_DEFINES'] + (['/fp:fast'] if fast else []) + \
                  (['/std:c++17', '/EHsc', '/I' + include] if cpp else []) + ['/LD', src, '/Fe:' + out, '/Fo:' + work + os.sep]
            r = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=work)
            shutil.rmtree(work, ignore_errors=True)
            for leftover in (os.path.splitext(out)[0] + e for e in ('.exp', '.lib')):
                if os.path.exists(leftover):
                    os.remove(leftover)
        else:
            cc = os.environ.get('CXX', 'c++') if cpp else os.environ.get('CC', 'cc')
            cmd = [cc, '-O2', '-shared', '-fPIC', '-D__declspec(x)='] + (['-ffast-math'] if fast else []) + \
                  (['-std=c++17', '-I' + include] if cpp else []) + [src, '-o', out, '-lm']
            r = subprocess.run(cmd, capture_output=True, text=True)
        ok = r.returncode == 0 and os.path.exists(out)
        failed += not ok
        print('%s  %s/%s -> %s' % ('ok  ' if ok else 'FAIL', plugin, os.path.basename(src), os.path.relpath(out, ROOT)))
        if not ok:
            print(r.stdout + r.stderr)
    return failed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('plugins', nargs='*', help='plug-in ids (default: every plug-in)')
    ap.add_argument('--list', action='store_true', help='list the helpers and their outputs, build nothing')
    a = ap.parse_args()
    items = sources(set(a.plugins) or None)
    if a.list:
        for plugin, stem, src, out in items:
            print('%s -> %s' % (os.path.relpath(src, ROOT), os.path.relpath(out, ROOT)))
        return
    sys.exit(1 if build(items) else 0)


if __name__ == '__main__':
    main()
