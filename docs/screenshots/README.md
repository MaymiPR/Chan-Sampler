# Screenshots

The images the README and the plug-in pages show. Rules: PNG, the editor at its design size at 100 %, no window
chrome, no cursor, the default preset unless the shot shows a feature. The suite's one snapshot tool
(`tools/Snapshot.cpp`, built for each plug-in as `<Stem>Snapshot`) renders an editor without a window and gives the
same image on every machine:

```text
build/<Stem>Snapshot_artefacts/Release/<Stem>Snapshot docs/screenshots/<pluginid>.png
build/<Stem>Snapshot_artefacts/Release/<Stem>Snapshot docs/screenshots/<pluginid>-<view>.png paramId=value
```

| File | What |
|---|---|
| `hero.png` | the suite's plug-ins side by side, 24 px apart, at most 1800 px wide (the README's first image) |
| `<pluginid>.png` | one plug-in at its design size (its page's image) |
| `<pluginid>-<view>.png` | one view or feature of one plug-in (optional) |
| `<suite>-<view>.png` | a view the suite's plug-ins share, such as a toolbar (optional) |
| `<pluginid>.gif` | at most 8 s and 5 MB, 20 to 30 fps (optional) |

`<pluginid>` is the plug-in id in lower case, `<suite>` the suite id in lower case. Until the first editor exists,
`hero.png` is a placeholder. CI's Windows job renders every plug-in's snapshot on each push and keeps them as the
`screenshots` artifact for 14 days, so a release's screenshots can be taken from the run that built it.