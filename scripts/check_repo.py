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


def check_file(rel, data, old_names, findings):
    text = data.decode('utf-8', 'replace')
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
    findings, checked = [], 0
    for rel in files:
        data = read_bytes(rel)
        if data is None or b'\0' in data[:8192]:
            continue
        if a.fix and fix_file(rel, data):
            print(f'{rel}: fixed whitespace and line endings')
            data = read_bytes(rel)
        checked += 1
        check_file(rel, data, old_names, findings)
    if a.release:
        release_rules(files, findings)
    for f in findings:
        print(f)
    print(f'check_repo: {checked} text files, {len(findings)} finding(s)'
          + (f', {len(old_names)} old-name pattern(s)' if old_names else '') + (', release rules' if a.release else ''))
    sys.exit(1 if findings else 0)


if __name__ == '__main__':
    main()
