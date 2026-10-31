# FL e3 generic (elastique): black-box measurements

Measured with FL renders only (lib/nullfl.py; FL's own Resample render is the
reference source). Everything below is behaviour, not code. The tools that
reproduce these measurements are in stretch/ (see README.md).

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

## Prototype (Python, before the plugin version; not kept)

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

## Leftovers (round 8)

- Single-sample clicks at exactly +1200 cents (e3_par.wav, 16 clicks on
  alternating even / odd samples): FL drops the same parity as ours (even
  ones vanish in both, and both drop the first few); the surviving clicks
  play at x0.53-0.82 in FL, x1.0 in ours. The round-3 note ("FL keeps ~0.8")
  came from a different start parity; nothing to change.
- The vocal's soft start ("x0.2-0.4") is timing, not a fade: FL treats the
  swell as an attack and times it from a later reference point (R 1258 vs
  our 922 at x1.5, 1137 vs 873 at x0.667), so its rise comes out later.
  Fade-in bursts are only attenuated in their first 128-256 samples (-3 to
  -12 dB). A stricter attack detector moves R towards FL's but costs the
  other cases (20-case offline score), so detection is unchanged.
- PITCH has its own grid (e3_grid2.wav at +700 / -700 cents, mode 1): bursts
  at grid phase 232-400 (+700) / 224-400 (-700) are timed by their peak, the
  rest by their start. The window model fits all 128 with offset ~360 and a
  gap of 129-136 (up) / 137-144 (down) samples. The gap counts from where the
  attack is under way (first sample at 25% of the loudest power over the
  next 512): a ramping drum hit in DL Groover (1% at +0, 25% at +53) is
  timed by its peak in FL, which only that start puts on the right side of
  the grid. All 192 MUL grid bursts and 32 of 40 round-5 bursts (was 31) stay
  as FL times them. 20-case offline score (e3 generic, floored envelope)
  1.73 / 1.99 / -2.98 -> 1.70 / 1.99 / -3.07; in FL (30 pitch cases) env
  1.64 -> 1.64, spectrum 2.01 -> 2.02, null raw -1.44 -> -1.21, fitted -2.86
  -> -2.72: one case (the vocal at +700: -5.4 -> +4.5 dB) carries the loss.
  There FL's reference point (496 samples into the swell) is between the
  old 400-sample window's (388) and the grid's (728): FL measures the
  "loudest" point differently on a slow swell (unknown).
- The low band's gain while an attack is held, by grid phase (MUL): timed by
  its start ~x1.13 flat, timed by its peak 1.13 + 0.0019 per sample of phase
  into the peak range (x1.5 / x2). Mean error on the 64 bursts per ratio
  0.121 -> 0.067 (x1.5), 0.160 -> 0.111 (x2); squeezing varies within the
  attack (x0.667: up then down to x0.7) and keeps x1.1 (0.132 either way).
  Neutral on real material.

## Ratio grid and period estimator (round 9, after 1.2.3)

