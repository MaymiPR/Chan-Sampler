# Screenshots

The images the README and the plug-in page show. Rules: PNG, no window chrome, no cursor. The snapshot tool renders
the editor without a window and gives the same image on every machine:

```text
build/ChanSamplerSnapshot_artefacts/Release/ChanSamplerSnapshot docs/screenshots/chansampler.png
python plugins/ChanSampler/scripts/make_screenshots.py
```

The first line is the standard call (a synthesised drum hit, the Sample sheet; `--scale=2` for a double-size image).
The second makes every image below at 1.6x from a synthesised drum loop.

| File | What |
|---|---|
| `chansampler.png` | the Sample page with the drum loop, its loop points and the precomputed effects (the README's first image and the plug-in page's) |
| `chansampler-effects.png` | the precomputed effects' second page |
| `chansampler-envelope.png` | the Envelope page: volume envelope, LFO, filter and root note keyboard |
| `chansampler-misc.png` | the Misc page: levels, polyphony, tracking, cut groups, arpeggiator and echo |
| `chansampler-spectrum.png` | the sample view as a spectrogram |
| `chansampler-waveforms.png` | the sample view in its three colour modes, one above the other |

The images are named after the plug-in id in lower case. CI's Windows job renders the snapshot on each push and
keeps it as the `screenshots` artifact for 14 days.
