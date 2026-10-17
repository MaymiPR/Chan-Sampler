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

## Period estimator (round 2)

- Noise plus one echo (x = w + w(n - D)), MUL 1.5: every jump equals D for
  D = 40 ... 512 exactly; D = 530 and up gives random jumps, so the search
  range is 40..512 samples. D = 20 / 30 give constant 50 / 256 (fallbacks).
- On plain noise (random jumps) no autocorrelation definition reproduces FL's
  choices: best is the normalised autocorrelation of the high band, 512
  samples from the grain centre on (24% of jumps; chance < 1%). Not a
  lag-length window, not a fixed analysis grid, not a "first peak" rule;
  the low band alone matches worst.
- With a fixed period FL jumps every 3 grains at x1.5 (drift cycle -P/3, 0,
  +P/3), as the half-period rule gives. Grain centres from Hann crossfade
  fits: a jump repeats the same source position.
- Note start: FL plays 1:1 for 336 / 368 / 528 / 704 / ~1000 / ~1150 samples
  at x0.5 / 0.667 / 0.8 / 1.25 / 1.5 / 2, then jumps to within ~100 samples of
  the ideal position. Noise-burst onsets inside a sample are NOT placed at
  their ideal time (+152 samples at x1.5, -99 at x0.667), unlike clicks.
- FL's output on a perfect 300-sample saw has one small glitch per period
  (its grains are not seamless): not modelled.
- In the plugin: range 40..512, high-band estimator on the grain cache, the
  measured note-start hold for every attack. In FL (30 pitch cases):
  envelope 1.59 -> 1.57 dB, spectrum 2.08 -> 1.94 dB, null -1.93 -> -1.90.

## Transient boost (round 3)

- Single-sample clicks after silence (e3_imp.wav) come out as one clean copy
  but louder, in both bands. MUL (no resampling): x1.2 at x0.5 / 0.667 / 0.8,
  x1.05-1.41 at x1.25, ~x1.38 at x1.5, x1.36-1.63 at x2 - it varies from
  click to click (at x2: 1.63, 1.60, 1.49, 1.36 for four identical clicks).
  PITCH (after dividing out the cubic read's own energy change): ~x1.2 down,
  x1.3-1.4 up. The low band alone: x1.46 at x1.5, x1.6 at x2.
- The same clicks on steady noise (e3_impn.wav) are NOT treated as attacks:
  placed up to +-190 samples off and not boosted.
- A gain model (x1.2 .. x1.5 by stretch, flat 30% of the hold, back to 1 by
  60%) on our detected attacks did not bring the 30 pitch cases closer (env
  1.59 -> 1.60 .. 1.62), so it is not in the plugin. The rule for when FL
  boosts is still unknown.
- FL starts the 707 Kick ~x1.4 louder than the source at +-1200 (x1.1 at
  +-300); it starts the soft vocal at x0.2-0.4 (not our start lock: making
  the lock need a real attack changed nothing for the vocal and broke the
  kick).
- Found on the way: our cubic read at exactly x2 (+1200) samples only every
  second synthesised sample from an even start, so a one-sample click on an
  odd sample vanishes (FL keeps ~0.8 of its energy). Real audio is not
  affected much; FL's read grid phase differs from ours.

## Per-period "glitch" (round 4)

- On the 147 Hz saw FL's high band keeps a constant phase (no offset slips
  at grain joins); the per-period difference sits on the saw's sharp edge:
  FL renders the edge slightly softer (residual -20 dB there, -24 dB
  elsewhere). Cause: FL's output sits at a fractional offset (43.5 samples
  on the saw) and reads through an interpolator that softens the treble by
  the fraction.
- Measured per grain run on noise (MUL 1.5): |H| at 6/10/14/18 kHz against
  the fractional offset matches a 50/50 blend of Catmull-Rom and 4-point
  Lagrange (mean error 0.004; Catmull alone 0.020, Lagrange 0.023, Keys a
  -0.4 0.017, windowed sincs worse).
- The fraction drifts linearly: FL's jumps are the period + 0.025 samples at
  MUL 1.5. FL stretches at a ratio slightly below the requested one and
  resamples the rest: requested 1.500039 / 1.414214 / 1.231144 -> FL
  ~1.5 / 1.414062 / ~1.230769 (within-run drift 2.2e-5, 7.6e-5, 2.6e-4 per
  sample). No single grid or integer hop pair fits all three; the rule is
  unknown.