- **Ratio grid.** FL's e3 stretches by floor(MUL x 512) / 512 and resamples
  the remainder (e2 transient the same, e2 generic 1/256). Found from a
  15-20 kHz level ripple FL makes on noise: its rate is the drift
  |MUL / grid - 1| x 44100 Hz, predicted to 1% at 25 settings (25-28 dB
  peaks; none at grid ratios). Rounding to nearest fails; so does a grid on
  the speed. It is the read interpolator's treble loss following the
  fractional position (round 4's Catmull/Lagrange). In the plugin: the
  stretcher runs at the grid ratio, the FIFO is read at increment x
  grid / wanted. In FL (30 pitch cases): null raw -1.21 -> -3.06, fitted
  -2.72 -> -4.44; envelope 1.64 -> 1.64; spectrum 2.02 -> 1.99. Over whole
  renders (cases/e3_pitch_cases.py) the Rhodes at +-300 / -700 gains 3-4 dB,
  the vocal -300 2.1 dB; the vocal -700 loses 2.9 dB (1.0 -> 3.9); the rest
  moves within 0.4 dB.
- **Period estimator.** Noise plus echoes, FL's splice jumps
  (ratio_grid.py's tracker, refined per splice): upper band only (an echo
  only below 1.4 kHz gives random jumps); the stronger of two echoes wins;
  a 0.1 echo sets 20% of splices, 0.2 78%, 0.35 all (no floor); an echo
  that changes delay mid-file changes the splices from one near the change
  (window around the splice). On noise FL's jumps span 132 / 215 / 283 /
  337 / 397 (10..90th pct): a lean to mid-range lags. Ours had a 0.3 floor
  and used its 256 fallback on all noise (a 20 dB treble flutter at the
  splice rate); now no floor and a mid-range pull (score - 0.04 ((L - mid) /
  half)^2): 139 / 184 / 255 / 336 / 397. Exact choice on noise: best 21% of
  splices (window 512 at the splice + 128, any normalised correlation; the
  envelope, the cepstrum and period-length windows do worse). FL decides the
  jump before it is due (jump size correlates 0.76 with the drift at the
  jump), so its estimate is made earlier; when is not known.
- Soft start re-measured (e3_am): the grains alone now need +60 source
  samples at x0.667 (cleared at an attack): e3 soft-start error 28 -> 9
  samples mean over the four cases.

## Low band after an attack (round 10)

- **Method.** Between restarts a phase vocoder's steady partial keeps one
  phase relation to the source, set where its phases were last restarted:
  a restart from the source frame at P, played where the stretch puts P,
  turns a partial of frequency w by -w (stretch - 1) P against ours. So the
  low band's phase against ours locates FL's restart (several inharmonic
  tones make it unambiguous); the cross-spectrum of FL's low band and ours
  gives it as a phase delay on real material (stretch/lowband.py case).
- **Note start.** Saws: FL's harmonics come out scattered +-10 deg against
  the source (x1.5), set in the first ~2000 output samples, then steady;
  the scatter scales with stretch - 1 (x2: double, x0.667: -2/3); ours has
  none. A lone sine (no treble): FL plays a 1:1 copy for ~1100 output
  samples (x2), then crossfades (level dips to 0.13 where the phases
  oppose) into a vocoder restarted near source 1035 (x1.5, x2) or not at
  all (x0.667, x1.231 at some frequencies); chaotic between 330 and 400 Hz.
  Not modelled.
- **Attacks: the rule.** Five steady tones (211 ... 1129 Hz) switched on by a
  treble click, the onset stepped 16 samples through 512 (lowband.py
  probe): FL restarts from the loudest sample of the band below the split
  in [onset + 660, onset + 1172), 20 earlier where the timing grid times
  attacks by their peak, played where the stretch puts it. The restart
  lands on the tone sum's loudest peaks (5140, 5667; window start boundary
  644..676 after the onset at x0.667, x1.5 and +700 cents; length 495 ...
  1863). A treble click train rising through the window does not move it
  (the loudest point is the low band's). Same structure at x2.
- **A sung "ah" (+700 cents, the stretcher alone):** FL's restart is at its
  sample 1470 (ours: no restart, the attack's own phases at 1373); with it
  the band below 1.3 kHz nulls -14.2 dB (was +4.7). The point does not move
  with level (-12 / +6 dB). Padded with 0..496 samples of silence, FL's
  point flips between the swell's peaks (1474 for pads 0-48, 1366 for
  64-368, 1479 for 384-496): under the rule FL's onset is at about sample
  320 of the swell, ours 603 (our detector needs an 8x block-energy jump).
  No block-energy rule fitted the 32 pads (relative rules fail on the
  leading silence; the best fits sit on the boundary).
- **In the plugin:** at each vocoder attack lock (e3 generic, Auto) the
  restart point is found as above and, from the first unlocked frame
  centred at or after where it plays, the partials are turned by -w (stretch
  - 1)(restart - attack) (the phase advance of each peak, the locked bins
  following). Probe against FL: within 3 samples of FL's restart over most
  of the grid at x0.667 and +700, and 1-3 deg at onsets 208-464 at x1.5 (was
  50-60 deg); at x2 two stretches of the grid miss. In FL (30 pitch
  cases): null raw -3.06 -> -3.30, fitted -4.44 -> -4.57; envelope 1.64 ->
  1.70, spectrum 1.99 -> 2.02. Vocal +700 4.5 -> -5.5 dB, groove -700 / -300
  / +300 by 1-2 dB, the loop by ~1 dB; vocal -300 -0.6 -> 4.6 and +1200
  -3.4 -> 1.2 (our onset on the swell), groove +700 -1.1 -> 0.4.
- **When it plays:** the probe's crossfade is where the attack's 1:1 copy
  reaches the restart point (x1.5: predicted 7288, measured mid-point 7272),
  not where the stretch puts it; but on the loops the stretch-map time
  nulls better (offline, 18 cases: 1.16 against 1.44 dB), so that is used.
- **Skipped restarts:** FL does not always restart. The groove's hit at
  44085 (+700): FL follows ours-before. The loop's two hits repeat every
  41340 samples: the same hit is restarted at onset mod 512 = 66 and 132
  and not at 260 ... 450 (PITCH). The probe with tones decaying (tau 3000,
  x1.5): restarted at 96 ... 352, kept at 0 and 416 ... 480. Steady tones
  are restarted everywhere. The rule (grid range and decay) is not found.

## Swell onsets (round 11, not solved)

Measured at x2 (FL's ratio grid: after a restart at P FL plays the source
with phase -w P, offset 0, calibrated on the click probe to 1.6 deg); the
restart frame's own bias fits best with a 2048-sample frame.

- FL's onset detection is level-free: the vocal at -12 / +6 dB and a swell
  at -20 dB restart where they did at 0 dB. A note that starts in digital
  silence and swells: FL's low band is digital silence until its first
  restart frame fades in.
- Treble alone (a 3-16 kHz noise swell over steady low tones) gives no clean
  restart: the low band restarts on its own onsets.
- Five equal steady tones under swells (-60 dB, +2 / 3 / 4 / 5 / 8 dB per 128,
  then full; t0 stepped 32 through 512; lowband.py swell): FL restarts on the
  tone sum's peaks. Every restart window starts on 512 k + 148 from the
  note's start (the click probe's onset + 660 with the onset on the grid).
  In the middle of the grid cycle (about the range where attacks are timed
  by their peak) FL restarts twice: a first restart, then ~730 samples later
  a second one; elsewhere once. The first restart sits a nearly fixed time
  after t0 per rate (about +3800 / 2750 / 1960 / 1590 / 1050 samples, i.e.
  where the swell is near -40 dB at 5 dB / 128, later in level for slower
  swells).
