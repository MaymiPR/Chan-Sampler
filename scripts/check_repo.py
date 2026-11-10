"""Repository checks: every tracked text file against the hygiene and documentation rules of STANDARDS.md.

    python scripts/check_repo.py              # every rule below; exit 1 on any finding
    python scripts/check_repo.py --release    # also the release rules
    python scripts/check_repo.py --fix        # rewrite the W findings (trailing whitespace, stray CRLF), then check

Each finding prints as `path:line: rule: text`. The files are the ones `git ls-files` lists; a file with a null byte
in its first 8 KB is binary and skipped. Rules:

    P  an absolute or machine path: a drive letter, then a colon, a slash and Users; /Users/ or /home/ and a name;
       the per-user application data folder; the local application data and user profile variables;
       the placeholder for a user name
    N  an old name: one regular expression per line of old-names.txt at the repository root, when that file is tracked
    Q  an attribution or quotation form: words, a rule or a decision credited to the person the work is for, a
       request credited to someone, a dated note in brackets naming that person, a transcript line that starts with
       User and a colon
    E  an e-mail address other than a users.noreply.github.com address
    D  Markdown only: a heading that is a date or names a round, session, log, diary or progress; a line that starts
       with an ISO date and a colon; an em or en dash
    W  trailing whitespace; CRLF line endings in a file that is not .iss or .ps1
    V  a vendor word (the vendor: lines of .provenance, any letter case) in a code identifier or a file name: in a code
       file, a CamelCase or snake_case name whose parts spell the vendor word (VendorDrag, render_vendor); in every
       tracked path, a file or folder name with the vendor word among its parts. Word-boundary aware: a plain word in
       prose or a comment is not an identifier, and a vendor word inside a longer part (CLA in CLAP) is not a match
    H  plugins/<Id>/nulltest/ without its harness <ab>_ref.py (<ab>: the plug-in's <Ab>Render abbreviation in lower
       case)
    I  a numbered iteration script: a script or native helper under nulltest/ or scripts/ whose name ends in a number
       right after a letter (fit_release2.py, gain_fast6.c); the version whose output ships keeps a name that says
       what it does, the others leave the tree

Licence texts (LICENSE, COPYING and the THIRD-PARTY-NOTICES files) are verbatim third-party text: the P, Q, E and D
rules skip them. --release also requires a `## Provenance` section in every plugins/*/MEASUREMENTS.md and
`## Unreleased` as the first section of CHANGELOG.md. Standard library only.
"""
import argparse, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The patterns are written so that this file does not match itself (character classes, \s, escapes).
P_RULES = [
    re.compile(r'\b[A-Za-z]:(?:\\{1,2}|/{1,2})Users\b', re.I),
    re.compile(r'(?<![\w.$])/(?:Users|home)/[A-Za-z0-9_][\w.-]*'),
    re.compile(r'\bAppDat[a]\b'),
    re.compile(r'%LOCALAPPDAT[A]%|%USERPROFIL[E]%', re.I),
    re.compile(r'\bAI\sProjects\b|\bMaster\sSessions\b'),
    re.compile(r'<use[r]>'),
]
Q_RULES = [
    re.compile(r"\bthe\suser\s(?:said|says|asked|wants|wanted)\b", re.I),
    re.compile(r"\bthe\suser['\u2019]s\s(?:rule|words?|yes|OK|call|decision)\b", re.I),
    re.compile(r'\bas\srequested\b', re.I),
    re.compile(r'\bper\sthe\suser\b', re.I),
    re.compile(r"\bat\sthe\suser['\u2019]s\sword\b", re.I),
    re.compile(r'\(\s*user\b[^)\n]*\d{4}-\d{2}-\d{2}', re.I),
    re.compile(r'^\s*(?:[>*-]\s*)?\**User\**\s*:'),
]
E_RULE = re.compile(r'[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}')
NOREPLY = re.compile(r'@users\.noreply\.github\.com$', re.I)
D_HEADING = re.compile(r'^\s{0,3}#{1,6}\s+(.*)$')
D_DATE_HEADING = re.compile(r'^\[?\d{4}-\d{2}(?:-\d{2})?\]?\s*(?:$|[:(,.-])')
D_WORDS = re.compile(r'\b(?:rounds?|sessions?|logs?|diary|diaries|progress)\b', re.I)
D_DATE_LINE = re.compile(r'^\s*(?:[-*]\s+)?\**\d{4}-\d{2}-\d{2}\**\s*:')
DASHES = re.compile('[\u2013\u2014]')
LICENCE = re.compile(r'(?:^|/)(?:LICEN[CS]E|COPYING)[^/]*$|(?:^|/)THIRD-PARTY-NOTICES[^/]*$', re.I)
CRLF_OK = ('.iss', '.ps1')
CODE_EXT = ('.h', '.hpp', '.cpp', '.c', '.mm', '.py', '.cmake', '.ps1', '.sh', '.bat', '.iss', '.yml', '.yaml', '.js')
IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
SCRIPT_EXT = ('.py', '.ps1', '.sh', '.bat', '.c', '.cpp', '.h')
NUMBERED = re.compile(r'[A-Za-z][0-9]+$')


