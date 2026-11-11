"""Raise the suite's version everywhere it lives and make the release commit (and, with --tag, the tag).

    python scripts/bump_version.py 1.2.0            # CMakeLists.txt, CHANGELOG.md, commit "Release 1.2.0"
    python scripts/bump_version.py 1.2.0 --tag      # the same, then the annotated tag v1.2.0 (on approval only)
    python scripts/bump_version.py 1.2.0-rc.1       # a release candidate: the tag publishes a pre-release
    python scripts/bump_version.py 1.2.0 --dry      # show what would change, touch nothing

What it does, in order: refuses a dirty working tree (unless --allow-dirty), a version that is not valid semantic
versioning, or one that is not newer than the current; rewrites `project(<Suite> VERSION x.y.z)` in CMakeLists.txt
(the plug-ins, their About menus, the tools and the installers all read it from there); renames `## Unreleased` in
CHANGELOG.md to `## x.y.z (YYYY-MM-DD)` and inserts a fresh, empty `## Unreleased` above it (a release candidate
leaves the changelog alone: its notes are the Unreleased section); commits with the house identity; with --tag,
tags `vx.y.z` with the message "<Suite display name> x.y.z". It never pushes: the last line prints the push command.
Nothing else in the repository carries the version (the README's release badge reads it from GitHub).
"""
import argparse, datetime, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEMVER = re.compile(r'^(\d+)\.(\d+)\.(\d+)(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?$')


def git(*args, check=True):
    r = subprocess.run(['git', '-C', ROOT] + list(args), capture_output=True, text=True)
    if check and r.returncode != 0:
        sys.exit(f'git {" ".join(args)} failed:\n{r.stderr}')
    return r.stdout


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as h:
        return h.read()


def write(rel, text):
    with open(os.path.join(ROOT, rel), 'w', encoding='utf-8', newline='\n') as h:
        h.write(text)


def key(v):
    m = SEMVER.match(v)
    core = tuple(int(x) for x in m.groups()[:3])
    pre = m.group(4)
    # a pre-release sorts before its release: (core, 0, parts) < (core, 1)
    if pre is None:
        return core + (1,)
    parts = tuple((0, int(p)) if p.isdigit() else (1, p) for p in pre.split('.'))
    return core + (0, parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('version')
    ap.add_argument('--tag', action='store_true', help='also create the annotated tag vX.Y.Z')
    ap.add_argument('--dry', action='store_true', help='print the changes, write nothing')
    ap.add_argument('--allow-dirty', action='store_true')
    ap.add_argument('--date', default=datetime.date.today().isoformat(), help='the date for the changelog heading')
    a = ap.parse_args()
    v = a.version[1:] if a.version.startswith('v') else a.version
    if not SEMVER.match(v):
        sys.exit(f'{v} is not a semantic version (x.y.z or x.y.z-rc.1)')
    prerelease = '-' in v

    cmake = read('CMakeLists.txt')
    m = re.search(r'^project\((\w+) VERSION (\d+\.\d+\.\d+)\)', cmake, re.M)
    if not m:
        sys.exit('CMakeLists.txt has no "project(<Suite> VERSION x.y.z)" line')
    suite, current = m.group(1), m.group(2)
    if key(v) <= key(current) and not (prerelease and v.split('-')[0] == current):
        sys.exit(f'{v} is not newer than the current version {current}')
    # The display name: the suite's COMPANY_NAME in plugin/plugin.cmake (a suite of one plug-in) or the first
    # plugins/<Id>/plugin.cmake; a repository without either takes PRODUCT_NAME from CMakeLists.txt instead.
    pdir = os.path.join(ROOT, 'plugins')
    if os.path.isfile(os.path.join(ROOT, 'plugin', 'plugin.cmake')):
        first = [os.path.join('plugin', 'plugin.cmake')]
    elif os.path.isdir(pdir):
        first = [os.path.join('plugins', d, 'plugin.cmake') for d in sorted(os.listdir(pdir))
                 if os.path.isfile(os.path.join(pdir, d, 'plugin.cmake'))]
    else:
        first = []
    if first:
        name = re.search(r'^\s*COMPANY_NAME\s+"([^"]+)"', read(first[0]), re.M)
    else:
        name = re.search(r'^\s*PRODUCT_NAME\s+"([^"]+)"', cmake, re.M)
    display = name.group(1) if name else suite

    if not a.allow_dirty and git('status', '--porcelain').strip():
        sys.exit('the working tree has uncommitted changes: commit or stash them first (or --allow-dirty)')
    if git('tag', '-l', f'v{v}').strip():
        sys.exit(f'tag v{v} exists already')

    changelog = read('CHANGELOG.md')
    if not re.search(r'^## Unreleased\s*$', changelog, re.M):
        sys.exit('CHANGELOG.md has no "## Unreleased" section')
    body = re.split(r'^## ', changelog, flags=re.M)[1]
    if len([l for l in body.splitlines()[1:] if l.strip()]) == 0:
        sys.exit('the "## Unreleased" section is empty: write the changes first')

    core = v.split('-')[0]
    new_cmake = cmake.replace(m.group(0), f'project({suite} VERSION {core})', 1)
    if prerelease:
        new_changelog = changelog
    else:
        new_changelog = re.sub(r'^## Unreleased[ \t]*$', f'## Unreleased\n\n## {v} ({a.date})', changelog, count=1, flags=re.M)

    print(f'{suite}: {current} -> {core}' + (f' (pre-release {v})' if prerelease else ''))
    print(f'  CMakeLists.txt: project({suite} VERSION {core})')
    print(f'  CHANGELOG.md:   ' + ('unchanged for a release candidate' if prerelease else f'## Unreleased -> ## {v} ({a.date}), a fresh ## Unreleased above it'))
    print(f'  commit:         Release {v}')
    if a.tag:
        print(f'  tag:            v{v} "{display} {v}"')
    if a.dry:
        return

    if new_cmake != cmake:
        write('CMakeLists.txt', new_cmake)
    if new_changelog != changelog:
        write('CHANGELOG.md', new_changelog)
    if git('status', '--porcelain').strip():
        git('add', 'CMakeLists.txt', 'CHANGELOG.md')
        git('-c', 'user.name=Maymi', '-c', 'user.email=159730528+MaymiPR@users.noreply.github.com',
            'commit', '-q', '-m', f'Release {v}')
        print(f'committed Release {v}')
    else:
        print('nothing to commit (the version was already set)')
    if a.tag:
        git('-c', 'user.name=Maymi', '-c', 'user.email=159730528+MaymiPR@users.noreply.github.com',
            'tag', '-a', f'v{v}', '-m', f'{display} {v}')
        print(f'tagged v{v}')
        print(f'push with:  git push origin main v{v}')
    else:
        print('push with:  git push origin main      (then --tag once the tag is approved)')


if __name__ == '__main__':
    main()
