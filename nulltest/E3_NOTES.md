# FL e3 generic (elastique): black-box measurements

Measured with FL renders only (nullfl.py harness; FL's own Resample render is
the reference source). Everything below is behaviour, not code.

## Architecture

- **Pitch = stretch, then resample.** With PITCH, FL time-stretches at the
  source rate and reads the result back with a short interpolator: the -700
  cents output rolls off above 13 kHz and keeps -21 dB at 22 kHz exactly like
  our cubic FIFO read (within 1 dB), not a band-limited resampler.
- **MUL = 1 is transparent** (output == input). TIME / MUL with PITCH 0 show
  the stretcher alone (no interpolation): use MUL (event 104, 2^(raw/10000))
  to probe it. MUL 1.5 = raw 5850, 0.667 = raw -5850.
- FL ignores PITCH in Resample mode when it comes from a project: to render
  FL's tape version, play the note key instead.
- **Two bands, split at ~1.48 kHz (steep: on saws harmonics up to 1470 Hz
  take the low path, 1500 Hz and up the grain path).** Above: the output is literally
  pieces of the source (coherence 0.99+, gain 1.00). Below: incoherent with
  any grain offset even with 1024-sample windows -> spectral processing
  (phase-vocoder like). On saws the low band keeps the waveform shape
  (-23..-29 dB vs a shifted source) at its own offset, independent of the
  high band's.

## High band: pitch-synchronous grains (PSOLA-like)

- Grains are played 1:1 (constant source offset) and spliced once per
  estimated pitch period: 147 Hz saw (period 300) -> splices exactly every
  300 output samples; 100 Hz saw (441) -> about every 441.
- When the drift from the ideal position exceeds about half a period the
  offset jumps by one period, symmetric around the ideal (old offset about
  +J/2, new about -J/2).
- On noise the "period" is whatever the estimator returns: jumps of 94..548
  samples; it is not the plain autocorrelation peak of the source (full, low
  or high band; 512..2048 windows).
- Crossfades at offset changes fit a linear ramp of about 0.7 x the jump
  (consistent with 2-period Hann grains overlapping by one period).
- Grain positions are fractional: clicks come out at fractional offsets
  (+0.9, +0.65, +0.45 samples over three clicks at MUL 1.5, drifting slowly:
  FL's ratio differs from 2^(raw/10000) by ~1e-5).

## Transients

- Clicks land on time (0..2 samples), as one copy of the source, but louder:
  x1.40-1.45 at MUL 1.5, x1.26-1.31 at MUL 0.667, in both bands. A noise
  onset starts at x1.16 and settles to x1.0 over ~500 samples.

## Low band vs ours

- On saws our current vocoder's low band is already -17..-26 dB from FL's
  (FL's low band keeps the source's phase at the same time, 1-2 samples
  late, like a plain phase vocoder). At MUL 0.667 ours drifts against FL by
  ~0.1% (to fix).

## Prototype (scratchpad, Python)

- Hybrid = our vocoder below 1.485 kHz + PSOLA above (period from the
  normalized autocorrelation, 1024 window, 90..600 lags; grain centred on
  each onset at its ideal time) + cubic resample: against the 30 in-FL pitch
  cases envelope 2.02 -> 1.78 dB, spectrum 2.47 -> 2.46 dB, null -1.85 ->
  -2.21 dB. With the true period on a 147 Hz saw the grain band is -20..-25
  dB from FL's (the rest is FL's own splice glitches).
- FL's period estimator is NOT the plain autocorrelation of the source at
  the splice (full / low / high band, 256..2048 windows, anchors -3000..+3000
  samples: at most 2 of 8 noise jumps reproduced). Without it the grain band
  cannot null on real material.

## In the plugin (TimeStretch, e3 generic / Auto)

- Vocoder bins below 1.485 kHz; above, PSOLA grains from a cached
  high-passed copy of the source (overlap-save, the complement of a Hann
  windowed sinc as long as the vocoder frame), period from the normalised
  autocorrelation (reused within 512 samples). From an attack within the
  crossover filter's reach the grains play 1:1 in the attack's timing (its
  ringing lines up with the vocoder's band); drift caught up after it.
- Fixed on the way (all vocoder modes): the start alignment used the
  quarter-frame hop instead of the hop in use (steady content 256 samples
  early unpitched); the note-start lock was skipped when the first window
  began after the start (pitch down) and its target position disagreed with
  the alignment afterwards.
- In FL, 30 pitch cases: envelope 2.01 -> 1.59 dB, spectrum 2.44 -> 2.08 dB,
  null (level fitted) -2.00 -> -1.93 dB. CPU: e3 generic 28 -> 38% of a core
  for 32 voices.

## Open

- The period estimator (range, method, resolution).
- Grain window shape, how analysis marks are placed.
- The low band's frame / hop / phase handling.
- The transient gain rule.