- For PITCH, FL's rolloff at -700 cents matches our Catmull read (blend a
  little too dull); at +700 FL loses 1-3 dB at 18-22 kHz that no kernel
  alone gives. Nothing changed in the plugin this round.

## Onset placement (round 5)

- Noise bursts after silence (e3_onsA: flat, start phase varied; e3_onsB:
  decays of 20..1000 samples, flat, 300 / 1000-sample fade-ins), MUL 1.5
  and 0.667: FL puts a reference point R samples after the burst's start on
  time and plays 1:1 around it (offset = R (alpha - 1); both bands alike).
  R = the first sample, within 400 samples of the start, that reaches 95%
  of the loudest one: 16 of 20 bursts to the sample (loudest sample alone
  15/20; centroids, smoothed energy and the low band much worse). Fast
  decays land on their start, flat noise on its peak, fade-ins late.
- The same rule puts a 1 kHz click's reference 9 samples in, which is what
  the old per-pitch attack table (-8.3 .. +3.35 output samples) encoded:
  R (1 - 1 / pitch ratio). In e3 generic the table is now 0.
- In the plugin (both bands): bursts land within 3 samples of FL's in 29 of
  40; clicks keep -16..-31 dB. In FL (30 pitch cases): null (level fitted)
  -1.90 -> -2.79 dB, raw -0.34 -> -1.51 dB; envelope 1.57 -> 1.62 dB,
  spectrum 1.94 -> 2.02 dB.
- Unexplained: 4 bursts whose R needs a shorter or longer window (it may
  follow the grain period).

## Transient boost, with the timing rule (round 6)

- Aligned on FL's attack timing, the gain during the attack hold per band
  (noise bursts, 128-sample windows): the band below the split is a clean
  copy of the source, louder: x1.1-1.4 (up to 1.64) at MUL 1.5, ~x1.0-1.3 at
  0.667, varying from burst to burst (flat bursts alone: 0.98 .. 1.64). The
  grain band is not a clean copy there and not boosted (x0.6-1.0, other
  grains mixed in). So the earlier "click boost" is mostly the low band.
- In the plugin: the vocoder's locked attack frames x1.2 (stretching) /
  x1.1 (squeezing), the measured means. In FL (30 pitch cases): null (level
  fitted) -2.79 -> -2.86 dB, raw -1.51 -> -1.44 dB, envelope 1.62 -> 1.64,
  spectrum 2.02 -> 2.01: about neutral; the per-burst variation (probably
  where the attack falls in FL's frame grid) is not modelled.

## Frame grid (round 7)

- 64 identical noise bursts stepped 8 samples through a 512-sample grid
  (e3_grid2.wav), MUL 1.5 / 0.667 / 2: FL times each burst either from its
  start or from its loudest sample 469 in, decided only by its position in
  a 512-sample grid counted from the note's start (start mod 512: 0-168 and
  456-504 from the start, 176-448 from the peak at x1.5 and x2; switch
  points 224 / 408 at x0.667).
- Model: FL's search window runs from the attack's start to the first grid
  point (offset ~410 at x1.5 / x2, ~364 at x0.667) at least ~240 / ~144
  samples later. Fits 80 of 84 bursts at x1.5; in the plugin all 64 grid
  bursts time like FL's at x0.667 (and at x1.5, apart from a test-ratio
  drift).
- It does NOT carry over to PITCH: in FL the 30 pitched cases got worse with
  it (null -2.86 -> -2.52 dB, the vocal and the groove), so the plugin uses
  the grid window only when the pitch ratio is 1 (MUL / TIME) and the
  400-sample window with PITCH. In FL (30 pitch cases) unchanged: null
  -1.44 raw / -2.86 fitted.
- The low-band gain during the hold also follows the grid phase smoothly
  (x1.5: 1.10 -> 1.24 in the start-timed range, 0.85 -> 1.59 in the
  peak-timed range); not modelled.

## Open

- The period estimator (range, method, resolution).
- Grain window shape, how analysis marks are placed.
- The low band's frame / hop / phase handling.
- The transient gain rule.