- Not fitted by: a level crossing (sample or 64 ... 512-sample blocks on the
  note's grid, any threshold and offset: <= 48 / 80 first restarts), a
  look-ahead level ratio, a block energy-jump rule (first or last), FL's
  attack-timing window (round 7) with a per-rate onset (6-12 / 15), frame
  energies of 512 ... 4096 samples (<= 45 / 75).
- The vocal: picking the first low-band sample within 90% amplitude of the
  window's loudest (from our own onset) nulls the in-FL vocal cases well
  (offline 18 cases: +1.17 -> -0.03 dB) but matches FL's restart in only 6 of
  32 paddings (our onset follows our frame grid, FL's a 512 grid from the
  note's start): a coincidence of the test file's position, not used.

## Swell onsets, a per-peak model (round 12, offline, not in the plugin)

docs/BINARY-NOTES.md suggested how FL's e3 decides a restart: per spectral
peak, from a relative spectral flux (against the spectrum a few hops back,
so level-free) and the energy's time centroid inside the frame; a peak whose
energy sits late waits, and restarts once its centroid reaches the frame's
centre; the restart point is the loudest sample of the windowed full-band
frame within a hop of its centre. Tested offline against the round-11
swells (80 cases: 5 rates x 16 onsets, FL's restarts from the cached
renders by `stretch.lowband swell`), with the frame grid, window shape and
centroid threshold fitted:

- 52 / 80 first restarts to the sample (a 4096-sample frame on a 512 grid
  from the note's start, offset ~100, Hann or Blackman window), against
  <= 48 / 80 for every rule of round 11. Where it is right it is right to
  the sample (3978 against FL's 3977, 1571 against 1571), which supports
  the restart point; where it is wrong it is one frame late (+527 / +528) or
  ~200 late, so the decision rule is still missing a detail (the whole-frame
  rules, how FL splits the spectrum into peaks).
- The restart point is the loudest sample within a hop of the frame's
  centre weighted by a much sharper window than the analysis Hann (Hann^3 or
  Blackman^2 fit best; with plain Hann a rising swell's later, louder samples
  win): 67 / 80 first restarts to the sample with the centroid threshold
  refitted (0.3 of half a frame). The rest are a frame early or late.
- The flux threshold hardly matters on these swells (0.5 ... 1.0 the same);
  the centroid threshold does.
- With generic peak regions (peaks above -40 dB of the frame's loudest,
  each with the bins down to the minima beside it) the decision has to be
  weighed by energy (the restarting peaks above ~10% of the low band's
  energy; 0.1 ... 0.5 the same): 66 / 80. Without it any sidelobe peak
  restarts (34 / 80).
- Tried in the plugin (a detector on FL's grid ahead of the vocoder, a phase
  turn from the position the low band's phases follow) and taken out again:
  ours on the same 80 swells (`stretch.lowband swellours`) got 26 / 80 with
  it and 26 / 80 without. On faster swells the detector found FL's restart
  to the sample (+1571 at 5 dB / 128) but our attack rule claims the swell
  first (its restart 20 samples early); on the slowest it fired a frame
  late; and where it did restart, the probe saw no restart in our output,
  so a turn relative to our phases is not FL's restart (FL's resets the
  phases). Next: let the detector time the attack rule's restart on swells,
  and restart with a phase reset. The model is kept outside the repository (it follows
  thresholds read from the binary); a plugin version would fit its own.

## The split at large stretches, Stretch Pro, restarts (round 13)

docs/BINARY-NOTES.md read the split inside the FFT: fixed for moderate
ratios, rising steeply for large ones, capped at Nyquist. Measured with
`stretch.split_rise` (noise, per third-octave band, correlation with the
source: grains ~0.85+, the vocoder a falling trend):

- FL's e3: the split stays at 1.485 kHz up to x2, then the bands that stop
  being pieces of the source start at 2.5 / 3.2 / 4 / 5 / 6.3 / 8 / 12.7 kHz
  at x2.2 / 2.3 / 2.4 / 2.5 / 2.6 / 2.75 / 3; at x4 none are. (stretch -
  1)^3 x 1.485 kHz lands in the same band at every one. In the plugin
  (followSplit): the same rows. Key changes on noise (the output resampled
  back) don't separate the two readings; the engine's own split follows the
  stretch factor, so ours does.
- Real material x2.5 / x3 / x4 (15 new FL renders, beats=24, the stretcher
  alone): envelope 2.295 -> 2.287, spectrum 3.766 -> 3.775. The Rhodes and
  the voice closer (voice x4 2.30 -> 1.58), the kick worse (x4 6.48 ->
  7.60, its quiet tail), the loop mixed. The hold gain of the band below the
  split stays below 1.485 kHz (applied to all of it the kick was worse
  still).
- Other modes, the same probe: FL's Stretch is the vocoder over the whole
  band (x1.5, x2.5). FL's Stretch Pro is e3's structure, grains above 1.5
  kHz and the same rise (x2.5 ~5 kHz, x3 ~12.7, x4 none), and keeps the
  grains with PITCH (+700 cents: 0.91 above 1.6 kHz). Ours with e3's grains
  for Stretch Pro: MUL cases spectrum 2.16 -> 2.26 (with e3's estimator too:
  the Rhodes at x0.667 nulls -13.3 dB, was -2.6, the rest no better); PITCH
  cases far worse (the voice at -700 4.6 -> 6.8): the grains are resampled
  with the rest, so they move the formants Stretch Pro keeps. Not adopted;
  it needs grains that keep the formants. FL's e2 generic rises later
  (about 4 / 6.3 kHz at x2.75 / x3, all vocoder from x3.25); its rows are
  noisier, not modelled.
- The restart point from the full band (not the band below the split): no
  net change (voice x1.5 -5.8 -> -4.6 dB, groove +700 -1.7 -> -3.3).
- A phase reset at the restart (the source's phases, read so the restart
  point plays where the stretch puts it) instead of our turn: the
  steady-tone probe gives the same offsets to a sample, and real material
  the same nulls. For steady partials the two are the same thing; the turn
  stays. (A reset placed a hop late scored better on the drums, which led
  to the next point.)
- No restart after an attack at all: the groove at -700 and x0.667 -3.4 ->
  -5.5 dB, the voice -5.8 -> -0.9. FL seems to restart a held onset later
  and a drum hit not (or at once, which our attack frames already are).
  Two deciders tried: the band's energy after the onset against its first
  512 samples (a threshold that keeps the voice's restart, 2x, also drops
  the restarts of held tones, which FL makes); and the whole band's energy
  centre on FL's frame grid with round 12's threshold, skipping restarts
  that land near the onset: the drums gain (null -5.07 -> -5.36 over the 20
  cases) but the steady-tone probe falls from 15 / 32 restarts to the
  sample to 1 / 32. Not adopted: the decision is per spectral peak in FL,
  and a whole-band centre doesn't make it.

## Swell restarts in the plugin (round 14, after 1.4.1; tried, not adopted)

FL's first low-band restart on round 11's 80 swells lands about where the
swell stops rising (full level at 3840 / 2560 / 1920 / 1536 / 960 samples
after t0 for 2 / 3 / 4 / 5 / 8 dB per 128; FL's restarts are within a few
hundred of those). Ours (`stretch.lowband swellours`): 26 / 80 to within 3
samples, all at 5 and 8 dB / 128 where our attack rule fires; the slow
swells never restarted (no 128-sample block 8x the four before it).

- Detector, own fit (no values from the binary): 4096-sample Hann frames on
  a 512 grid from the note's start (offset 0); a frame whose energy is above
  1.05x the last frame's with its energy centre past 0.35 half frames marks
  a swell under way; the first later frame whose centre is back within 0.35
  restarts at the loudest sample within a hop of its centre, weighted by
  Hann^3. Offline 62 / 80 to the sample; most misses a frame late.
- In the plugin: the restart as a phase reset of the low band (the analysis
  phases, each bin moved by its measured frequency x the distance between
  the frame's centre and the restart point, in the source and where the
  stretch plays it; in the stretcher's stream a frame's sample i sits at
  synthPos + i, not synthPos + i - hop). Applied on the first frame starting
  half a frame after where the restart plays (earlier frames still sit on the
  rise, which skews their phases by up to 8 degrees; on the restart frame
  itself 42 / 80, a hop after 47, half a frame 53); deferred past an attack's
  1:1 frames; an attack inside a swell leaves the restart to it. 53 / 80.
  Peak frequencies for the off-peak bins did not help (41).
- Real material, the stretcher alone (`stretch.real_material e3gen`): null
  -5.07 -> -5.37 dB, envelope 1.74 the same, spectrum 2.00 -> 1.99 (DL
  Groover +2 dB at three settings, e3_loop mixed). In FL (`stretch.e3_in_fl`,
  the reference): envelope 1.70 -> 1.67, but null raw -3.30 -> -3.05, fitted
  -4.57 -> -4.46 (DL Groover at -700 / -300 / +300 cents ~2 dB worse, where
  the stretcher alone had it better), and the engine test "a soft start lines
  up" failed. Not adopted; the change is kept as a patch outside the
  repository. Next: why the groove goes opposite ways in FL and offline (the
  in-FL case is the whole plugin, the offline one the stretcher fed FL's
  decode), and the remaining 27: our attack rule's 1:1 lock is itself a
  restart (its frames take the source's phases) and claims 4 dB / 128 swells
  ~200 samples before FL.

## Open

- The period estimator's exact choice on noise (when it is made; 21% now).
- Grain window shape, how analysis marks are placed.
- When FL skips the low band's restart (decaying attacks, by grid range),
  and the note start's (the sine's restart near 1035, the saws' scatter).
- FL's onset on a slow swell (the "ah": ~320 against our 603), which sets
  both its restart and (round 8) its timing; and its second restart in the
  peak-timed part of the grid (round 11). Round 12's per-peak model gets
  67 / 80 swells.
- The transient gain rule.
