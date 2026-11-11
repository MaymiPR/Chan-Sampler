"""Build the suite's landing page (GitHub Pages) from README.md, each plug-in's README and docs/: one static HTML
page per Markdown document, the branding and screenshot images copied beside them, relative links rewritten.
pages.yml runs it after every published release; by hand:

    python -m pip install markdown==3.7
    python scripts/make_site.py --out site --repo owner/Example-Suite
    python -m http.server --directory site 8000        # then open http://localhost:8000

The page is the README's content in the suite's accent colour (docs/branding/colours.json), nothing more: the README
is the product page, and the site must not say anything the README does not.
"""
import argparse, json, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{description}">
<meta property="og:title" content="{title}">
<meta property="og:description" content="{description}">
<meta property="og:image" content="{social}">
<style>
:root {{ --accent: #{accent}; --bg: #ffffff; --fg: #1f2328; --muted: #656d76; --line: #d0d7de; --code: #f6f8fa; }}
@media (prefers-color-scheme: dark) {{ :root {{ --accent: #{accent_dark}; --bg: #0d1117; --fg: #e6edf3; --muted: #8b949e; --line: #30363d; --code: #161b22; }} }}
html {{ background: var(--bg); color: var(--fg); font: 16px/1.6 -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }}
body {{ margin: 0 auto; max-width: 960px; padding: 24px 16px 64px; }}
a {{ color: var(--accent); }}
img {{ max-width: 100%; height: auto; }}
h1, h2, h3 {{ line-height: 1.25; margin-top: 1.6em; }}
h2 {{ border-bottom: 1px solid var(--line); padding-bottom: .3em; }}
table {{ border-collapse: collapse; margin: 1em 0; width: 100%; }}
th, td {{ border: 1px solid var(--line); padding: 6px 12px; vertical-align: top; }}
code, pre {{ background: var(--code); border-radius: 6px; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 85%; }}
code {{ padding: .15em .35em; }}
pre {{ overflow: auto; padding: 12px 16px; }}
pre code {{ padding: 0; }}
blockquote {{ border-left: 4px solid var(--line); color: var(--muted); margin: 1em 0; padding: 0 1em; }}
nav {{ color: var(--muted); font-size: 90%; margin-bottom: 16px; }}
footer {{ border-top: 1px solid var(--line); color: var(--muted); font-size: 85%; margin-top: 48px; padding-top: 16px; }}
</style>
</head>
<body>
<nav><a href="{home}">Home</a> &middot; <a href="https://github.com/{repo}">GitHub</a> &middot; <a href="https://github.com/{repo}/releases/latest">Download</a></nav>
{body}
<footer>Generated from the repository's README by scripts/make_site.py. <a href="https://github.com/{repo}">Source and issues on GitHub</a>.</footer>
</body>
</html>
"""


def md_to_html(text):
    try:
        import markdown
    except ImportError:
        sys.exit('python -m pip install markdown==3.7')
    return markdown.markdown(text, extensions=['tables', 'fenced_code', 'toc', 'md_in_html'])


def rewrite_links(html, depth):
    """Relative .md links become .html; a link to a plug-in folder (plugins/<Id>, or plugin in a suite of one
    plug-in) becomes that plug-in's page; others point at GitHub."""
    up = '../' * depth
    html = re.sub(r'href="(plugins/([^/".#]+))(#[^"]*)?"', lambda m: f'href="{up}plugins/{m.group(2)}/index.html{m.group(3) or ""}"', html)
    html = re.sub(r'href="plugin/?(#[^"]*)?"', lambda m: f'href="{up}plugin/index.html{m.group(1) or ""}"', html)
    # the site keeps the source tree's relative layout: a README page is that folder's index.html
    html = re.sub(r'href="([^":#]*/)?README\.md(#[^"]*)?"', lambda m: f'href="{m.group(1) or ""}index.html{m.group(2) or ""}"', html)
    html = re.sub(r'href="([^":#]+)\.md(#[^"]*)?"', lambda m: f'href="{m.group(1)}.html{m.group(2) or ""}"', html)
    return html


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default='site')
    ap.add_argument('--repo', required=True, help='owner/name')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    colours = {'accent': '6c8cff', 'accentDark': '8fa6ff'}
    cj = os.path.join(ROOT, 'docs', 'branding', 'colours.json')
    if os.path.isfile(cj):
        with open(cj, encoding='utf-8') as h:
            colours.update(json.load(h))
    for sub in ('docs/branding', 'docs/screenshots'):
        src = os.path.join(ROOT, sub)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(out, sub))
    social = ''
    for f in os.listdir(os.path.join(ROOT, 'docs', 'branding')) if os.path.isdir(os.path.join(ROOT, 'docs', 'branding')) else []:
        if f.endswith('-social.png'):
            social = f'https://{a.repo.split("/")[0]}.github.io/{a.repo.split("/")[1]}/docs/branding/{f}'

    docs = [('README.md', 'index.html', 0)]
    plugins = os.path.join(ROOT, 'plugins')
    # the plug-in folders: plugin in a suite of one plug-in (plugin/plugin.cmake), else each plugins/<Id>
    if os.path.isfile(os.path.join(ROOT, 'plugin', 'plugin.cmake')):
        folders = ['plugin']
    elif os.path.isdir(plugins):
        folders = [f'plugins/{p}' for p in sorted(os.listdir(plugins)) if os.path.isdir(os.path.join(plugins, p))]
    else:
        folders = []
    for f in folders:
        depth = f.count('/') + 1
        if os.path.isfile(os.path.join(ROOT, f, 'README.md')):
            docs.append((f'{f}/README.md', f'{f}/index.html', depth))
            for d in ('NULLTESTS.md', 'MEASUREMENTS.md', 'RESEARCH.md'):
                if os.path.isfile(os.path.join(ROOT, f, d)):
                    docs.append((f'{f}/{d}', f'{f}/{d[:-3]}.html', depth))
    for src, dst, depth in docs:
        with open(os.path.join(ROOT, src), encoding='utf-8') as h:
            text = h.read()
        title = re.search(r'^# (.+)$', text, re.M)
        title = title.group(1).strip() if title else a.repo.split('/')[1]
        desc = ''
        for para in re.split(r'\n\s*\n', text):
            p = para.strip()
            if p and not p.startswith(('#', '<', '|', '>', '!', '-', '```')):
                desc = re.sub(r'[*_`\[\]]', '', p.replace('\n', ' '))[:200]
                break
        body = rewrite_links(md_to_html(text), depth)
        if depth:
            body = body.replace('src="docs/', 'src="' + '../' * depth + 'docs/')
        page = PAGE.format(title=title, description=desc.replace('"', '&quot;'), social=social, accent=colours['accent'],
                           accent_dark=colours.get('accentDark', colours['accent']), repo=a.repo, body=body,
                           home='../' * depth + 'index.html' if depth else 'index.html')
        path = os.path.join(out, dst)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as h:
            h.write(page)
        print('wrote', dst)
    with open(os.path.join(out, '.nojekyll'), 'w') as h:
        h.write('')


if __name__ == '__main__':
    main()