def git_files():
    r = subprocess.run(['git', '-C', ROOT, 'ls-files', '-z'], capture_output=True)
    if r.returncode != 0:
        sys.exit('git ls-files failed: run this inside a git repository (git init and git add first)')
    return [f for f in r.stdout.decode('utf-8', 'replace').split('\0') if f]


def read_bytes(rel):
    try:
        with open(os.path.join(ROOT, rel), 'rb') as h:
            return h.read()
    except OSError:
        return None   # listed by git but deleted in the working tree


def old_name_rules(files):
    if 'old-names.txt' not in files or not os.path.isfile(os.path.join(ROOT, 'old-names.txt')):
        return []
    rules = []
    with open(os.path.join(ROOT, 'old-names.txt'), encoding='utf-8') as h:
        for n, line in enumerate(h, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            try:
                rules.append(re.compile(line))
            except re.error as e:
                sys.exit(f'old-names.txt:{n}: not a regular expression ({e})')
    return rules


def name_parts(word):
    """The lower-case parts of a name: split at every character that is not a letter or a digit, between a lower-case
    letter and a capital, before the last capital of a run followed by lower case, and between letters and digits."""
    parts = []
    for chunk in re.split(r'[^A-Za-z0-9]+', word):
        parts += re.findall(r'[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+', chunk)
    return [x.lower() for x in parts if x]


def vendor_words(root=ROOT):
    """The vendor words of .provenance (its vendor: lines), each as the run of name parts it stands for."""
    path = os.path.join(root, '.provenance')
    words = []
    if os.path.isfile(path):
        with open(path, encoding='utf-8') as h:
            for line in h:
                if line.strip().startswith('vendor:'):
                    for w in line.strip()[7:].split(','):
                        if name_parts(w):
                            words.append((w.strip(), name_parts(w)))
    return words


def vendor_in(parts, vendor):
    """The vendor word that whole parts in a row spell (Maxx + Bass, or one part maxxbass), or None."""
    for word, run in vendor:
        joined = ''.join(run)
        for i in range(len(parts)):
            spelled = ''
            for part in parts[i:]:
                spelled += part
                if spelled == joined:
                    return word
                if len(spelled) >= len(joined):
                    break
    return None


def is_identifier(token):
    """A CamelCase or snake_case name (a single plain word is prose, not an identifier)."""
    core = token.strip('_')
    return '_' in core or bool(re.search(r'[a-z0-9][A-Z]|[A-Z]{2}[a-z]', core))


def tree_rules(files, findings, vendor):
    """The rules on the tracked tree rather than on a file's text: V for file and folder names, H, I."""
    if vendor:
        seen = set()
        for rel in files:
            if rel == '.provenance':
                continue
            for comp in rel.split('/'):
                word = vendor_in(name_parts(os.path.splitext(comp)[0] if comp == rel.split('/')[-1] else comp), vendor)
                if word and (comp, word) not in seen:
                    seen.add((comp, word))
                    findings.append(f'{rel}:1: V: the vendor word {word} in the name {comp}')
    plugins = sorted({f.split('/')[1] for f in files if f.startswith('plugins/') and f.count('/') >= 3
                      and f.split('/')[2] == 'nulltest'})
    for plugin in plugins:
        cmake = ''
        for name in ('plugin.cmake', 'CMakeLists.txt'):
            data = read_bytes(f'plugins/{plugin}/{name}') if f'plugins/{plugin}/{name}' in files else None
            cmake += data.decode('utf-8', 'replace') if data else ''
        m = (re.search(r'foreach\(t\s+(\w+?)Render\s', cmake)
             or re.search(r'(?:juce_add_console_app|add_executable)\((\w+?)Render\b', cmake))
        harness = [f for f in files if re.fullmatch(rf'plugins/{re.escape(plugin)}/nulltest/[a-z]{{2}}_ref\.py', f)]
        want = f'{m.group(1).lower()}_ref.py' if m and len(m.group(1)) == 2 else None
        if want and f'plugins/{plugin}/nulltest/{want}' not in files:
            findings.append(f'plugins/{plugin}/nulltest:1: H: no harness {want} (the renderer is {m.group(1)}Render)')
        elif not want and not harness:
            findings.append(f'plugins/{plugin}/nulltest:1: H: no harness <ab>_ref.py')
    for rel in files:
        parts = rel.split('/')
        stem, ext = os.path.splitext(parts[-1])
        if ext.lower() in SCRIPT_EXT and ('nulltest' in parts[:-1] or 'scripts' in parts[:-1]) and NUMBERED.search(stem):
            findings.append(f'{rel}:1: I: a numbered iteration script ({parts[-1]}): keep the one whose output ships, '
                            f'named for what it does')


def check_file(rel, data, old_names, findings, vendor=()):
    text = data.decode('utf-8', 'replace')
    code = rel.lower().endswith(CODE_EXT) or os.path.basename(rel) == 'CMakeLists.txt'
    licence = bool(LICENCE.search(rel))
    markdown = rel.lower().endswith('.md')
    crlf_ok = rel.lower().endswith(CRLF_OK)
    in_code = False
    for n, raw in enumerate(text.split('\n'), 1):
        line = raw[:-1] if raw.endswith('\r') else raw

        def add(rule, what):
            findings.append(f'{rel}:{n}: {rule}: {what.strip()[:120]}')

        if raw.endswith('\r') and not crlf_ok:
            add('W', 'CRLF line ending')
        if line != line.rstrip():
            add('W', 'trailing whitespace')
        if rel != 'old-names.txt':
            for r in old_names:
                if r.search(line):
                    add('N', line)
                    break
        if licence:
            continue
        if vendor and code:
            for token in set(IDENTIFIER.findall(line)):
                word = vendor_in(name_parts(token), vendor) if is_identifier(token) else None
                if word:
                    add('V', f'the vendor word {word} in the identifier {token}')
        if any(r.search(line) for r in P_RULES):
            add('P', line)
        if any(r.search(line) for r in Q_RULES):
            add('Q', line)
        for m in E_RULE.finditer(line):
            if not NOREPLY.search(m.group(0)):
                add('E', line)
                break
        if not markdown:
            continue
        if line.lstrip().startswith(('```', '~~~')):
            in_code = not in_code
            continue
        if DASHES.search(line):
            add('D', line)
        if in_code:
            continue
        h = D_HEADING.match(line)
        if h and (D_DATE_HEADING.match(h.group(1).strip()) or D_WORDS.search(h.group(1))):
            add('D', line)
        elif D_DATE_LINE.match(line):
            add('D', line)


def fix_file(rel, data):
    crlf_ok = rel.lower().endswith(CRLF_OK)
    text = data.decode('utf-8')
    eol = '\r\n' if crlf_ok and '\r\n' in text else '\n'
    lines = text.replace('\r\n', '\n').split('\n')
    fixed = eol.join(l.rstrip() for l in lines)
    if fixed != text:
        with open(os.path.join(ROOT, rel), 'w', encoding='utf-8', newline='') as h:
            h.write(fixed)
        return True
    return False


def release_rules(files, findings):
    for rel in files:
        if re.fullmatch(r'plugins/[^/]+/MEASUREMENTS\.md', rel):
            data = read_bytes(rel)
            if data is not None and not re.search(r'^## Provenance\s*$', data.decode('utf-8', 'replace'), re.M):
                findings.append(f'{rel}:1: release: no "## Provenance" section')
    data = read_bytes('CHANGELOG.md') if 'CHANGELOG.md' in files else None
    if data is None:
        findings.append('CHANGELOG.md:1: release: no CHANGELOG.md')
        return
    for n, line in enumerate(data.decode('utf-8', 'replace').splitlines(), 1):
        if line.startswith('## '):
            if line.rstrip() != '## Unreleased':
                findings.append(f'CHANGELOG.md:{n}: release: the first section is not "## Unreleased"')
            return
    findings.append('CHANGELOG.md:1: release: no "## Unreleased" section')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--release', action='store_true', help='also the release rules')
    ap.add_argument('--fix', action='store_true', help='fix the W findings in place, then check')
    a = ap.parse_args()
    files = git_files()
    old_names = old_name_rules(files)
    vendor = vendor_words()
    findings, checked = [], 0
    for rel in files:
        data = read_bytes(rel)
        if data is None or b'\0' in data[:8192]:
            continue
        if a.fix and fix_file(rel, data):
            print(f'{rel}: fixed whitespace and line endings')
            data = read_bytes(rel)
        checked += 1
        check_file(rel, data, old_names, findings, vendor)
    tree_rules(files, findings, vendor)
    if a.release:
        release_rules(files, findings)
    for f in findings:
        print(f)
    print(f'check_repo: {checked} text files, {len(findings)} finding(s)'
          + (f', {len(old_names)} old-name pattern(s)' if old_names else '') + (f', {len(vendor)} vendor word(s)' if vendor else '')
          + (', release rules' if a.release else ''))
    sys.exit(1 if findings else 0)


if __name__ == '__main__':
    main()
