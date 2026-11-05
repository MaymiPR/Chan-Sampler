"""Write the release notes for a version: its section of CHANGELOG.md, verbatim, followed by the fixed footer
(system requirements, where the install steps are, whether the files are signed). release.yml runs it; by hand:

    python scripts/release_notes.py 1.2.0 --out notes.md
    python scripts/release_notes.py 1.2.0-rc.1 --out notes.md        # a candidate: the "## Unreleased" section
    python scripts/release_notes.py 1.2.0 --no-footer                # the section alone, to stdout

--signed-windows / --signed-macos take the words the release jobs recorded ("signed", "signed and notarized",
"unsigned") and are printed as they are. --readme is the link the footer's install sentence points at.
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def section(changelog, version):
    """The text under `## <version>` (any "(date)" suffix) up to the next `## `; a pre-release reads Unreleased."""
    lines = changelog.replace('\r\n', '\n').split('\n')
    heads = [i for i, l in enumerate(lines) if l.startswith('## ')]
    want = 'Unreleased' if '-' in version else version
    for n, i in enumerate(heads):
        title = lines[i][3:].strip()
        if title == want or title.startswith(want + ' ('):
            end = heads[n + 1] if n + 1 < len(heads) else len(lines)
            return '\n'.join(lines[i + 1:end]).strip('\n')
    return None


def footer(readme, win, mac):
    def state(s):
        return s.strip() or 'unsigned'
    lines = [
        '---',
        '',
        '**System requirements:** Windows 10 or later (x64); macOS 10.13 or later (Apple Silicon and Intel); Linux x86-64 '
        'with glibc 2.35 or later. VST3, CLAP, AU (macOS) and standalone hosts.',
        '',
        f'**Install:** download the file for your system below and follow the [Quick start]({readme}). Every file is '
        f'listed in `SHA256SUMS.txt`. Windows files: {state(win)}; macOS installer: {state(mac)}; the Linux tarball is '
        'checksummed only. An unsigned file shows the first-launch warning described in the README.',
    ]
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('version')
    ap.add_argument('--changelog', default=os.path.join(ROOT, 'CHANGELOG.md'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--no-footer', action='store_true')
    ap.add_argument('--readme', default='README.md#quick-start')
    ap.add_argument('--signed-windows', default='unsigned')
    ap.add_argument('--signed-macos', default='unsigned')
    a = ap.parse_args()
    v = a.version[1:] if a.version.startswith('v') else a.version
    with open(a.changelog, encoding='utf-8') as h:
        changelog = h.read()
    body = section(changelog, v)
    if body is None or not body.strip():
        suite = re.search(r'^project\((\w+) VERSION', open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read(), re.M)
        body = f'{suite.group(1) if suite else "Release"} {v}'
        print(f'warning: CHANGELOG.md has no section for {v}; the notes carry the title only', file=sys.stderr)
    text = body if a.no_footer else body + '\n\n' + footer(a.readme, a.signed_windows, a.signed_macos)
    text = text.rstrip('\n') + '\n'
    if a.out:
        with open(a.out, 'w', encoding='utf-8', newline='\n') as h:
            h.write(text)
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
