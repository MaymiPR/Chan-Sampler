# Chan Sampler: measurements

What Chan Sampler's behaviour is matched to, how each law was found, and how closely it matches FL Studio's Sampler.
Every law below is a formula or table fitted to FL's *output*; none of FL's code, artwork, files or impulse responses
is copied, and the editor is drawn by our own code from measured sizes and colours. What the original is and what is
not matched are in [RESEARCH.md](RESEARCH.md), the null-test results in [NULLTESTS.md](NULLTESTS.md), the e3 /
elastique measurements in [e3 generic in detail](#e3-generic-in-detail); how to run the tests is in
[nulltest/README.md](nulltest/README.md).

## Setup

| Item | Value |
|---|---|
| Reference | FL Studio 26.1.7, its Sampler channel |
| Sources | **[M]** the manual, **[PYFLP]** the project-format description and **[NULL]** the runtime measurements and null tests in `nulltest/` ([RESEARCH.md](RESEARCH.md#sources)) |
| In-FL pairs (the reference) | FL's Sampler on channel 1 and the Chan Sampler VST3 on channel 2 of one project, rendered once from FL's command line, subtracted as heard in FL with no gain correction (`nulltest/cs_ref.py`) |
| Offline cases | FL's Sampler alone from FL's command line against `CsRender` (`nulltest/nulltest.py` aligns, gain-matches and subtracts); projects built by `nulltest/project_lib.py`, rendered by `nulltest/ref_solo_lib.py` |
| Render queue | every FL render through `nulltest/queue_lib.py` (keyed cache, retries, resume) |
| Export | FL's command-line export is 16-bit: the best residual is about −65 to −95 dB, depending on signal level |
| Velocity | FL velocity 100 (unity; FL passes it to a plug-in unchanged as MIDI velocity 100) |
| Tempo and gap | 130 BPM unless a case sets `bpm`; a tick is 11025 / 52 samples at 130 BPM, so multi-note cases use a 26-beat gap between the halves (it also lines up at 65, 100, 120 and 160 BPM) |
| Rates | the project rate 44.1 kHz; host rates 44.1 / 48 / 96 kHz in the robustness check |
| Scale | **NULL** better than −60 dB (a match); **close** −30 to −60 dB; **diff** worse than −30 dB |

## Measured laws

| Feature | FL behaviour (measured) | Evidence |
|---|---|---|
| Velocity | gain = (11^(v/100) − 1)/10, FL velocity v 0 to 128 (100 = unity, 128 = ×2.05). MIDI m → v = m·128/127 | NULL |
| Levels VOL / PAN | VOL = FL's channel volume: gain (11^x − 1)/10, x = raw/12800 (knob position raw/25600), default raw 10000 = 78% = 0.551. PAN raw 0..12800 (centre 6400), constant power with unity centre: L = √2·cos θ, R = √2·sin θ, θ = (pan+1)·π/4 (50% left = L ×1.307, R ×0.541). FL also applies its own channel levels to plugin channels, so in-FL tests leave Chan Sampler's FL channel at default | NULL −74…−84 dB (7 cases, in FL) |
| End of sample | Holds the last value and fades it out with a cosine over the declick out-time (10 ms for Out only) | NULL −75 dB |
| Boost | gain = 1 + 16·knob (raw/256), up to ×17; CLIP hard-clips at ±1 | NULL −76…−95 dB |
| EQ | y[n] = x[n] + 0.75·eq·y[n−1] (one-pole; right = more lows) | NULL −66…−69 dB |
| Fade IN / OUT | Linear slopes relative to sample length: IN = clamp(1 − (1−x)·4k), OUT = clamp(1 − x·4k) | NULL −67…−75 dB |
| Sample start / Length | start = round(start·(N−1)); length = round(fraction · (N − 1 − start·(N−1))), half to even, one frame more with Reverse, 100% the whole rest (FL's end found in 85 forward and 15 reversed in-FL trims of noise by fitting the end filter's position: all exact); a segment shorter than 41 frames is lengthened by 41 (LENGTH 0 plays 40, a 30-frame trim about 70, 40 about 80, 41 about 40), growing backwards within that much of the sample's end, and the end filter runs whenever LENGTH < 100%, even at the sample's end (nulltest/cases_trim_short.py, 16 in-FL cases: FL's lengths matched to a frame). Segments shorter than the 200-frame table use the same filter as a model, a swept one-pole high-pass whose window is the whole segment as FL keeps it, its last frame included, its sweep rate per frame 9.87 / (window − 1.3) (fitted to FL's click matrices of segments of 41, 50, 60, 80, 100, 150 and 190 frames, nulltest/measure_trim_matrix.py: −67…−72 dB up to 150 frames, −54 at 190): −59…−69 dB in FL (16 cases; LENGTH 0 keeps its own table and sits at FL's 16-bit floor, a few one-step differences). The parameter is a float, so the fraction is taken as the shortest decimal that reads back as it, the double FL holds (0.05f → 0.05). Trailing digital silence inside a trimmed segment is left out of the end filter: FL fades where the sound stops (a trim landing in a gap of loud.wav: −77 dB fitted, −62 dB in the plugin, against −49 dB with the filter at the cut). **Reverse applies to the trimmed segment.** The trimmed start and end are linear filters, not gain fades (measured with clicks, superposition verified; nulltest/measure_tail.py, nulltest/measure_head.py, nulltest/measure_tail_short.py → Source/dsp/TrimTail.h): at the end each output keeps its own frame and subtracts a weighted average of the frames behind it, up to twice its distance from the end (low frequencies fade first, DC reaches 0 at the end); at the start each output mixes the input from n to about 3n over 22 frames. The shortest segment (LENGTH 0 = 40 frames) has its own measured table and carries two silent frames after it. Order: start filter, end filter, then Reverse. A segment of pure digital silence around the cut renders silent in FL. START exactly 0.5 (a half-sample tie): the play position rounds the tie up but the length count rounds it down, so the segment runs one frame past the sample end (silence; the end then holds that silence): modelled, NULL −69 dB with and without Reverse. Fades are applied before Reverse (measured) | Full length NULL −66…−90 dB at any start, with or without Reverse; trimmed: median −75 dB over 137 in-FL cases (122 NULL, 15 close, worst −52.5 dB: LENGTH 0 with a start, and cuts in real material); reversed, the segment keeps FL's last frame and the table ends a frame before it (reversed trims −65…−85 dB) |
| Crossfade | Knob position = raw/1024; loop start = position/2 of the sample (FL truncates). Without loop points the sample loops from there to the end; the last stretch of that length blends linearly into the audio before the loop start | NULL −82…−84 dB (5 cases, in FL) |
| Output level | FL's plugin wrapper outputs twice what a default Sampler channel does, so a default Chan Sampler must output 0.5. OUT is FL's channel volume knob (measured in FL: gain (11^x − 1)/10 of its position, 78 % = raw 10000 of 12800 by default showing "78%  -5.2dB  0.55", 100 % = 0 dB at its top); its gain is scaled by 0.5 / 0.551 so the default puts out 0.5 (OUT's top is 0.907, +5.2 dB over the default). Projects saved before state version 2 kept OUT as a trim in dB; they load with the same level, capped at the knob's top | NULL −78.8 dB (defaults, in FL) |
| Arpeggiator chords | Auto, Auto (sustain), (none) plus 66 chord presets (standard chord names and intervals, in the order of FL's chord menu so a preset's choice carries over); presets build on the lowest held key | FL's chord menu |
| Normalize, Reverse, Polarity, Swap, Fade stereo (cos/sin), Remove DC (mean) | Exactly as named | NULL −73…−82 dB |
| Trim | Cuts after the last frame whose louder channel exceeds raw/32768; at 0 nothing is cut (trailing digital silence is kept) | NULL −76 dB |
| Ring mod | ×((1−mix) + mix·m), m advances (raw+8)/5760 cycles/sample; **sin on L, cos on R**; a mono file stays mono, so sin on both | NULL −61…−74 dB (stereo, mono, 48 kHz) |
| Precomputed cut/res | y = y1 + f(x − y1) + r(y1 − y2), f = (601^u − 1)/600; r measured at raw resonance 0, 32 … 640 (cutoff open: y = x + r(y1 − y2)), cubic in log(1 − r) | NULL −70…−100 dB (all settings tested) |
| Stereo delay | Delays one channel by \|raw − 2048\| samples (≤ 2048); right = delays left; the sample grows | NULL −76 dB |
| Pogo | Length-preserving time warp: (1−\|a\|)x + \|a\|·w(x), w = sin(πx/2) or 1 − (2/π)asin(1−x); 4-point Hermite with silence outside the sample | NULL −75 dB (full range) |
| Playback interpolation | 30-tap windowed sinc, k(d) = c·sinc(c·d)·w(d), \|d\| < 15 source frames, c = min(1, 1/ratio); the window stays 15 frames wide; w = 7-term cosine series (nulltest/measure_kernel.py, nulltest/fit_window.py) | < 1 LSB vs 686k measured points; pitched notes NULL |
| Sample-rate conversion | Files at another rate are converted to the project rate on load with an 88-tap windowed sinc (\|d\| < 44, near-Hann 9-term window), then played through the 30-tap kernel | 1 LSB; 48 kHz mono sweep NULL |
| Envelope graph | Stage width = knob position × ⅓ of the view (decay × (1 − sustain)); no sustain plateau; release drawn from the sustain level at full width; delay is empty space; curves bend with each tension | matches FL screenshots (9 settings) |
| Arpeggiator | Auto (sustain) is FL's default chord. Step k starts at round(k·step) after the first. TIME position = (raw−450)/997, raw 1447 = Hold; FL plays whole ticks: an exponential of the raw value rounded to half ticks and halved downwards (two values fitted to 55 measured raws, measurements/ChanSampler/arp_ticks_dense.txt and measure_arp_period.py: all exact). GATE ends a whole number of ticks after the step, GATE × the step's ticks rounded down, at least one (cases a_t*: 7 ticks × 50% = 3). Hold plays only the first step (the sequence's first note) and ends it when every key is up; keys pressed later, GATE and the sample running out change nothing (nulltest/probe_arp_hold.py; 4 cases NULL −74…−100 dB). Arp notes end at the gate end or key release (cut with the declick fade when the volume envelope is off), unlike normal notes. Auto (sustain) holds a single key and restarts it on the next step once the sample has run out. Slide = legato: the sounding note takes the next pitch at the step, no glide; once it runs out later steps stay silent. FL sends All-notes-off right before the note-off | NULL −61…−83 dB (27 cases); short steps (3 to 7 ticks, 6 cases) −45…−48 dB (half-sample ties) |
| Stretch modes | Pitch and length independent in every stretch mode (a key change keeps the length; MUL / TIME change the length, not the pitch). How each mode behaves and how close it comes: [Time stretching](#time-stretching-against-fls-renders) | cannot null: FL's engine (elastique) is proprietary |
| Envelope on the tick grid | FL works the envelope out once per song tick (96 per beat) from the note start and interpolates linearly; every FL render is piecewise linear on that grid to the 16-bit floor (nulltest/probe_env_tick.py). Each stage lasts N = ceil(time / tick + ½) ticks (none for no time), counted from the note start for delay and attack; the attack then lasts as many ticks as FL's single-precision steps of 1 / N take to reach 1 (N, or N + 1 when the float sum falls short: N = 12, 23, 28, 49, 58, 75, 107, 120, 132 among those measured; the extra tick is at full level, so it moves the hold and decay a tick later; all 26 attack lengths measured, double precision gets 13 wrong); the hold's ticks are its own, and its last tick is the decay's first (with no decay time, the last tick before the fall). From tick to tick the level moves at most the full range per 8.83 ms (389.5 samples): that is what smooths a jump and limits very short attacks (at 130 BPM it reads as 424-sample ramps and a 19.23 ms attack floor). The first tick holds the second's level when there is no delay or attack. A note-off starts the release on the next tick (FL's notes end on ticks; within a sample of one counts as on it); once the release is silent FL ends the voice with the declick fade. Default release tension (−101) exponent 4.62 | NULL −63…−99 dB at 65, 100, 130 and 160 BPM, Tempo off and on (nulltest/cases_core.py, nulltest/cases_tempo_env.py: 41 of 44 cases; the other three −45…−54 dB, see [Known gaps](RESEARCH.md#known-gaps-diff--approximate)) |
| MUL (Resample) | 2^(raw/10000); tape-style speed 1/MUL | NULL −68…−80 dB (×¼ … ×4) |
| LFO | Tempo-synced: FL evaluates the LFO once per song tick (96/beat, from the note start) and interpolates linearly; the first tick holds tick 1's value. Period ticks = max(3, round(13.5755·982.5^p − 13.2745)), p = speed position (42 speeds measured); predelay ticks = same law on the predelay raw, no minimum (11 values). Attack fade in seconds like the envelope times. Shapes start at 0 going up; triangle in phase with the sine; pulse edges cross over 4 ticks (0.27205 per tick for 3 ticks, then the rest). Volume: gain × (1 + lfo); pan: + 2 × lfo (clipped); pitch envelope and LFO in cents (raw amount = cents, ±128) and half a tick early (−2.5 samples); Mod X / Y: + lfo (full scale). Note: with the volume envelope off a note plays to the end of the sample (note-off ignored), so test samples must be shorter than the gap | NULL −67…−96 dB (17 volume/pan/pitch cases), pitch envelope −60 dB |
| Stretch fields | PITCH in cents, MUL 2^(raw/10000), TIME in ticks at 192 per beat ("1 bar" = 768). Resample + TIME plays the whole sample in TIME at the host tempo, tape-style (in-FL NULL −62…−77 dB; TIME values that are not powers of two show a ~3e-8 rate difference, −57 dB). Auto + TIME / PITCH / MUL is bit-identical to FL's e3 generic (−300 dB), i.e. elastique. Past the sample end FL keeps interpolating at the advancing position with clamped neighbours while the declick fades (noise case −72 dB). TIME menu: (none) / Autodetect… / Project tempo (sample length at the host tempo) / 1 beat … 4 bars, long menu without Type in value; hint shows bars:steps:ticks at 24 ticks per step. Turning PITCH in Resample switches FL to Auto, which shows the method it picked (e3 generic) | FL GUI readout |
| Start offset | Fraction of the sample | NULL −74 dB |
| Declick in-fade | Only when starting past frame 0: none / 4 ms / 4 ms / 20 ms / 100 ms / Crossfade's own curve (8750 samples at 44.1 kHz, a degree-10 polynomial fit, max error 0.6 LSB of 3192). Transient modes remove only the start step: x − x0·(1 − S) | NULL −77…−82 dB (nulltest/cases_declick.py) |
| Declick out / cut | Cosine 10 ms (Out only) / 20 / 20 / 100 ms; Crossfade's own curve (8781 samples, polynomial fit). Transient (no bleeding) is not a gain fade: a one-pole high-pass (input minus a one-pole low-pass starting at 0) whose coefficient sweeps up, 1 − exp(−ΔL) with L(n) = 0.002015·(e^(0.0228 n) − 1), so a held value decays smoothly while noise rings on, then a closing gain table silences it by 440 samples (separated with DC, noise and sine renders). A cut voice keeps playing while fading | NULL −64…−98 dB (DC, noise, sine; all six modes) |
| Cut itself | New note fades earlier voices with the declick out-fade | NULL −80 dB |
| Envelope times | Knob position = (raw − 100)/65436; the minimum is zero time. FL's own times, without the tick rounding: attacks measured at six tempos, each giving the time to a tick, intersected (nulltest/probe_env_time.py, env_times.txt; 28 points from raw 1000, typically ±0.3 ms). Close to t = 0.0646 e^(6.90 p) − 0.0637 s; the table interpolates log (t + 0.0637) between points | NULL −66…−99 dB (11 cases at 130 BPM, in FL) |
| Envelope shape | Linear at tension 0; tension = (e^{kx}−1)/(e^k−1) with k fitted per tension; decay/release = attack curve reversed. The decay runs the whole curve from 1 to 0 over its time and stops at the sustain level (positive decay tension: −55…−74 dB) | NULL |
| Decay / release | Decay is rate-based (time = full 1→0 travel). Release joins its curve where the curve equals the current level (from sustain 0.39 at tension −101 it takes 79% of the time) | NULL −65…−78 dB |
| Sustain | Linear (raw/128) | measured |
| Precomputed reverb | A type (A or B) and a mix (0 to 256). Identified from impulse responses (nulltest/fit_reverb_*.py): Schroeder reverb per channel. Parallel combs with feedback g = 10^(−3D/T60), T60 = mix/256 s, then allpasses (g 0.7), then a smoothing filter; dry 0.7. **A:** combs 1777/1847/1993/2137 × 0.15, allpasses 43/127/389, output w/2 + w[−1] + w[−2]/2. **B:** combs 2467/2767/3217/3533/3877/4127 × 0.027, allpasses 67/97 (R: 79)/101/197/599, output one-pole y = w + 0.7y[−1]. The tail is then trimmed after the last sample ≥ 65/32768. Default type B | Model matches FL's IR to 0.29 LSB; NULL −60…−95 dB (14 cases, in FL; the impulse residual is 1 LSB) |
| Knob mapping | Every knob's 0..1 position equals FL's "Copy value" text, collected per knob in FL's GUI (e.g. PITCH ±1200 cents, MUL 2^(4p−2), TIME beats/192, LFO speed raw = 200 + p·65336, slide raw/1660 (glide ticks below), arp time table, echo time p·8 steps, echo pan ±2). Right-click menu: Reset / Copy value / Paste value (xx%) / Type in value… | unit tests + in-FL nulls |
| Filter: Fast LP | Same recursion as the precomputed filter (Mod X/256 = u, Mod Y/256 = res). Below u = 1/64 the coefficient falls linearly to 0, so Mod X 0 is silence (the precomputed filter keeps its 1/64 floor) | NULL/close; cutoff 0 and 2/256 in FL: silent / 16-bit floor |
| Filter: LP/BP/HP/BS | RBJ biquads (direct form I); f0 and Q from tables every 8 raw, traced tick by tick with Mod X / Mod Y LFOs (agree with the static points). Above raw 240 f0 = 16412·e^(3.99(u − 0.9375)), and modulation can push it past the knob's end up to FL's cap of 3.0 rad/sample (21056 Hz at 44.1 kHz, reached at Mod X 100%) | NULL −65…−77 dB static |
| Filter: SVF LP | Chamberlin SVF, f = (26^u − 1)/25, q = 1 − 0.8·res | NULL −65 dB |

## Tempo / Global switches, LFO graph, Levels VOL (measured in FL)

- Each target has its own envelope Tempo, LFO Tempo and LFO Global switches. Hints: "Tempo-based time", "Global / retriggered LFO phase".
- LFO period / predelay: the knob gives a length T in ticks at 130 BPM (13.5755 \* 982.5^p - 13.2745); FL rounds T \*
  ref / 130 to whole ticks with ref = the project tempo (Tempo off: the same length in seconds at any tempo, at tick
  resolution) or 120 (Tempo on: a fixed musical length). At 130 BPM the two readings coincide. Matches FL at 65 / 100
  / 130 BPM (-62...-77 dB, nulltest/probe_tempo.py).
- Envelope Tempo: times are for 120 BPM (x 120 / tempo), then rounded to the song's ticks like any other. With Tempo off, the tick rounding of stage lengths makes a stage about 1 % slower at 65 BPM.
- Global: the LFO phase runs from the song start (host position); it is at full value from the note's first sample. -77 dB.
- LFO graph: drawn in knob units, not time: on a 130 px view one period is 8 + 64 x SPEED, the predelay 64 x DELAY, the linear fade 64 x ATT; height = AMT (negative starts downwards).
- FUNC Levels VOL is FL's "Volume multiplier" (raw / 12800, default 100% = unity, gain (11^x - 1)/10, hint "100%
  0.0dB  1.00"); FL's channel volume (78%) is applied by FL to plugin channels too, so the plugin's output trim
  supplies the fixed 0.5 (-6.02 dB).

## Control timing

Envelopes / LFOs are updated in 32-sample control blocks that start at each note's own start (not the host buffer),
and an event that changes a voice refreshes its targets on that sample without moving the envelopes on (hold 5000:
−68 dB; offline renders match in-FL ones). Arp TIME 640 stays at −46 dB: 4 of ~100 steps fall on exact half-sample
ties that FL rounds both ways (its internal arithmetic is not visible).

## Echo delay (in FL)

The echo's settings: feedback (raw / 25600), pan (centre 6400; ours (raw − 6400)/6400), pitch (cents), echoes, time
(raw / 48 steps). NULL −95…−108 dB for feedback, count, time and pan (nulltest/cases_echo.py); echo pitch close
(−57/−59 dB: only the first ~6 frames of each pitched echo differ).

- **Mod X / Y:** knob = (raw − 128)/128 (checked by sound). Echo k moves the filter value by 1 − (1 − |m|)^k towards
  the knob's side and clamps to 0..1: Mod X −50% gives cutoffs 0.5, 0.25, 0.125… from open; +25% from 0.25 gives 0.5,
  0.69, 0.83. NULL −71…−86 dB (7 cases).
- **Fat mode:** echoes keep the normal timing, are all the same level 2G/(3·count + 2) (G = feed gain 2·feed) and the
  original note becomes 1.5 − count·that (the levels always sum to 1.5). Pan lowers an echo on both channels by
  cos(π/4 · min(1, k·|pan|)); Ping pong has no effect. NULL −97…−107 dB (5 cases).
- **Ping pong:** NULL −102 dB. The in-FL test projects end with a placeholder note so FL renders the whole second half (it stops a fixed tail after the last note, which would cut long echo trails).

## Filter modulation (in FL, nulltest/cases_lfo.py)

Read from FL's output by fitting the filter coefficients in short windows against the known noise input. With an
envelope or LFO on Mod X / Y, FL works the values out once per tick of the note (the first tick holds the second knot,
as for the LFOs) and then:

- **Fast LP:** the cutoff coefficient f moves linearly between the tick values (in coefficient terms, not knob terms: the mid-tick bump is the proof); the resonance term holds the end-of-tick value.
- **LP / BP / HP / BS and SVF:** all coefficients hold the end-of-tick value for the whole tick, switching on the sample nearest the tick.
- Biquads behave as direct form I when the coefficients change.

NULL −59…−72 dB for LFO / envelope on Mod X and Mod Y with Fast LP, LP, HP and SVF.

## Tracking, root note, polyphony (in FL, nulltest/cases_untested.py)

- **Tracking** (velocity and key, each with mid, pan, mod X and mod Y; knob = raw/256): velocity offset =
  (raw/128)·(f(vel) − M(mid))/M(128), with f FL's velocity-to-gain curve (11^(v/100) − 1)/10 and M(m) = (6.5^(m/100) −
  1)/5.5 for the middle value 0..128. Key offset = (raw/128)·(key − mid)/24 (50% = 0.5 per octave). Pan, Mod X and Mod
  Y alike. NULL −62…−76 dB (15 cases); velocity to Mod X −55 dB (2 cases).
- **Root note:** stored apart from the other settings, C5 by default; fine tune = Levels pitch (cents). NULL −77…−80 dB.
- **Mono / porta glide:** the pitch moves in N equal steps on the song's tick grid (each on the sample nearest its
  tick, the first at the event's tick); FL adds the steps to a float32 in cents from note·100, so long glides end a
  little flat (−0.00012 semitones after 83 steps; reproduced). N = round(0.92049·209.414^(raw/1660) − 0.15), 17 slide
  values measured (raw 0 → 1, 500 → 4, 820 → 13, 1000 → 23, 1660 → 193); FL's Slide readout shows N − 1 ticks. Mono
  with or without Porta is identical. Mono keeps a key stack: releasing the sounding key glides back to the previous
  key and keeps the note going; with none left the note is released (with the volume envelope off the sample plays
  on). Releasing another key removes it from the stack only with the volume envelope on. Porta (poly) glides each new
  voice from the last note. NULL −70…−82 dB (21 cases).
- **Max polyphony:** loaded by FL (shown in its GUI) but no effect in its renders (1 and 2 voices, different and same
  keys, envelope on and off): every voice plays. Chan Sampler shows the knob and ignores it too.
- **Overlapping notes on one key:** a note-off ends the oldest voice only. NULL −76 dB.
- **Test harness:** a tick at 130 BPM is 11025/52 samples, so the two halves of an in-FL render share sub-sample note
  and tick positions only when the gap is a multiple of 13 beats; multi-note cases use 26 (a 16-beat gap moves later
  notes by a sample, which ruins a noise null).

## Loop points (in FL)

The END frame is part of a forward loop; with loop points the crossfade length is crossfade/4 × the sample and moves
the loop start that far in. Forward, short, ping-pong, crossfade and off: NULL −79…−83 dB.

## Content: regions and slice markers (in FL)

FL's defaults: Resample, Load regions and Use loop points on. Rendered in FL's Slice map with test tones carrying
markers (nulltest: cue_tone.wav, strc_tone.wav, strc_jump.wav): **Load regions** slices at the WAV cue points, **Load
slice markers** at the ACID "strc" chunk (28-byte header with the count, 24-byte entries, position in the second field;
the first entry is a header entry). Loaded markers replace the detected transients (a hard level step at 0.5 s is a
slice start without markers and falls inside the 0.3 s slice with them). Chan Sampler carries the markers through rate
conversion, Sample Start and Reverse; its slice boundaries land within 2 samples of FL's (12211 / 28493 vs 12211 /
28491; level step 21032 vs 21029). Keep on disk and Resample only change how FL holds the file and are shown greyed.

## Time stretching (against FL's renders)

FL keeps pitch and length independent in every stretch mode: a key change keeps the length, MUL / TIME change the
length and not the pitch (FL renders). FL's engine (elastique) is proprietary, so these modes cannot null; they are
compared by behaviour, attack timing and envelope / band-spectrum distance to FL's renders (`nulltest/compare_stretch*.py`,
[e3 generic in detail](#e3-generic-in-detail)). Evidence: nulltest/check_stretch.py,
nulltest/compare_stretch.py, nulltest/compare_stretch_material.py, e3_clicks.wav.

### Our modes

Source/dsp/TimeStretch.cpp, own code: pitch-synchronous grains for e3 / e2 mono and e2 speech; e3's vocoder (and e3's
high-band grains for e2 generic) for Stretch / e2 generic / e2 transient; e3 generic restarted at each slice for slice
stretch; Stretch Pro = Stretch's vocoder (2048 points, e3's attack handling) with cepstral formant preservation
(64-sample lifter, fixed in time so the frame size keeps the same envelope) and FORMANT shift (+/-1200 cents, centre =
preserve). The stretch range is 1/4096 ... x4096.

### FL's modes, measured

FL renders of clicks, noise and saws at MUL x1.5 / x0.667, and of a kick, a Rhodes, a voice and two drum loops at -700
/ +700 cents and x1.5 / x0.667:

- FL's e3 mono and e2 mono are time-domain splicers (noise: the output is pieces of the source, a splice every ~900 /
  ~480 samples at x1.5, each jumping one estimated period, ~250 / ~140 samples on noise), clicks land on time (+/-20
  samples) at full level, and PITCH keeps the spectral envelope (a voice's 99% roll-off stays at 12.7 kHz at -700 and
  +700 cents; e3 / e2 generic, e2 transient and Stretch move it with the pitch; e3 mono and e2 mono null at -13...-20
  dB against each other on a Rhodes); e3 mono plays a note's first ~384 samples 1:1, e2 mono splices at once.
- e2 speech is time-domain with long splices (~1150 samples at x1.5).
- e2 generic's and Stretch Pro's high band is pieces of the source (noise correlation 0.8 at x1.5), Stretch's and e2
  transient's spectral (0.2). FL's e2 generic / transient smear a click over ~-300 samples and run at a quantised
  ratio (x1.5 plays as 1024 / 683).
- FL's Stretch and Stretch Pro differ only with PITCH (MUL alone: their envelopes 0.54 dB apart over the 20 cases).

### Distance to FL

Envelope / band-spectrum distance in dB over the 20 cases above (`nulltest/compare_stretch_material.py`); log-spectrogram
distance to FL's renders 7-10 dB. The earlier column is the model each mode replaced.

| Mode | Current | Earlier model |
|---|---|---|
| e3 mono | 2.16 / 3.83 | 3.34 / 6.38 |
| e2 mono | 2.06 / 5.10 | 2.76 / 7.28 |
| e2 speech | 1.89 / 2.45 | 2.33 / 3.25, then 1.88 / 2.70 |
| e2 transient | 2.45 / 3.07 | 4.95 / 3.83 |
| Stretch | 1.77 / 2.04 | 2.12 / 2.82 |
| e2 generic | 2.55 / 2.85 | 2.57 / 2.87 |
| e3 generic | 1.73 / 1.99 | |
| Stretch Pro | 1.94 / 3.77 | 2.93 / 4.37 (4096 points); 2.92 / 4.34 |

Stretch Pro in detail: FL's Stretch Pro matched our Stretch (1.84 / 2.17) better than a 4096-point Stretch Pro (2.79 /
2.88), so Stretch Pro runs Stretch's vocoder plus the formant envelope; its soft start is fitted to FL's (-76 / -56 /
-44 / -84 samples, ours within 4). A drum loop at MUL x0.5 / x2 / x4: 2.66 / 2.76, 3.01 / 2.50, 3.35 / 3.36 (4.12 /
4.55, 3.94 / 4.32, 7.44 / 11.01 with the 4096-point model); MUL-only cases 1.84 / 2.17 (2.79 / 2.88), PITCH-only 2.04
/ 5.37 (3.05 / 5.80).

### e3 generic

- **Pitch** (nulltest/cases_e3_pitch.py: FL's 707 Kick, a Rhodes, a voice, two drum loops, -1200 ... +1200
  cents, in FL): FL's e3 on noise plays resampled stretches of the source with constant offsets for 400-700 samples,
  then jumps (a time-domain-like engine), and keeps a kick's envelope at any pitch. Ours: the vocoder's synthesis hop
  shrinks below +1 octave (0.5 x the analysis rate) and an attack is held 1:1 only up to the frame centred on it, so a
  kick an octave down does not last twice as long: mean envelope distance 2.01 dB (2.64 before), band-spectrum
  distance 2.44 dB (2.61), raw null -0.5 dB (+0.0) (sample-level nulls stay near 0 dB past the first attack).
- **Attacks** (1 kHz clicks at -1200 / -600 / +600 / +1200 cents): FL sounds every attack at the same place, -6 / -1 /
  +4 / +5 samples from the source's; ours the same within a sample (attack found to the sample, the 1:1 frames placed
  with a per-pitch shift table); each click nulls at −22…−43 dB after a level fit; a note's start is 1:1 from its
  first sample (in-FL +600 cents: -37 dB over the first 256 samples). Sustained tonal material stays uncorrelated (≈0
  dB).
- **Far stretches:** FL fills the TIME at any stretch (the 707 Kick at TIME 16 beats, x25: 7.38 s in FL and ours). At
  such stretches FL's e3 and Stretch start a kick about 10 dB quieter than ours for the first 0.15 s.
- **Large MUL** (1 kHz clicks every 0.5 s, our stretcher alone): an attack seen too early waits for a later frame:
  within 3 ms from x0.25 to x6 (Stretch Pro x6: -12 ms). FL's drum loop at x4 lands within 15 ms of on time in all
  three modes.
- **Loops in e3 / e2** (FL renders of a Rhodes with a 253-frame loop): once in the loop FL's output repeats exactly
  (residual -99 to -120 dB) every loop x MUL samples (127 / 506 / 1012 at x0.5 / x2 / x4) in e3 generic, e3 mono, e2
  generic and e2 speech: FL loops its precomputed stretched copy. Chan Sampler stretches the loop's first pass
  straight through, records it (per voice, up to 32768 frames; longer loops fold into the stretcher) and repeats it:
  band-spectrum distance 3.75 / 2.52 / 3.05 dB (e3 generic, x2 / x0.5 / x4; 13.9 / 10.4 / 12.8 without the recorded
  pass); e2 generic, e3 mono and e2 speech 1.5-6.1. Resample, Stretch and Stretch Pro loop in real time in FL too
  (period = the loop, 253).
- **Stretch-ratio grid:** FL stretches by the ratio rounded down to a grid and resamples the rest: e3 generic (and
  Auto) and e2 transient 1/512, e2 generic 1/256 (the e3 / e2 mono and e2 speech grains do not drift; Stretch /
  Stretch Pro unclear). Its read interpolator dulls the treble with the fractional position, so the 15-20 kHz level
  ripples at exactly the drift, |MUL / grid ratio - 1| x 44100 Hz: predicted to within 1 % over 25 MUL settings (e3; 7
  for e2 generic), none where MUL is on the grid (x1.5, x2, x0.75, x0.5). Chan Sampler does the same. Against FL: e3
  generic's 30 in-FL pitch cases null -3.06 dB raw (-1.21 without the grid), -4.44 dB fitted (-2.72) (the Rhodes -11
  ... -17.6 dB at every pitch), spectrum 1.99 (2.02); real material (stretcher alone) e2 transient -2.31 dB (-1.16)
  and e2 generic -2.45 dB (-1.00) fitted. [e3: ratio grid](#e3-ratio-grid).
- **Grain period:** measured from the upper band only (an echo only below the split leaves the splices random), the
  strongest correlation wins (of two echoes the stronger, at equal strength either), with no floor (a 0.2 echo still
  sets most splices) and a lean to the middle of the 40..512 range: FL's splices on noise span 132 / 215 / 283 / 337 /
  397 samples (10th..90th percentile). Ours: the treble's modulation spectrum flutters 3-4 dB where FL has 3 dB (20 dB
  with a 0.3 floor and a fixed 256 fallback on noise and weak periodicity: splices every 256 samples). FL's exact
  choice on noise is not reproduced (21% of splices). nulltest/measure_ratio_grid.py, [e3: period
  estimator](#e3-period-estimator).
- **The band below the split after an attack:** FL's low band is a phase vocoder whose steady partials keep one phase
  relation to the source until the next restart, so the phase of FL's low band against ours locates where FL restarts
  it. Steady tones switched on by a treble click (onset stepped 16 samples through 512; MUL x0.667, x1.5, x2 and +700
  cents): FL restarts the phases from the low band's loudest sample 660 ... 1172 samples after the attack's start (20
  earlier where the timing grid times attacks by their peak), played where the stretch puts it; the tones name that
  sample to within a sample, the same at all four settings. Ours turns its partials as FL's restart would: our offline
  build against FL's renders of these tones is within 3 samples of FL's restart over most of the grid at x0.667 and
  +700 cents, 1-3 degrees in the middle of it at x1.5 (50-60 keeping the attack's own phases). A sung "ah" stretched
  alone nulls 14 dB below the split once its restart matches (+4.7 dB without). In FL (30 pitch cases): null -3.30 dB
  raw (-3.06 without), -4.57 fitted (-4.44); envelope 1.70 (1.64), spectrum 2.02 (1.99) (the drum loop, where FL skips
  some restarts). Not modelled: FL skips the restart on some decaying attacks by their place in the grid, and finds a
  swell's start earlier than ours (the "ah": about sample 320 against our 603), which costs the vocal at -300 and
  +1200 cents. nulltest/measure_e3_lowband.py, [e3: low band after an
  attack](#e3-low-band-after-an-attack).
- **The split at large stretches:** FL's e3 keeps the split at 1.485 kHz up to x2 of MUL / TIME, then raises it: on
  noise the bands that stop being pieces of the source start at 2.5 / 3.2 / 4 / 5 / 6.3 / 8 / 12.7 kHz at x2.2 / 2.3 /
  2.4 / 2.5 / 2.6 / 2.75 / 3, and at x4 none are (nulltest/measure_split_rise.py). Ours follows (stretch - 1)^3 x
  1.485 kHz and gives the same rows. Real material at x2.5 / x3 / x4 (15 FL renders, the stretcher alone): envelope
  2.29 (2.30 with a fixed split), spectrum 3.78 (3.77) (the Rhodes and the voice closer, e.g. the voice at x4 1.58
  (2.30); the kick's quiet tail further, 7.60 (6.48)). FL's Stretch Pro splits the same way (grains above 1.5 kHz,
  also with PITCH; the same rise), FL's Stretch does not (the vocoder over the whole band at x1.5 and x2.5, as ours).
  e2 generic's split rises too, later (about 4 / 6.3 kHz at x2.75 / x3, the whole band from about x3.25); not
  modelled.

### Slices

- **Slice stretch:** FL's slice stretch behaves as e3 generic on each slice: its renders are FL's e3 generic exactly
  where it finds one slice (the Rhodes and the "ah": bit-identical; the 707 Kick -41 ... -46 dB). Ours restarts e3
  generic at each slice. FL's slices (Slice map at x2, where they show as runs of sound) start where a hit leaves the
  level before it, to a sample on e3_loop, and a fade-in (the "ah") has none. Ours takes a 128-sample block far above
  the four before it (the rule e3's attacks use) and the first sample above the level before the hit, within a sample
  of FL's on e3_loop. Against FL (nulltest/compare_slices.py, 20 cases each): Slice stretch envelope 1.75, spectrum
  2.16, null -5.54 dB; e2 transient, which also restarts at the slices, spectrum 3.19.
- **Slice map's slice edges:** fitted per slice on e3_loop at MUL x1.5 and x2, where silence separates the slices. FL
  starts a slice on a zero crossing: from the detected point it steps back to an exact zero or to the first sample
  past a sign change (0-2 samples on e3_loop; every one of the 16 slices). It plays from the output sample floor
  (start x MUL) (15 of 15 slices, exact at x2), at full level from the first sample, and ends with Transient (no
  bleeding)'s high-pass out-fade at half speed: it starts 942 samples before the slice's end (the next slice or the
  sample end) and is silent from 62 samples before it (a slice's tail -52.8 dB at both MULs; a gain fade fits to -21
  dB at best). Without a loop the fade also ends where the play head reaches the sample end (707 Kick at x0.667, -62
  dB).
- **Slices cut by the next one** (below x1 the play head reaches the next slice before the slice ends): the old
  slice's gain against FL's render less the new slice, in 16-sample blocks, falls smoothly from about 320 samples
  before the cut and goes on after it; fitted, it is the same half-speed high-pass fade, started 500-501 samples
  before the play head reaches the next slice (-62.2 / -43.6 dB on e3_loop's two cuts with sound in them; at full
  speed -26 at best), so the old slice fades out under the new one for 379 samples. At x0.667: e3_loop -53.3 dB, DL
  Groover -74.7.
- **Slice map null** (compare_slices.py, FL's channel volume applied, no gain fitted, ours with Use loop points on as
  in the FL template): mean -61.4 dB; x2 and x1.5 -52 ... -83 dB on all five samples. Not matched at x0.667: the
  Rhodes's 252-sample loop, which FL replays about every 168 samples once the play head wraps (-5.0 dB).
- **Which channel starts a slice** (stereo test files, nulltest/measure_slice_starts.py, MUL x2, 38 slice starts, all
  identical to FL's): FL stops at the first sign change in either channel, not in L + R (with the louder channel's
  crossing further back, L + R is wrong in 8 of 16); a digitally silent channel never stops it; both output channels
  start on the same sample. File markers are not moved: cue points (regions) and ACID slice markers start exactly
  where the file puts them, off a zero crossing or not. Unverified: an exact zero in one channel only (ours walks on
  past it).

### The grain modes' periods

Where FL's e3 mono, e2 mono and e2 speech splice, from which source sample each short piece of the output plays (FL's
output is pieces of the source; nulltest/measure_grain_marks.py, pulse trains of known period over a faint noise floor
at MUL x1.5 and x0.667, the same at both). All three jump by the period itself on pulse trains of 64 ... 1000 samples,
as ours; FL's e3 / e2 mono go on to 1100 (none at 1150), e2 speech to 750 (none at 800); ours stop at 512.

- **e2 speech** splices the whole number of periods nearest 390 samples: 52 -> 364, 64 -> 384, 76 -> 380, 100 -> 400,
  150 -> 450, 220 -> 440, from 300 the period itself (19 periods; at 52 and 60 the two candidates are equally far from
  390 and FL takes either). It keeps its last period where it finds none (a drum loop's hits: runs of 409, 431 and 200
  samples, where a fallback gives 384). Ours does both.
- The crossfade at each splice is complementary (a + b = 1) and about a period long in all three, as ours.
- Measured, not matched: FL's mono modes cross over between two pulses (0.42 period before each), at every period from
  150 to 800: grains on the waveform's peaks. On a drum loop FL's e3 / e2 mono splice every ~950 samples (a kick's
  body) and play 2000-3000 samples 1:1 between, where ours (a 128 / 256 sample fallback) splice every 384 / 768; on
  FL's 707 Kick e2 mono follows the attack's falling pitch (jumps of 45-55 samples), e3 mono plays runs of ~900
  samples with jumps of ~450. On noise FL's mono modes start at 256 (e3) / 128 (e2) samples, ours' fallbacks, and
  drift smoothly from there (233 ... 314 / 100 ... 163). The attempts to match these are under [Declined
  changes](#declined-changes).

## e3 generic in detail

What FL Studio's e3 generic stretch mode does, measured from its output alone, and how Chan Sampler's e3 generic / Auto
follows it. Everything below is behaviour, not code. [Time stretching](#time-stretching-against-fls-renders) has the
summary for every stretch mode; the tools that reproduce these measurements are the stretch scripts in `nulltest/`
([nulltest/README.md](nulltest/README.md)).

### e3: setup

| Item | Value |
|---|---|
| Reference | FL Studio 26.1.7, Sampler in e3 generic; FL's own Resample render is the reference source |
| Renders | FL renders only, through `nulltest/cs_ref.py` and `nulltest/queue_lib.py` |
| Probing the stretcher alone | MUL (2^(raw/10000)) with PITCH 0: TIME / MUL show the stretcher with no interpolation; MUL 1.5 = raw 5850, 0.667 = raw -5850; MUL = 1 is transparent (output == input) |
| Tape version in FL | FL ignores PITCH in Resample mode when it comes from a project: play the note key instead |
| The 30 pitch cases | `nulltest/cases_e3_pitch.py` through `nulltest/null_e3_pairs.py`: FL's 707 Kick, a Rhodes, a voice, two drum loops at -1200 ... +1200 cents, in FL; envelope and band-spectrum distance in dB, null raw and with the level fitted |
| The 20 offline cases | `nulltest/compare_stretch_material.py`: the stretcher alone, fed FL's decode of each sample |

### e3: architecture

- **Pitch = stretch, then resample.** With PITCH, FL time-stretches at the source rate and reads the result back with
  a short interpolator: the -700 cents output rolls off above 13 kHz and keeps -21 dB at 22 kHz exactly like our cubic
  FIFO read (within 1 dB), not a band-limited resampler.
- **Two bands, split at ~1.48 kHz** (steep: on saws harmonics up to 1470 Hz take the low path, 1500 Hz and up the
  grain path). Above: the output is literally pieces of the source (coherence 0.99+, gain 1.00). Below: incoherent
  with any grain offset even with 1024-sample windows -> spectral processing (phase-vocoder like). On saws the low
  band keeps the waveform shape (-23..-29 dB vs a shifted source) at its own offset, independent of the high band's.

### e3: high band, pitch-synchronous grains (PSOLA-like)

- Grains are played 1:1 (constant source offset) and spliced once per estimated pitch period: 147 Hz saw (period 300) -> splices exactly every 300 output samples; 100 Hz saw (441) -> about every 441.
- When the drift from the ideal position exceeds about half a period the offset jumps by one period, symmetric around
  the ideal (old offset about +J/2, new about -J/2). With a fixed period FL jumps every 3 grains at x1.5 (drift cycle
  -P/3, 0, +P/3), as the half-period rule gives. Grain centres from Hann crossfade fits: a jump repeats the same
  source position.
- Crossfades at offset changes fit a linear ramp of about 0.7 x the jump (consistent with 2-period Hann grains overlapping by one period).
- Grain positions are fractional: clicks come out at fractional offsets (+0.9, +0.65, +0.45 samples over three clicks at MUL 1.5, drifting slowly: FL's ratio differs from 2^(raw/10000) by ~1e-5).
- FL's output on a perfect 300-sample saw has one small glitch per period (its grains are not seamless): on the 147 Hz
  saw FL's high band keeps a constant phase (no offset slips at grain joins); the per-period difference sits on the
  saw's sharp edge, which FL renders slightly softer (residual -20 dB there, -24 dB elsewhere). Cause: FL's output
  sits at a fractional offset (43.5 samples on the saw) and reads through an interpolator that softens the treble by
  the fraction ([Ratio grid](#e3-ratio-grid)).

### e3: period estimator

- Noise plus one echo (x = w + w(n - D)), MUL 1.5: every jump equals D for D = 40 ... 512 exactly; D = 530 and up
  gives random jumps, so the search range is 40..512 samples. D = 20 / 30 give constant 50 / 256 (fallbacks).
- Noise plus echoes, FL's splice jumps (measure_ratio_grid.py's tracker, refined per splice): upper band only (an echo only
  below 1.4 kHz gives random jumps); the stronger of two echoes wins; a 0.1 echo sets 20% of splices, 0.2 78%, 0.35
  all (no floor); an echo that changes delay mid-file changes the splices from one near the change (window around the
  splice).
- On plain noise the "period" is whatever the estimator returns: jumps of 94..548 samples, spanning 132 / 215 / 283 /
  337 / 397 (10..90th percentile): a lean to mid-range lags. It is not the plain autocorrelation peak of the source
  (full, low or high band; 256..2048 windows, anchors -3000..+3000 samples: at most 2 of 8 noise jumps reproduced);
  not a lag-length window, not a fixed analysis grid, not a "first peak" rule; the low band alone matches worst. The
  best definitions found: the normalised autocorrelation of the high band, 512 samples from the grain centre on (24%
  of jumps; chance < 1%), and any normalised correlation over a window of 512 at the splice + 128 (21% of splices);
  the envelope, the cepstrum and period-length windows do worse. Without FL's estimator the grain band cannot null on
  real material.
- FL decides the jump before it is due (jump size correlates 0.76 with the drift at the jump), so its estimate is made earlier; when is not known.
- Ours: range 40..512, the normalised autocorrelation of the high band on the grain cache (reused within 512 samples),
  no floor and a mid-range pull (score - 0.04 ((L - mid) / half)^2): splices on noise 139 / 184 / 255 / 336 / 397
  (10..90th percentile). With the true period on a 147 Hz saw the grain band is -20..-25 dB from FL's (the rest is
  FL's own splice glitches).

### e3: ratio grid

- FL's e3 stretches by floor(MUL x 512) / 512 and resamples the remainder (e2 transient the same, e2 generic 1/256).
  Found from a 15-20 kHz level ripple FL makes on noise: its rate is the drift |MUL / grid - 1| x 44100 Hz, predicted
  to 1% at 25 settings (25-28 dB peaks; none at grid ratios). Rounding to nearest fails; so does a grid on the speed.
- The ripple is the read interpolator's treble loss following the fractional position. Measured per grain run on noise
  (MUL 1.5): |H| at 6/10/14/18 kHz against the fractional offset matches a 50/50 blend of Catmull-Rom and 4-point
  Lagrange (mean error 0.004; Catmull alone 0.020, Lagrange 0.023, Keys a -0.4 0.017, windowed sincs worse).
- The fraction drifts linearly: FL's jumps are the period + 0.025 samples at MUL 1.5. Requested 1.500039 / 1.414214 /
  1.231144 -> FL ~1.5 / 1.414062 / ~1.230769 (within-run drift 2.2e-5, 7.6e-5, 2.6e-4 per sample).
- For PITCH, FL's rolloff at -700 cents matches our Catmull read (the blend a little too dull); at +700 FL loses 1-3 dB at 18-22 kHz that no kernel alone gives.
- Ours: the stretcher runs at the grid ratio, the FIFO is read at increment x grid / wanted.
- Our cubic read at exactly x2 (+1200) samples only every second synthesised sample from an even start, so a
  one-sample click on an odd sample vanishes. On single-sample clicks at exactly +1200 cents (e3_par.wav, 16 clicks on
  alternating even / odd samples) FL drops the same parity as ours (even ones vanish in both, and both drop the first
  few); the surviving clicks play at x0.53-0.82 in FL, x1.0 in ours (from another start parity FL keeps ~0.8 of a
  click's energy). Real audio is not affected much.

### e3: attack timing

- Clicks land on time (0..2 samples), as one copy of the source.
- **Onset placement.** Noise bursts after silence (e3_onsA: flat, start phase varied; e3_onsB: decays of 20..1000
  samples, flat, 300 / 1000-sample fade-ins), MUL 1.5 and 0.667: FL puts a reference point R samples after the burst's
  start on time and plays 1:1 around it (offset = R (alpha - 1); both bands alike). R = the first sample, within 400
  samples of the start, that reaches 95% of the loudest one: 16 of 20 bursts to the sample (loudest sample alone
  15/20; centroids, smoothed energy and the low band much worse). Fast decays land on their start, flat noise on its
  peak, fade-ins late. The same rule puts a 1 kHz click's reference 9 samples in, R (1 - 1 / pitch ratio) output
  samples (-8.3 .. +3.35 over the pitches), so e3 generic needs no per-pitch attack table. Ours (both bands): bursts
  land within 3 samples of FL's in 29 of 40; clicks keep -16..-31 dB. Unexplained: 4 bursts whose R needs a shorter or
  longer window (it may follow the grain period).
- **Frame grid (MUL / TIME).** 64 identical noise bursts stepped 8 samples through a 512-sample grid (e3_grid2.wav),
  MUL 1.5 / 0.667 / 2: FL times each burst either from its start or from its loudest sample 469 in, decided only by
  its position in a 512-sample grid counted from the note's start (start mod 512: 0-168 and 456-504 from the start,
  176-448 from the peak at x1.5 and x2; switch points 224 / 408 at x0.667). Model: FL's search window runs from the
  attack's start to the first grid point (offset ~410 at x1.5 / x2, ~364 at x0.667) at least ~240 / ~144 samples
  later. Fits 80 of 84 bursts at x1.5; ours times all 64 grid bursts like FL's at x0.667 (and at x1.5, apart from a
  test-ratio drift). Ours uses the grid window only when the pitch ratio is 1 (MUL / TIME), the PITCH grid below
  otherwise.
- **PITCH grid.** e3_grid2.wav at +700 / -700 cents, mode 1: bursts at grid phase 232-400 (+700) / 224-400 (-700) are
  timed by their peak, the rest by their start. The window model fits all 128 with offset ~360 and a gap of 129-136
  (up) / 137-144 (down) samples. The gap counts from where the attack is under way (first sample at 25% of the loudest
  power over the next 512): a ramping drum hit in DL Groover (1% at +0, 25% at +53) is timed by its peak in FL, which
  only that start puts on the right side of the grid. All 192 MUL grid bursts and 32 of 40 onset bursts stay as FL
  times them (31 without the PITCH grid).
- **Note start.** FL plays 1:1 for 336 / 368 / 528 / 704 / ~1000 / ~1150 samples at x0.5 / 0.667 / 0.8 / 1.25 / 1.5 /
  2, then jumps to within ~100 samples of the ideal position; ours holds the measured note-start time for every
  attack. Noise-burst onsets inside a sample are NOT placed at their ideal time (+152 samples at x1.5, -99 at x0.667),
  unlike clicks. The same clicks on steady noise (e3_impn.wav) are NOT treated as attacks: placed up to +-190 samples
  off and not boosted.
- **Soft starts.** The vocal's soft start is timing, not a fade: FL treats the swell as an attack and times it from a
  later reference point (R 1258 vs our 922 at x1.5, 1137 vs 873 at x0.667), so its rise comes out later; FL starts the
  soft vocal at x0.2-0.4. Fade-in bursts are only attenuated in their first 128-256 samples (-3 to -12 dB). The grains
  alone need +60 source samples at x0.667 (cleared at an attack): e3 soft-start error 9 samples mean over the four
  cases (28 without). On a slow swell FL measures the "loudest" point differently: on the vocal at +700 FL's reference
  point (496 samples into the swell) is between a 400-sample window's (388) and the grid's (728).

### e3: attack gain

- Single-sample clicks after silence (e3_imp.wav) come out as one clean copy but louder, in both bands: x1.40-1.45 at
  MUL 1.5, x1.26-1.31 at MUL 0.667. MUL (no resampling): x1.2 at x0.5 / 0.667 / 0.8, x1.05-1.41 at x1.25, ~x1.38 at
  x1.5, x1.36-1.63 at x2: it varies from click to click (at x2: 1.63, 1.60, 1.49, 1.36 for four identical clicks).
  PITCH (after dividing out the cubic read's own energy change): ~x1.2 down, x1.3-1.4 up. The low band alone: x1.46 at
  x1.5, x1.6 at x2. A noise onset starts at x1.16 and settles to x1.0 over ~500 samples.
- Aligned on FL's attack timing, the gain during the attack hold per band (noise bursts, 128-sample windows): the band
  below the split is a clean copy of the source, louder: x1.1-1.4 (up to 1.64) at MUL 1.5, ~x1.0-1.3 at 0.667, varying
  from burst to burst (flat bursts alone: 0.98 .. 1.64). The grain band is not a clean copy there and not boosted
  (x0.6-1.0, other grains mixed in). So the click boost is mostly the low band.
- The low-band gain during the hold follows the grid phase smoothly (x1.5: 1.10 -> 1.24 in the start-timed range, 0.85
  -> 1.59 in the peak-timed range). By grid phase (MUL): timed by its start ~x1.13 flat, timed by its peak 1.13 +
  0.0019 per sample of phase into the peak range (x1.5 / x2); mean error on the 64 bursts per ratio 0.067 (x1.5),
  0.111 (x2) against 0.121 / 0.160 for a flat gain; squeezing varies within the attack (x0.667: up then down to x0.7)
  and keeps x1.1 (0.132 either way). Neutral on real material.
- FL starts the 707 Kick ~x1.4 louder than the source at +-1200 (x1.1 at +-300).
- Ours: the vocoder's locked attack frames x1.2 (stretching) / x1.1 (squeezing), the measured means; the hold gain applies below 1.485 kHz only. The rule for when FL boosts is unknown.

### e3: low band

- On saws the low band keeps the source's phase at the same time, 1-2 samples late, like a plain phase vocoder; ours
  is -17..-26 dB from FL's there. At MUL 0.667 ours drifts against FL by ~0.1% (open).
- **Note start.** Saws: FL's harmonics come out scattered +-10 deg against the source (x1.5), set in the first ~2000
  output samples, then steady; the scatter scales with stretch - 1 (x2: double, x0.667: -2/3); ours has none. A lone
  sine (no treble): FL plays a 1:1 copy for ~1100 output samples (x2), then crossfades (level dips to 0.13 where the
  phases oppose) into a vocoder restarted near source 1035 (x1.5, x2) or not at all (x0.667, x1.231 at some
  frequencies); chaotic between 330 and 400 Hz. Not modelled.

### e3: low band after an attack

- **Method.** Between restarts a phase vocoder's steady partial keeps one phase relation to the source, set where its
  phases were last restarted: a restart from the source frame at P, played where the stretch puts P, turns a partial
  of frequency w by -w (stretch - 1) P against ours. So the low band's phase against ours locates FL's restart
  (several inharmonic tones make it unambiguous); the cross-spectrum of FL's low band and ours gives it as a phase
  delay on real material (measure_e3_lowband.py case).
- **The rule.** Five steady tones (211 ... 1129 Hz) switched on by a treble click, the onset stepped 16 samples
  through 512 (lowband.py probe): FL restarts from the loudest sample of the band below the split in [onset + 660,
  onset + 1172), 20 earlier where the timing grid times attacks by their peak, played where the stretch puts it. The
  restart lands on the tone sum's loudest peaks (5140, 5667; window start boundary 644..676 after the onset at x0.667,
  x1.5 and +700 cents; length 495 ... 1863). A treble click train rising through the window does not move it (the
  loudest point is the low band's). Same structure at x2.
- **A sung "ah"** (+700 cents, the stretcher alone): FL's restart is at its sample 1470 (ours without a restart keeps
  the attack's own phases at 1373); with it the band below 1.3 kHz nulls -14.2 dB (+4.7 without). The point does not
  move with level (-12 / +6 dB). Padded with 0..496 samples of silence, FL's point flips between the swell's peaks
  (1474 for pads 0-48, 1366 for 64-368, 1479 for 384-496): under the rule FL's onset is at about sample 320 of the
  swell, ours 603 (our detector needs an 8x block-energy jump). No block-energy rule fitted the 32 pads (relative
  rules fail on the leading silence; the best fits sit on the boundary).
- **Ours** (e3 generic, Auto): at each vocoder attack lock the restart point is found as above and, from the first
  unlocked frame centred at or after where it plays, the partials are turned by -w (stretch - 1)(restart - attack)
  (the phase advance of each peak, the locked bins following). Probe against FL: within 3 samples of FL's restart over
  most of the grid at x0.667 and +700, and 1-3 deg at onsets 208-464 at x1.5 (50-60 deg without the turn); at x2 two
  stretches of the grid miss. For steady partials a phase reset at the restart (the source's phases, read so the
  restart point plays where the stretch puts it) is the same thing: the steady-tone probe gives the same offsets to a
  sample and real material the same nulls, so the turn stays.
- **When it plays:** the probe's crossfade is where the attack's 1:1 copy reaches the restart point (x1.5: predicted
  7288, measured mid-point 7272), not where the stretch puts it; but on the loops the stretch-map time nulls better
  (offline, 18 cases: 1.16 against 1.44 dB), so that is used.
- **Skipped restarts:** FL does not always restart. The groove's hit at 44085 (+700): FL follows ours without the
  restart. The loop's two hits repeat every 41340 samples: the same hit is restarted at onset mod 512 = 66 and 132 and
  not at 260 ... 450 (PITCH). The probe with tones decaying (tau 3000, x1.5): restarted at 96 ... 352, kept at 0 and
  416 ... 480. Steady tones are restarted everywhere. FL seems to restart a held onset later and a drum hit not (or at
  once, which our attack frames already are). The rule (grid range and decay) is not found.

### e3: swell onsets

Measured at x2 (FL's ratio grid: after a restart at P FL plays the source with phase -w P, offset 0, calibrated on the
click probe to 1.6 deg); the restart frame's own bias fits best with a 2048-sample frame.

- FL's onset detection is level-free: the vocal at -12 / +6 dB and a swell at -20 dB restart where they do at 0 dB. A
  note that starts in digital silence and swells: FL's low band is digital silence until its first restart frame fades
  in.
- Treble alone (a 3-16 kHz noise swell over steady low tones) gives no clean restart: the low band restarts on its own onsets.
- Five equal steady tones under swells (-60 dB, +2 / 3 / 4 / 5 / 8 dB per 128, then full; t0 stepped 32 through 512;
  lowband.py swell; 80 cases: 5 rates x 16 onsets): FL restarts on the tone sum's peaks. Every restart window starts
  on 512 k + 148 from the note's start (the click probe's onset + 660 with the onset on the grid). In the middle of
  the grid cycle (about the range where attacks are timed by their peak) FL restarts twice: a first restart, then ~730
  samples later a second one; elsewhere once. The first restart sits a nearly fixed time after t0 per rate (about
  +3800 / 2750 / 1960 / 1590 / 1050 samples, i.e. where the swell is near -40 dB at 5 dB / 128, later in level for
  slower swells), about where the swell stops rising (full level at 3840 / 2560 / 1920 / 1536 / 960 samples after t0
  for 2 / 3 / 4 / 5 / 8 dB per 128; FL's restarts are within a few hundred of those).
- Not fitted by: a level crossing (sample or 64 ... 512-sample blocks on the note's grid, any threshold and offset: <=
  48 / 80 first restarts), a look-ahead level ratio, a block energy-jump rule (first or last), FL's attack-timing
  window with a per-rate onset (6-12 / 15), frame energies of 512 ... 4096 samples (<= 45 / 75).
- **A per-peak model** (offline, not in the plugin and not in the repository): per spectral peak, from a relative
  spectral flux (against the spectrum a few hops back, so level-free) and the energy's time centroid inside the frame;
  a peak whose energy sits late waits, and restarts once its centroid reaches the frame's centre; the restart point is
  the loudest sample of the windowed full-band frame within a hop of its centre. Against the 80 swells (FL's restarts
  from the cached renders by `measure_e3_lowband swell`), with the frame grid, window shape and centroid threshold
  fitted: 52 / 80 first restarts to the sample (a 4096-sample frame on a 512 grid from the note's start, offset ~100,
  Hann or Blackman window). Where it is right it is right to the sample (3978 against FL's 3977, 1571 against 1571),
  which supports the restart point; where it is wrong it is one frame late (+527 / +528) or ~200 late. The restart
  point weighted by a much sharper window than the analysis Hann (Hann^3 or Blackman^2 fit best; with plain Hann a
  rising swell's later, louder samples win): 67 / 80 with the centroid threshold refitted (0.3 of half a frame); the
  rest a frame early or late. The flux threshold hardly matters (0.5 ... 1.0 the same); the centroid threshold does.
  With generic peak regions (peaks above -40 dB of the frame's loudest, each with the bins down to the minima beside
  it) the decision has to be weighed by energy (the restarting peaks above ~10% of the low band's energy; 0.1 ... 0.5
  the same): 66 / 80; without it any sidelobe peak restarts (34 / 80).
- **A swell detector of our own** (offline): 4096-sample Hann frames on a 512 grid from the note's start (offset 0); a
  frame whose energy is above 1.05x the last frame's with its energy centre past 0.35 half frames marks a swell under
  way; the first later frame whose centre is back within 0.35 restarts at the loudest sample within a hop of its
  centre, weighted by Hann^3: 62 / 80 to the sample, most misses a frame late.
- Ours (`measure_e3_lowband swellours`): 26 / 80 to within 3 samples, all at 5 and 8 dB / 128 where our attack rule
  fires; the slow swells never restart (no 128-sample block 8x the four before it). Our attack rule's 1:1 lock is
  itself a restart (its frames take the source's phases) and claims 4 dB / 128 swells ~200 samples before FL.

### e3: the split at large stretches

Measured with `measure_split_rise` (in `nulltest/`) (noise, per third-octave band, correlation with the source: grains ~0.85+, the
vocoder a falling trend):

- FL's e3: the split stays at 1.485 kHz up to x2, then the bands that stop being pieces of the source start at 2.5 /
  3.2 / 4 / 5 / 6.3 / 8 / 12.7 kHz at x2.2 / 2.3 / 2.4 / 2.5 / 2.6 / 2.75 / 3; at x4 none are. (stretch - 1)^3 x 1.485
  kHz lands in the same band at every one; ours (followSplit) gives the same rows. Key changes on noise (the output
  resampled back) do not separate the two readings; the engine's own split follows the stretch factor, so ours does.
- Real material x2.5 / x3 / x4 (15 FL renders, beats=24, the stretcher alone): envelope 2.287, spectrum 3.775 (2.295 /
  3.766 with a fixed split). The Rhodes and the voice closer (voice x4 1.58, 2.30 with a fixed split), the kick worse
  (x4 7.60 against 6.48, its quiet tail), the loop mixed.
- Other modes, the same probe: FL's Stretch is the vocoder over the whole band (x1.5, x2.5). FL's Stretch Pro is e3's
  structure, grains above 1.5 kHz and the same rise (x2.5 ~5 kHz, x3 ~12.7, x4 none), and keeps the grains with PITCH
  (+700 cents: 0.91 above 1.6 kHz). FL's e2 generic rises later (about 4 / 6.3 kHz at x2.75 / x3, all vocoder from
  x3.25); its rows are noisier, not modelled.

### e3: ours, in the plugin (TimeStretch, e3 generic / Auto)

- Vocoder bins below 1.485 kHz (the split rising as above); above, PSOLA grains from a cached high-passed copy of the
  source (overlap-save, the complement of a Hann windowed sinc as long as the vocoder frame), period from the
  estimator above. From an attack within the crossover filter's reach the grains play 1:1 in the attack's timing (its
  ringing lines up with the vocoder's band); drift is caught up after it.
- All vocoder modes: the start alignment uses the hop in use (the quarter-frame hop puts steady content 256 samples
  early unpitched); the note-start lock runs also when the first window begins after the start (pitch down), and its
  target position agrees with the alignment.
- The ratio grid, the attack timing and gain, and the low band's restart turn, as above.

Effect of each element in FL, the 30 pitch cases (envelope / spectrum in dB, null raw and fitted in dB); the last row
is the current state. CPU: e3 generic 38% of a core for 32 voices with the grains (28% without).

| Element | Envelope | Spectrum | Null raw | Null fitted |
|---|---|---|---|---|
| Python prototype of the hybrid (our vocoder below 1.485 kHz + PSOLA above: period from the normalised autocorrelation, 1024 window, 90..600 lags; grain centred on each onset at its ideal time; cubic resample), not kept | 2.02 -> 1.78 | 2.47 -> 2.46 | | -1.85 -> -2.21 |
| Vocoder + grains in the plugin | 2.01 -> 1.59 | 2.44 -> 2.08 | | -2.00 -> -1.93 |
| Period range 40..512, high-band estimator, note-start hold | 1.59 -> 1.57 | 2.08 -> 1.94 | | -1.93 -> -1.90 |
| Onset placement (R rule) | 1.57 -> 1.62 | 1.94 -> 2.02 | -0.34 -> -1.51 | -1.90 -> -2.79 |
| Attack frames x1.2 / x1.1 | 1.62 -> 1.64 | 2.02 -> 2.01 | -1.51 -> -1.44 | -2.79 -> -2.86 |
| Frame grid window (MUL / TIME only) | 1.64 | 2.01 | -1.44 | -2.86 |
| PITCH grid | 1.64 -> 1.64 | 2.01 -> 2.02 | -1.44 -> -1.21 | -2.86 -> -2.72 |
| Ratio grid | 1.64 -> 1.64 | 2.02 -> 1.99 | -1.21 -> -3.06 | -2.72 -> -4.44 |
| Low-band restart turn (current) | 1.64 -> 1.70 | 1.99 -> 2.02 | -3.06 -> -3.30 | -4.44 -> -4.57 |

Per case: the PITCH grid costs the vocal at +700 (-5.4 -> +4.5 dB) and nothing else; on the 20 offline cases (e3
generic, floored envelope) it moves 1.73 / 1.99 / -2.98 -> 1.70 / 1.99 / -3.07. Over whole renders the ratio grid gains
the Rhodes at +-300 / -700 3-4 dB and the vocal -300 2.1 dB, loses the vocal -700 2.9 dB (1.0 -> 3.9), the rest within
0.4 dB. The restart turn: vocal +700 4.5 -> -5.5 dB, groove -700 / -300 / +300 by 1-2 dB, the loop by ~1 dB; vocal -300
-0.6 -> 4.6 and +1200 -3.4 -> 1.2 (our onset on the swell), groove +700 -1.1 -> 0.4.

### e3: declined changes

| Change | Measured effect |
|---|---|
| A gain model on our detected attacks (x1.2 .. x1.5 by stretch, flat 30% of the hold, back to 1 by 60%) | the 30 pitch cases' envelope 1.59 -> 1.60 .. 1.62 |
| A start lock that needs a real attack | nothing for the vocal, broke the kick |
| The frame-grid window with PITCH too | null fitted -2.86 -> -2.52 dB (the vocal and the groove) |
| A stricter attack detector | moves R towards FL's on the vocal but costs the other cases (20-case offline score) |
| The first low-band sample within 90% amplitude of the window's loudest (from our own onset) as the vocal's restart | offline 18 cases +1.17 -> -0.03 dB, but FL's restart matched in only 6 of 32 paddings (our onset follows our frame grid, FL's a 512 grid from the note's start): a coincidence of the test file's position |
| The per-peak swell model in the plugin (a detector on FL's grid ahead of the vocoder, a phase turn from the position the low band's phases follow) | 26 / 80 swells with and without it: on faster swells the detector found FL's restart to the sample (+1571 at 5 dB / 128) but our attack rule claims the swell first (its restart 20 samples early); on the slowest it fired a frame late; where it did restart the probe saw no restart in our output, so a turn relative to our phases is not FL's restart (FL's resets the phases) |
| Swell restarts as a phase reset of the low band (the own detector above; the analysis phases, each bin moved by its measured frequency x the distance between the frame's centre and the restart point, in the source and where the stretch plays it; a frame's sample i sits at synthPos + i in the stretcher's stream; applied on the first frame starting half a frame after where the restart plays, deferred past an attack's 1:1 frames, an attack inside a swell leaving the restart to it) | 53 / 80 (on the restart frame itself 42, a hop after 47, half a frame 53; peak frequencies for the off-peak bins 41: earlier frames sit on the rise, which skews their phases by up to 8 degrees). Stretcher alone: null -5.07 -> -5.37 dB, envelope 1.74 the same, spectrum 2.00 -> 1.99 (DL Groover +2 dB at three settings, e3_loop mixed). In FL: envelope 1.70 -> 1.67, but null raw -3.30 -> -3.05, fitted -4.57 -> -4.46 (DL Groover at -700 / -300 / +300 cents ~2 dB worse, where the stretcher alone had it better), and the engine test "a soft start lines up" failed. Kept as a patch outside the repository |
| e3's grains in Stretch Pro (with e3's estimator too) | MUL cases spectrum 2.16 -> 2.26 (the Rhodes at x0.667 nulls -13.3 dB, -2.6 without, the rest no better); PITCH cases far worse (the voice at -700 4.6 -> 6.8): the grains are resampled with the rest, so they move the formants Stretch Pro keeps; needs grains that keep the formants |
| The hold gain applied to the whole band below the split as it rises | the kick worse still |
| The restart point from the full band, not the band below the split | no net change (voice x1.5 -5.8 -> -4.6 dB, groove +700 -1.7 -> -3.3) |
| A phase reset at the restart instead of the turn | the same offsets and nulls; a reset placed a hop late scores better on the drums |
| No restart after an attack | the groove at -700 and x0.667 -3.4 -> -5.5 dB, the voice -5.8 -> -0.9 |
| Restart decider: the band's energy after the onset against its first 512 samples | a threshold that keeps the voice's restart (2x) also drops the restarts of held tones, which FL makes |
| Restart decider: the whole band's energy centre on FL's frame grid with the per-peak model's threshold, skipping restarts that land near the onset | the drums gain (null -5.07 -> -5.36 over the 20 cases) but the steady-tone probe falls from 15 / 32 restarts to the sample to 1 / 32; the decision is per spectral peak in FL, and a whole-band centre does not make it |

### e3: open

- The period estimator's exact choice on noise (when it is made; 21% now).
- Grain window shape, how analysis marks are placed.
- FL's stretch ratio within a grid step: no single grid or integer hop pair fits the three measured ratios.
- When FL skips the low band's restart (decaying attacks, by grid range), and the note start's (the sine's restart near 1035, the saws' scatter).
- FL's onset on a slow swell (the "ah": ~320 against our 603), which sets both its restart and its attack timing; and
  its second restart in the peak-timed part of the grid. The per-peak model gets 67 / 80 swells; the remaining 27 of
  ours start with our attack rule's 1:1 lock.
- Why the groove goes opposite ways in FL and offline with the swell restarts (the in-FL case is the whole plugin, the offline one the stretcher fed FL's decode).
- The transient gain rule, and the per-burst variation of the hold gain (probably where the attack falls in FL's frame grid).
- Ours drifts against FL by ~0.1% at MUL 0.667 in the low band.

## FL Sampler presets (.fst)

A channel preset holds one channel's settings as a project does. Source/PresetImport.cpp reads the Sampler's settings
with the scales above and tells a Sampler preset from another instrument's. The preset's own channel pan / volume are
folded into Levels, as a plugin channel applies its own. FL 4 and earlier store pans and volumes at 1/100 of today's
scale (echo pan centre 64, not 6400; FL 9 onwards are current, measured across FL's preset library). Checked by writing
each in-FL null case's Sampler channel out as a preset (nulltest/cases_preset_import.py): the import renders
bit-identically to the parameter settings that null in FL, in all 177 cases. The FL trial cannot save presets, so
FL-written files are untested here.

## Skin (measured in FL, drawn in code)

Captures of FL's channel settings window at 100 % (Windows, default theme, no scaling) were measured for positions,
sizes, colours, gradient directions, font metrics, timings and drag laws. The colours measured are recorded below; the
editor uses Chan Sampler's own palette (true black, neutral greys and white, violet for the value arcs, lit switches and
selected tabs; `shared/ui/Theme.h`) with the measured sizes and positions. `shared/ui/Widgets.cpp` and `Source/ui/Controls.cpp` draw
every part in code from them with our own shading (discs, rings, arcs, rounded rectangles, gradients between measured
end colours), and each icon is our own glyph in the box FL's occupies. The editor is compared with FL's by layout:
positions and sizes within 1 px (the icons' ink boxes: 0 to 1 px), not by pixel difference. Every right-hand panel edge
sits at x = 553 of the 563 px canvas, as in FL.

- **Surfaces:** sheet #202324, panel body #222525 with a 1 px #1A1D1D edge on all four sides; the header tab #1F2222,
  20 rows with a #1A1D1D line under it, reaching 11 px past the title at the top, cut by a shallow bevel over its last
  17 px of the top row and leaning back 10 px over its height; titles 11.7 px Segoe UI at #D4D4D4 from 4 px in. Top
  bar #1B1D1E with #151718 rules above and below (rows 0 and 46), the selected sheet tab's 44 px cell #202324, the
  sheet icons in 28 x 21 / 25 x 24 / 19 x 20 px boxes (our own: a sound wave, an envelope, a spanner), the effects
  view's spanner and valve and the LFO shapes likewise.
- **Text:** labels 11.7 px (#D0D0D0), switch captions 11.1 px, knob captions 7 px bold (#C4C4C4) drawn with their
  baseline on the label's last row from a whole-pixel start (FL's are hinted to the grid; our caps fill the same 5
  rows), the filter panel's MOD X / MOD Y 9.5 px bold (7 px caps, measured), titles 11.7 px (#D4D4D4), file name
  #BCBCBC, the lit envelope target #40F59A; all Segoe UI em sizes (JUCE height = 1.333 em). Ink totals of FL's and our
  words agree within 5-10 %.
- **Knobs** (22 to 38 px): a 1 px #191C1F outer ring, a 2 px bevel #25282B (top) to #1B1D1F (bottom), a 1 px gap
  #1D2023 / #131517, a body 4 px in darkening downwards (#353739 to #07090B) with a light along its top rim (#595B5D)
  and a fainter one along its foot; the value dot 3.6 px inside the body, 1.5 x 1.8 px, in 73 % of the arc colour; the
  arc 2 px wide on the bevel (1.1 to 3.1 px in). A knob turns a full circle: value 0.5 is straight up, a one-sided
  knob starts and ends at the bottom. FL draws a knob 1 px in from the top left of its control rectangle. Arc colours
  per control (measured in FL): amber #FFA830 for most, green #47F49A for the INS envelopes, VOL and the echo's FEED,
  teal #5CEEFF for PAN, yellow #DBF459 for MOD X, blue #3B6BE2 for the tracking and echo MOD Y, pink #A05265 for the
  echo's PITCH. Hovered, a knob lightens by about 4 luma.
- **Filter knobs** (66 px): a cap 17.5 px across its radius, lit from above (#575859 at the top, #2B2C2D in the
  middle, darker at its sides) with a soft light up to its right; a dark collar; a ring out to 23 px lit at its top
  (#5A5B5B), dark at its sides (#0A0B0B) and #141515 at its foot; a soft shadow round it; nine 1 px tick marks 29.5-36
  px out every 40 degrees and the dot (green on MOD X, gold on MOD Y) 22.5 px out; the sweep is 320 degrees. Hovered,
  FL's body luma nearly doubles (+52) and a dark collar with five notches 72 degrees apart (about 2.4 px deep, 41
  degrees wide, the first 32 degrees on from the dot) appears round the cap and turns with the value. Ours lights the
  cap a little, tints the ring with the knob's colour and grows the notched collar out from the cap with the hover
  fade (eased), turning with the value; at rest it shows the value as an arc just outside the ring, like the small
  knobs.
- **Round switches** (15 px; the effects view's CLIP is an 11 px one: 5 px disc lit along its top #4C4E4F to #191B1C
  at its foot, 1 px gap, 2 px ring, with a 7 px bold caption 21 px in): ring #5C5F5F (6.1-7.6 px out), gap #0F1010, an
  inner disc #2D3031 (top) to #454849 (bottom) with a +31 lit rim towards the top; lit: #EC734D to #F9D7B6 downwards,
  lit edge, a cream #FCF5D5 specular at the top, gap #942B15, ring #5C5858. The light sits 4 px in and 1 px below the
  control's centre, the label 7 px after it (the Playback panel's two lights sit 3 px further left and 1 px higher).
  Nothing changes under the mouse.
- **Drop-down field:** drawn 1 px in from the top left of its rectangle: frame #1B1E1E, inner edge #313233 (top) /
  #2C2D2E (sides) / #262728 (bottom), fill #202122 to #191A1B, text 11.7 px #D0D0D0 from 4 px in, a 4 x 7 px #686C6E
  arrow 6 px from the right that points down in #C0C6C9 while the list is open.
- **Push button** (Reset, Cut self, the file buttons): a 1 px #141717 frame with rounded corners (#1E2121 outer,
  #161919 inner pixels), a #262729 inner row along the top, inner columns down the sides fading #252628 to #1B1C1E, a
  fill #202123 to #18191B dithered, a #1B1C1E inner row at the bottom. Reset is 46 x 21 px in 11 px text; Cut self 61
  x 31 px in 12 px (FL's caps are 9 rows); pressed: everything darkens and the label (or icon) drops a pixel; no hover
  change. A greyed control (SLIDE with Porta and Mono off) is the control at 39 % over the panel, text included.
- **Digi wheel:** a rounded box (radius 4.7) 2.8 px in from the left and 0.7 px from the top of its rectangle, 28.4 x
  21.7 px, lit from its left middle (#5E6366) and darker towards the right (#50555A); two 5 px #303538 triangles with
  their apex 6 px from the right on rows 6-8 and 12-14; a value in 18 px #C5CACE, an empty box three 1.5 px #8F9598
  dashes 3 px apart on row 10. Hovered: the box lightens and the triangles disappear. FL's digits come out narrower
  than Segoe UI's (hinting); not reproduced.
- **Sliders** (fine tune, Mid): a 4 px groove (#171A1A edges, #1D2020 floor 3 px inside its ends) 4.5 px in from the
  control's ends, 104 px long on the fine tune and 69 px on Mid; a fill from the default position in the line colour;
  1 px ticks (#171A1A, top pixel #1C1F1F) 10 rows below the groove's top, 5 rows tall, every 12 px on the two-sided
  track and 6 px on Mid, standing on a #202323 band (top row #202424) that stops a pixel short of each tick; the thumb
  an 18 x 17 px ball (rx 9.2, ry 9.1): a 1.7 px dark rim, a body darkening downwards (#353839 to #070A0B), a light
  band 2 px inside the rim at the top, a softer light at its foot, a 1 px line down its middle (#FF4E60 on the fine
  tune, #FFA830 on Mid), and a shadow on the ticks below (to 13 px from the centre). Hovered the rims brighten by 12.
- **Effects view:** a sheet-coloured (#202324) 42 x 69 px well behind BOOST and CLIP; the reverb type an upright 14 x
  27 px pill (#3C464B outline, #1D1F1F inside) with a 12 px #A6ABAF knob (#B9BEC2 rim) at the top for A and the bottom
  for B, the letters in 11.1 px text beside it.
- **Keyboard:** 91 px octaves, 13 px white keys shading #BABEC5 to #CFD3DA downwards under a 15 px #BABEC5 label strip
  and a #A3A7AE rule, C keys and the root flat (root #77A9E6 over the full height, labels #272224 / #1F4F9E); black
  keys 8 x 40 px from the rule at 8, 23, 47, 61 and 75 px into the octave: #2F3031 outline, a 5 px body #3A3B3C to
  #575859 over 27 rows, two fading rows, a 6 row front face, a #575859 lip, a #979AA0 shadow row on the white keys, a
  lit left column #4C4D4E to #6A6B6C; a played key #ECA449. Bar: #3F4042, 2 px #242527 chevrons, a 57 x 3 px grip
  #252628 / #202123.
- **Sample view:** a #090B0C line, the 107 px view 1 px in from the left with a #161819 column at its right and a
  #090B0C line under it, then the 18 px bar; the 0.1 s grid is kept down to 13.6 px per step. Envelope / LFO curves
  #3BC97E, 1.5 px, points 8 px #40D989 rings filled #224534.
- **Layout audit:** every word and icon was located in FL's captures and ours (bright-ink blobs, `audit.py` in the
  skin harness) and the offsets closed. FL's Tracking panel is 116 px wide at x = 437 next to its Time panel (which
  Chan Sampler has no features for); in Chan Sampler Polyphony and Tracking share that row equally (178 and 177 px)
  with Tracking's knobs spread over its width and a 104 px Mid slider, so this row is laid out, not copied. The
  arpeggiator's directions are our own symbols in FL's 19 px cells, on a dark box when chosen; the Playback panel's
  loop link is a 9 px icon of ours on the caption row between START and END, its knobs 30 px at 6 / 40 and its second
  row (Slide, Chord, the 105 px chord list) 4 px higher and 10-14 px further right; the echo knobs sit in FL's pairs
  (6 / 38 / 80 / 112 / 154 / 196) with the two switches label-first and their lights at the right; Cut and By are
  right-aligned to their boxes; the Playback knobs are at FL's 17 / 70 / 112 with the lights 2 px higher; the LFO
  knobs 10 px higher; the envelope's menu arrow is 5 x 8 px, 4 px lower; the Tempo / Global switches sit 7-14 px
  further right with the light 4 px after the text; "Fine tune" and its track at FL's right-hand place (the hint moves
  left); drop-down text a pixel higher. SLIDE's greying follows Porta / Mono from the attachments' own values (a
  parameter's listeners run before its raw value is stored, so reading the raw value greys it a click late).
- **Hover animations** (measured frame by frame in FL, 45 frames over 310 ms after the mouse arrived or left): knobs,
  filter knobs, slider thumbs and digi wheels fade their hover highlight in as an exponential (half way at 55-75 ms,
  90 % at 160-190 ms; implemented as a fifth of the remaining distance per 60 Hz frame) and out as a straight fade
  over about 300 ms (90 % at 250-265 ms). Under the mouse FL's filter knob body lifts by 54 luma and the bevel's lit
  arc grows; ours lights its cap. The digi wheel lightens by 9. Drop-down arrows and the echo panel's menu arrow do
  not fade: they turn from pointing right (4 x 9 px, #686C6E on a drop-down, #C1C7CB on the echo arrow) to pointing
  down (7 x 4 px), the drop-down's brightening to #C1C7CB as it turns, and turn back the same way (timing below); a
  drop-down's arrow also points down while its list is open. The envelope panel's arrow is a 20 x 20 px button (canvas
  rows 92 to 111): at rest its 4.4 x 8 px triangle sits a pixel right of the cell's middle; under the mouse an amber
  frame (2 px, radius 5, #FCBA40 with a dimmer inner pixel) appears and the triangle turns down (8 x 4.4 px), centred
  and a pixel lower, and turns amber. A push button (Cut self) darkens the moment it is pressed (frame #14 to #10,
  fill #1C to #1A, the label a pixel down) and recovers the moment it is released, with no fade; a toggled Cut self
  looks exactly like an untoggled one (0 pixels differ). The keyboard scrolls 22 px per wheel notch and glides there
  with a 42 ms time constant (a third of the remaining distance per 60 Hz frame). START and END keep their look
  without a sample (0 pixels differ); only their drag is refused. CLIP lit is the large switch's lit look at 11 px:
  #EC744D in the middle, a cream specular on the top row, a lighter foot.
- **Gestures** (measured in FL with the mouse driven and Copy value read back): pressing, dragging and releasing a
  knob, the filter knob or a slider change nothing beyond the hover highlight (the body luma is the same pressed as
  hovered, and stays lit while the button is held outside the control, the cursor coming back to it on release); the
  filter knob's highlight fades out as an exponential with a 218 ms time constant (rest 51.5, fitted over 660 ms, rms
  0.9; a straight 300 ms fade misses by 4.3), its fade-in with a 72 ms time constant like the small knobs'. The
  slider's highlight covers the whole control: hovered anywhere, its groove grows a pixel up and down (edge #171A1A at
  rows 16 and 21 instead of 17 and 20 of the capture, the floor #1D2020 between), the tick band's top row (#202424)
  goes and the ball's rims lift. Drag: FEED moves 50 / 256 for 50 px, 12.5 / 256 with Ctrl (a quarter), the same as
  plain with Shift; Alt+click resets to the default (FEED to 0, the fine tune slider to 0.5); a double-click changes
  nothing; a wheel notch is worth 8 px of drag on every control (FEED +8/256, START OFFSET +8/2048, the fine tune
  slider 8/400) and half that with Ctrl, and runs the other way on the horizontal slider (a notch forward moved its
  ball left). A press on a slider's track walks the ball towards the mouse: 1.5 thumb pixels for a 60 ms click, 16 for
  a second held (a pixel at once, then about 15 px a second). FL's switches (Use loop points, CLIP) toggle the moment
  the mouse goes down and their light crossfades: on over about 180 ms, off over about 235 ms, both close to straight
  lines (the lit frame matched a blend of the two end frames within 1.5 luma); nothing changes under the mouse, the
  label never changes. The drop-down arrow is a crisp 4 x 7 px triangle whose base is 9 px from the field's right edge
  on the middle row (all four lists; the open one 7 x 4 px, a pixel lower); the fields' right edges are at x + w of
  their form rectangles (the filter list a pixel higher and 89 px wide, the chord list 107 px); the echo arrow is 3.4
  x 8 px with its base at x 326, rows 167 to 174. The effects view's icons are #FFC742 chosen and #D4DBDF not. FL's
  Edit menu names the steps ("Undo knob tweak (Delay feedback)  Ctrl+Z", "Redo ... Ctrl+Alt+Z") and Ctrl+Z steps back
  through knob tweaks one by one (a drag, then the Alt+click before it). The A / B pill's letters are 7-row caps (rows
  250-256 and 265-271 of the fx capture): a 10.3 px font on baselines 12 and 27 (baseline 28 puts the B's foot a row
  below the control).
- **Arrow rotation and wheel easing** (FL, frames every 8 to 16 ms, angles fitted by matching the rest arrow rotated
  in 2.5 degree steps): the list and menu arrows rotate rigidly a quarter turn clockwise at a steady rate with no
  wait: the Filter list's in about 155 ms and back in 205 ms, the echo arrow's in 150 and back in 185, the envelope
  arrow's in 170 and back in 210 (its amber frame fading with it, in about 175 ms and out in 235); used: 160 ms in,
  200 ms back. While a panel's menu is open its arrow stays turned and lit whatever the mouse does (the envelope
  arrow's frames matched its hovered frame for the whole 1.6 s the menu was open, the mouse having left after 1.7 s);
  once the menu closes it turns back exactly as on a plain leave (the frames after the close matched the hover-out
  frames one for one). A wheel notch moves the value at once and the knob eases to it: the pointer's centroid followed
  an exponential with a time constant of 36 ms on MOD X (30 to 42 ms on ATT, both directions); the fine tune slider's
  2 px move landed in two 1 px steps about 60 ms apart, the same easing; a one-step 30 px drag moved ATT in the next
  frame (no easing). FL Studio passes a VST3 plugin's keys through IPlugView::onKeyDown (logged: Ctrl+Z as key 26,
  Ctrl+Y as 25, with the command modifier) and runs its own shortcut when the view declines; when the plugin's window
  has the focus it can also hand the same press to the window, so the editor acts once per press. FL's own undo
  records only automatable parameters (a drag on a parameter that is not automatable leaves FL's Edit menu at a greyed
  Undo, and FL closes without asking to save; below, FL 26.1.7).
- **The filter knob turning, and the envelope's tension knobs:** recorded while MOD X was dragged through 97 degrees
  (frames every 16 ms). The metal cap and the shadow at the knob's foot stay pixel for pixel; under the mouse the dark
  collar round the cap has five lobes, and its outline turns with the value: matched against the first frame, the
  outline had turned by 5, 28, 53, 76 and 96 degrees (modulo the 72 degree lobe spacing) where the dot had turned by
  4.9, 29.1, 53.3, 76.2 and 96.6. Unhovered, FL's collar is round, so nothing turns. Ours grows a notched collar of
  its own from the cap under the mouse (see Filter knobs above), turning with the value. The envelope's tension knobs
  have no value dot (no green pixel at value 0) and draw their arc from the top whatever their default (the volume
  release tension, -79 %, from 12 o'clock round to the left); their TENSION captions are letter-spaced (37 px of ink
  against 32 set solid; ours 35).
- **Keys in FL Studio** (logged in FL with the key calls and the window's key messages written to a file): with FL's
  window for the plugin focused (after a click on its title), FL offers each key to the VST3 view (space: key 32) and
  plays when the plugin declines it. A click on a knob moves the Windows focus to JUCE's window inside FL's panel; FL
  then still offers the key and the plugin declines it, but FL does nothing, and JUCE's repost of the key to FL's
  panel is ignored too: the space bar neither plays nor stops, after a release or while a knob is held. Handing the
  focus back to FL's titled window after each click (and four times a second when no value box, menu or dialog needs
  it) restores it in every case tested: after a release, while held, after typing a value, with Ctrl+Z / Ctrl+Y still
  reaching the plugin, the right-click menu still opening and a typed value arriving once.
- **FL's project history** (FL 26.1.7, Current project > History in the browser): a VST3 plugin's change is an entry
  "Knob tweak (<the parameter's name, lowercased>)" only when the parameter is automatable (a PITCH wheel notch left
  nothing, an OUT notch one entry). FL records every change the plugin reports on the message thread, inside a begin /
  end edit or not (a sample loaded set the root note and loop points without one: four entries), one entry per
  parameter even when the edits overlap (a preset whose eight changes were all begun before any ended: eight entries).
  Changes reported from another thread (JUCE sends them with the next audio block) are not recorded. A parameter
  renamed by the plugin (a title change the host is told of) is recorded under its new name, also when it is changed
  right after the rename. FL's undo sets the parameter back from the message thread, its redo is Ctrl+Alt+Z (Ctrl+Y
  does nothing). Chan Sampler follows this: every parameter is automatable, and a group edit (a preset, a sample
  loaded or removed, a reset) sends its values from another thread and then steps one renamed parameter ("Knob tweak
  (preset: chop)"); FL's undo and redo of that entry bring the values and the sample back (checked in FL: remove the
  sample, undo, the sample is back; a preset undone and redone).
- **Not measured:** the preset arrows' click flash (white, fading over a quarter second; the preset bar is Chan
  Sampler's own control); the digi wheels' Alt+click and double-click; the A / B pill's press look.
- **Not reproducible:** ClearType text (its hinting also narrows FL's digi digits); FL's hint bar (its tooltips live
  in the main window), so Chan Sampler keeps its own tooltips; the lit state of Cut self was not captured (the click
  did not register in FL).

## Type in value (FL GUI, measured)

Every control's Type in value box in FL 2026 was opened, captured in both modes and fed test values (Copy value and the
hint read back):

- **The box:** a 244 x 51 px panel that opens where its menu entry was clicked (the mouse ends on its A button): a 1
  px #131516 edge round #1B1D1E; a text field with the value selected; at the right the mode buttons, 19 x 17 and 17 x
  15 px, the chosen one with a brighter frame (#333536 / #222425, the other #242627 / #1E2021); below, a tick, a cross
  and "Value for" the control's name in #5F6162 (long names cut with an ellipsis). It always opens in the control's
  own unit (a choice of A is not remembered); Return or the tick applies, the cross cancels, and a click elsewhere
  leaves it open. In FL, Escape closes the plug-in's window.
- **The modes:** the control's own unit, then A, the 0..1 position printed with up to 15 significant digits
  (0.780307471752167). dB: channel VOL and Levels VOL ("-5.2dB", "0.0dB"); %: the envelope and LFO knobs, START
  OFFSET, the loop START and END (as a percentage of the knob, though their hint shows frames), SMP START, LENGTH, IN,
  OUT, CROSSFADE, TRIM, the filter's MOD X / Y, the arpeggiator's GATE, the echo's FEED; C: channel PAN, the Levels
  PAN / MOD X / MOD Y, the tracking PAN / MOD X / MOD Y and the echo's PAN / MOD X / MOD Y ("Centered", "0%"); P:
  channel PITCH, the stretch PITCH, the echo's PITCH ("0 cents"); s: the arpeggiator's and the echo's TIME ("1:00"); V
  only: the digit wheels (pitch RANGE, the arpeggiator's Range, Echoes). No buttons, the position only: MUL, SLIDE,
  Fine tune. No Type in value: the stretch TIME and Max polyphony.
- **Reading typed text:** dB: a number is dB with or without "dB" (VOL: -6 is 75 % at -6.0dB, 0 is 100 %, +6 is 127 %;
  channel VOL tops out at 0 dB); a "%" there reads as silence in FL (Chan Sampler reads the knob's own percent). %:
  from 0 to 1 a fraction ("0.5" and "1" are 50 % and 100 %), above a percentage ("2" is 2 %, "25.5" is 26 %), "%"
  optional. C: whole percents, minus or "left" for the left ("50" is 50% right, "0.5" centred); "L50" is not
  understood. P: cents ("1" is +1 cent). s: steps:ticks, a bare number counts ticks ("2" is 0:02). A: the position,
  "50%" is half. On a % knob A shows the percentage shown read back as a position (attack 31% is 0.31, sustain 39%
  0.39, release tension -79% 0.105), elsewhere the position in full (SLIDE 0.301204819277108, arpeggio TIME
  0.575727181544634).

## Sample view and tracking defaults (FL GUI, measured)

- Sample view (measured on FL captures of a mono and a stereo file): an 8 px strip (#202324) above a 108 px view
  (#1B1D1E); grid lines darker than the background, #131516 every 0.1 s of the sample and #090B0C every second (the
  same rule at 363 and 272 px per second), plus lines at +/-50% of full scale; one waveform lane, linear and full
  height (peak 0.774 = 42 of 53.5 px), white #F6FEFF for mono files and #F2EDED for stereo; grey "Sampler" watermark.
  Bar below: #101213 / #313334 edges, track #282A2B, grey overview #A3A5A6 with 14 px arrow buttons. The grid step is
  the finest at least 15 px apart among powers of ten and, from 0.1 s up, 2 and 5 times them (FL: 0.1 s at 27 and 36
  px rather than 0.05 s at 18 px, 0.2 s where 0.1 s would be 9 px), and the next power of ten is darker (#090B0C) with
  a 1 px #17191A edge each side. Zoomed in (under 100 frames per pixel) FL shows a clean anti-aliased line through the
  wave instead of peaks; it is a line through each column's lowest and highest sample in the order they come (FL's 808
  CH hat, 0.1 s at about 8 frames per pixel: full height and spiky, the line's span per column within 1.8 px of FL's
  capture against 7.9 for a line through averages, which draws its 0.95 peak at a fifth of the height; a smooth wave
  still draws as a clean line). Grid lines never come finer than 0.1 s (the 0.1 s hat, whole and zoomed in to a third,
  shows only its 0 and 0.1 s lines); the scroll bar shows the overview only inside the visible window and hatches the
  rest. The badge at the bottom right is FL's sample format: 16 for 16-bit PCM, 32 for everything it holds as float
  (24 / 32-bit, compressed, compressed inside a WAV), with an icon of one notch for mono and two for stereo (measured
  with 16-bit mono, 24-bit stereo, compressed mono and 32-bit stereo files); it has no function of its own.
- A fresh Sampler (Add > Sampler on FL's channel rack; FL's Plugin database Sampler.fst and the Basic template store
  the same): SLIDE raw 500 (Portamento time 0:03, Copy value 0.301204819277108), envelope and LFO times raw 20000
  (shown as 31%), decay raw 30000 (46%), sustain 50 / 128 (39%), release tension -101 (-79%), as Chan Sampler's
  defaults.
- Envelope and LFO time percentages: FL's position (Copy value) is (raw - 100) / 65436, but its hint and Type in value
  show raw / 65536 as a whole percentage (a project with raws 16193, 20000, 51018, 53784, 54005, 54279 and 54722 on
  the predelay, attack, hold, decay, release and LFO times: 25, 31, 78, 82, 82, 83 and 83 %, which only raw / 65536
  rounded gives: (raw - 100) / 65436 shows 30 % for 20000, raw / 65535 84 % for 54722, raw / 65436 83 % for 54005 and
  rounding up 83 % for 53784; Copy value 0.778134359978139 for 51018). A typed percentage is still a position (50
  gives Copy value 0.5).
- Tracking: FL opens on the Key tab (Mid C5, Copy value 0.5); the velocity Mid default is 78% (0.78125) in a fresh FL
  Sampler too. The key Mid range is C0..C10 (Copy value = note / 120: G#7 = 0.76667).

## Sample view menu and markers (FL GUI, observed)

FL's sample view menu: Reload, Save as..., Locate file, Copy / Paste audio, Edit in audio editor / pitch corrector /
time warper, Open in new Sampler / Granulizer / Slicer / Slicex channel, Detect tempo, Get properties, Spectrum view,
Multichannel waveform view. Chan Sampler has FL's entries in FL's order (FL GUI): Reload (settings kept), Save as...
(the processed sample, 24-bit WAV), Locate file, Copy / Paste (FL: "Copy audio to clipboard" / "Paste audio from
clipboard", shortened in Chan Sampler; the file and its audio as WAV data; pasted data is kept as a WAV in
Documents/Chan Sampler/Pasted audio), Detect tempo, Get properties, Spectrum view, Multichannel view (greyed only while
the spectrum is on, as in FL) and Colorful waveforms; the audio editor (Edison), pitch corrector, time warper and
new-channel entries are FL's own. FL shows Ctrl+C / Ctrl+V beside Copy / Paste (italic, #A0A5A4) and underlines R, S, C,
P, E, D and G (Reload, Save as, Copy, Paste, Edit in audio editor, Detect tempo, Get properties); Chan Sampler shows the
same, answers to those letters while the menu is open (the knob menus' letters too) and to Ctrl+C / Ctrl+V (Cmd on
macOS) while its window is active. Ctrl+E is FL's audio editor. Dragging the waveform out of the view drags the
sample's file (to a DAW, an editor or a folder); when that file changes on disk the sample reloads, settings kept.

- Detect tempo (FL GUI): FL's "Tempo detection" dialog offers the estimate, the ranges 50 to 100, 75 to 150, 100 to
  200 and 150 to 300 BPM, Type in (BPM), Type in (beats), the project tempo and No tempo. The choice is kept as the
  sample's tempo: the view and TIME stay as they were (FL then offers to set the project tempo, which a plugin cannot
  do). TIME > Autodetect... opens the same dialog and sets TIME to the sample's length in beats at that tempo: a 2-bar
  loop at 128 BPM gives 2:00:00, and FL's TIME hint reads "Clip tempo is 128 BPM". A range gives the best tempo inside
  it (FL: 128 for the loop, 63.998 for 50 to 100). Chan Sampler's detector autocorrelates the onset strength (spectral
  flux) and prefers tempos that make a loop a whole number of beats (4, 8, 16...): the test loop gives 128 / 64 / 128
  / 256 for the four ranges, and FL's 20 legacy drum loops all give 120 (60 / 240 for the half and double ranges;
  their ACID chunks say 4 beats at 60 BPM, which is used as the starting tempo).
- Panel menus (FL GUI): the echo delay's ▸ menu has Reset, Echo - classic, Echo - ping pong, Fat - detuned, Fat - more
  detuned, Fat - detuned + time offset and Fat - 8 voices; their values read with Copy value (feed 0.25 / 0.875, pan,
  pitch +2 / +3 / +6 cents, time 3 steps or 0 or one 48th of a step, 4 or 2 echoes, Ping pong / Fat mode) are rebuilt
  exactly. The envelope's ▸ menu ("Channel envelope presets") holds no built-in presets in FL: only Save as... and the
  saved ones, as here (Documents/Chan Sampler/Envelopes, the target page's envelope and LFO). Loaded region / slice
  markers show as 4 px orange (#FFB871) tabs at the top of the view, as FL draws them.
- Root note on loading a sample (FL GUI: files loaded into FL's Sampler by drag and by its Open button, then its Root
  note read): the WAV's "inst" chunk note wins (a tenor sax with inst 37 / smpl 25 gets C#3), else the "smpl" MIDI
  unity note (BASS_EfEm_C2, 48: C4; CHR_Aah_A3, 69: A5, not the A3 of its name), else C5: files that store note x 256
  there (CHR_Gospel_C3 15360, DNC_Kick_4 9216) get C5, and a file without either resets the root to C5. Loading a
  project keeps the project's root (FL's renders with the file's root ignored match ours exactly). 703 of the 3150
  WAVs in FL's packs store a usable root, 485 of them not C5.
- TIME knob (FL GUI, its hint read every 2 px of a drag up to 240 px and back): one 16th step per pixel, whole steps
  only (bars:steps:ticks with ticks 00), and the 20 px hold of FL's detents at every bar: 0:15:00 at 15 px, 1:00:00
  from 16 to 36 px, 1:02:00 at 38, 2:00:00 from 52 to 72, 3:00:00 at 88, 7:00:00 at 232; the same positions on the way
  back, 0 below the start. FL's renders follow TIME the same way in Auto as in Resample and Stretch (a 2 s sine with
  TIME 1 beat at 130 BPM plays 0.47 s; Auto keeps its pitch, Resample does not). FL's hint adds "Unrealistic tempo"
  when TIME makes the sample's tempo implausible.
- Stretched view (FL GUI, a drum loop in 20 projects, view pixels compared with the plain file): in Resample, Stretch
  and Stretch Pro FL draws the file as it is whatever PITCH (+1200, -700), MUL (x2) and TIME say (0 pixels differ); in
  e3 generic, e3 mono, Slice stretch, Slice map, Auto and the four e2 modes it draws the sample as it plays (4200-6150
  pixels differ at MUL x2, also with PITCH alone). Chan Sampler follows the same rule. The badge reads 32 over a
  stretched copy (FL holds it as float) and the file's own depth otherwise (BASS_EfEm_C2 in e3 generic: 16 at TIME 0,
  32 at 1, 2 and 4 beats). In e3 generic FL's TIME gives the same lengths as ours (a sine: 0.124 / 0.470 / 0.932 /
  1.855 / 3.699 s at 0.25 ... 8 beats, ours within 1 ms) and its loop repeats every (END - START + 1) x the stretch
  (BASS_EfEm_C2: 653 / 1305 / 2611 / 5221 samples at TIME 0.5 / 1 / 2 / 4, as ours); the stretched waveform itself is
  elastique's (at TIME 4 FL's starts 5 dB quieter and swells a third of the way in).
- Menu look (measured in FL): #515456 background, 19 px rows, text 16 px in, disabled entries grey (#818789), 7 px
  separators with a 1 px #2F3336 rule, a 9 x 7 px tick in the left margin. The preset menu uses the same look.
- Get properties: FL opens the system's own file Properties window (Explorer's General / Security / Details sheet).
  Chan Sampler shows the facts in an FL-styled sheet instead: name, location, format (container, bit depth, or the
  codec inside a compressed WAV), size, date, sample rate, channels, length, the bit depth FL holds it at, embedded
  loop points and markers.
- Spectrum view (FL captures of a 6 dB ladder of a 3 kHz tone, a 20 Hz-step sweep around 3 kHz, clicks, white noise
  and tones): black, frequency linear from 0 at the bottom to half the sample rate at the top. Each pixel row shows
  the loudest frequency between (row - 1) and row times Nyquist / (rows - 1) (the row changes at 3070 and 3275 Hz on a
  109-row view). A click is 20 px wide at 16.2 samples per px, the shape of a 328-sample Blackman-Harris window (0.5
  dB fit; a Hann or Gaussian does not fit), and its level and the noise level are those of that window to 0.4 dB; yet
  a steady tone stays two rows thick with black (-110 dB) next to it, which no single window gives. Chan Sampler
  multiplies the short window's spectrum by a long (Gaussian, sigma 152 samples) spectrum divided by its local peak
  over +/-32 bins (4096-point FFT): mean error 0.2-1.0 dB per pixel over the four captures, noise -25.1 dB against
  FL's -25.3. Levels rise 3 dB per octave (0 at 3 kHz; click and noise rows agree to 0.5 dB), and the palette is FL's
  by level from the ladder: black at -112 dB, deep blue at -100, purple at -84, red from -66, orange -36, yellow -24,
  white towards 0 dB. FL keeps the "Sampler" watermark over it.
- Colorful waveforms (FL Options > General settings: Off / Color map / Multiband; FL's own default is Off, Chan
  Sampler sets it per plugin from the sample menu, Multiband by default). Measured with 20 tones from 30 Hz to 20 kHz,
  four two-tone mixtures, a level ladder, a synthetic test (tone switches, a 100 dB decay, tones with weak noise, a
  -70 dB tone, silences) and FL's own 808 kick, 707 snare, 808 open hat, a tom, a stereo drum loop and a vocal, all in
  both modes. The level does not change the colour. Bands: first-order filters in a tree, low = low-pass 168 Hz, the
  rest (high-pass 152 Hz) split at 1284 Hz into mid and high (1.2 RGB steps on the tones and mixtures). Time: FL's
  colours change before a switch and fade out after it, so each band's energy is taken in blocks that follow a rise at
  once and a fall smoothly, and a pixel reads the level at its centre (a quarter block late, interpolated). Multiband:
  32-frame blocks, 700-frame fall (a 100 Hz to 6.4 kHz switch fits to 1.6 steps; FL's own 707 Kick, 909 Kick, 707
  Snare, 808 CH, 707 Tamb and Alma Kick, 8-50 frames per pixel, start blue / teal in FL and only 32-frame blocks show
  it, 707 Kick's first 40 columns 8.0 RGB steps). A pitched view in a precomputed mode is coloured from the stretched
  copy, as FL's is (Auto: 707 Kick an octave down starts teal, not blue); Color map is much faster (64 frames,
  100-frame fall), which is why FL's Color map flickers on low notes and stripes a vocal while Multiband stays steady.
  Silence keeps the last colour (the tom's tail stays red). Stereo samples use the mean of both channels' band
  energies (a stereo loop: 6.3 to 3.8 steps). Mono samples are drawn 5% brighter (R + G + B = 257 instead of 245; FL's
  mono wave colour is brighter too). Multiband: RGB in proportion to the band RMS. Color map: (0.4 mid + high) /
  (low + mid + high) on FL's palette (red-pink, orange, cream, green, teal, blue, violet). Result against the captures:
  Multiband 0.4-2.5 RGB steps on solid fills (hat 0.4, vocal 1.1, t1 2.1); Color map matches FL's colours and flicker
  character, single flickering columns cannot match. The scroll-bar overview is coloured the same way.
- File buttons (measured in FL): 32 / 19 / 19 x 23 px boxes, a #141717 frame, a lighter inner edge and a fill
  darkening downwards; 1 px line icons: a 12 x 10 px folder, a 9 x 8 px folder over a small tree (locate), a 9 x 9 px
  cross.
- FL's message dialogs (captured): #202324 window with a 25 px title bar, the text on a #282C2D panel with a large
  grey sign, OK buttons with a dark frame and a darkening fill. Chan Sampler shows its messages (preset import,
  unreadable file) the same way inside the plugin window.

## Playback markers and loop knobs (FL GUI, measured)

FL defaults: Use loop points off in a new Sampler channel (on in the channels of FL's Basic project template, which the
null-test projects start from), loop START = END = 0 (0 = no loop); the sample view shows no markers. The Start marker
appears once the start offset is above 0; the loop once END is above 0 (START = END is drawn as one line). START moves
alone while below END and pushes END to START + 1 sample when it reaches it; END moves alone but stops at START. FL draws
the loop red even with Use loop points off; Chan Sampler draws an unused loop grey. Loading a new sample resets start
offset and loop points to 0 unless the file has an embedded loop, which sets the loop markers; Use loop points is not
touched (FL leaves it as it was). FL's linked loop START / END apply to edits in the editor only (knobs, menu, sample
view); host automation sets the values it shows, and the engine treats an END below START as START. In the stretch
modes the stretcher's read position folds into the loop on every pass, forward and ping-pong.

## Host features and robustness (tests)

- `ChanSamplerHostTest <Chan Sampler.vst3> nulltest/signals/sine.wav tests/data/state-*.bin` (paths in this folder) (it expects that 2 s
  sine) hosts the built VST3 through JUCE's VST3 hosting, as a DAW does: 162 automatable parameters, every one of Chan
  Sampler's (plus JUCE's MIDI-CC pass-through parameters); volume automation mid-note; pitch bend; MIDI CCs 1, 7, 10
  and 11 leave the sound alone (map them to parameters in the host) and the sustain pedal (CC64) holds note-offs (FL
  handles the pedal itself); save / reload into a new instance restores every parameter (host and plugin side), the
  sample and the editor size, and plays bit-identically; host rate changes keep the pitch; offline rendering right
  after a change or a project load uses the new sample; a stretch mode chosen while playing plays stretched; a project
  without its sample plays nothing and keeps the path; projects saved by earlier releases load with every setting
  unchanged. All pass; ctest runs it.
- `CsRender statetest <sample>`: in-process round trip of all 160 parameters, sample and window width, bit-identical audio.
- `nulltest/check_robust.py`: 44.1 / 48 / 96 kHz hosts with stereo float, mono 16-bit, stereo 24-bit and 48 kHz
  files all play at the right pitch and length; a 10-minute file loads and plays in 0.4 s.
- Memory (one instance, peak private bytes): about 4 MB in Resample; about 130 MB once a stretch mode is used, almost
  all of it the 64 voices' time stretchers (plus 16 MB of e3 / e2 loop takes), which are made the first time a stretch
  mode is needed.
- The editor scales as vectors (Retina / high-DPI sharp; the waveform is computed at the screen's physical resolution).

## CPU and performance

`CsRender bench`, one core, % of real time, 32 / 64 looping voices (the bench's notes run from C2 up to B5, and
a stretcher's work grows with the note's pitch ratio):

| Mode | 32 / 64 voices |
|---|---|
| Resample | 14 / 35 |
| Stretch | 18 / 42 |
| Stretch Pro | 25 / 53 (28 / 64 with Stretch's vocoder, measured in a run where Stretch took 21 / 48) |
| e3 generic and Auto | 16 / 36 (vocoder below 1.485 kHz, phase work on those bins only, + high-band grains; 37 / 83 in 1.1.0) |
| e2 generic | 16 / 36 |
| e2 transient | 19 / 43 |
| e3 mono, e2 mono | 6 / 12 |
| e2 speech | 5 / 11 |
| slice stretch | 10 / 27 |
| slice map | 12 / 23 |

Automating cutoff, resonance, pan, volume and fine tune every block adds 0.3 to 0.4% (16 voices: 5.1 → 5.5% Resample,
6.1 → 6.4% Stretch Pro).

| Optimisation in use | Measured effect |
|---|---|
| Own FFT (Source/dsp/FastFFT.h, radix-4 Stockham on split arrays with SIMD butterflies, twiddle tables shared between voices; JUCE's results to -133 dB, 3-3.6x faster than JUCE's fallback FFT at 256..8192 points); the high band's filter skips blocks of digital silence | e3 generic 29 / 67 → 16 / 36 |
| Phase vocoder packs both channels into one complex FFT each way, keeps phases as unit phasors so only spectral peaks need an angle, rebuilds Stretch Pro's formant envelope (64 cepstral coefficients) with a quarter-size FFT | Stretch Pro 39 / 118 → 19 / 54, the other vocoder modes 26 / 77 → 14 / 40; output within -95 dB of the accumulator form at a note's start, which loses precision as notes go on (-41 dB after 3.6 s), the phasors do not |
| Interpolator tap sines from one rotating phasor (the two taps next to the read position keep exact sines, as the phasor's 1e-15 would be amplified there) | Resample -27%; output within -326 dB, the offline FL nulls unchanged at 65 NULL / 17 close |
| Channel 1's spectral work copied from channel 0 when both read the same (bit-identical) | Slice map -29%, Stretch / Stretch Pro / e2 transient -22 to -25% on mono samples; no change on true stereo |
| The audio thread reads the parameters through pointers looked up once (no juce::String per envelope parameter each block) | part of the rows above |
| Attack-detection block energies summed only when attack detection runs, the blocks side by side (each still in sample order); branch-free peak test in the phase locking (noise spectra mispredicted it); found with line-level profiles (xperf sample addresses through llvm-symbolizer), bit-identical output (the 81-case audio battery unchanged) | Stretch −9% (stereo noise) / −10% (mono), e2 transient −9% / −9%, Stretch Pro −6% / −7%, e2 generic −4%, e3 generic −3% / −5% |

Method: alternating old / new builds, best of runs, 32 voices. What remains is FFTs, atan2 per peak and, in Stretch Pro,
a log and an exp per bin for the formants, which cannot be cut without changing the sound.

## Declined changes

Changes measured and not kept, and earlier models the laws above replace, with the measurement that decided.

| Change or earlier model | Measured effect |
|---|---|
| Length as ceil(fraction · what remains) | a frame long in two thirds of the 100 in-FL trims, which made the tail look content-dependent |
| Short-trim end filter fitted on 100 frames alone, rate scaled by the window | drifted to −53 dB on the shortest segments; segments under 200 frames −47…−57 dB in FL before the seven-length fit |
| Reversed trims with the end table a frame late | −46…−64 dB (now −65…−85) |
| Arpeggiator TIME as round(3.88852·25^p − 1.08184) | a tick out at 13 of the 55 measured raws |
| Past the sample end without interpolation at the advancing position | noise case −37 dB (now −72) |
| Envelope time table read at 130 BPM | 2.6 to 6.8 ms long (half to one and a half ticks) |
| Decay stopping before the full 1 → 0 curve | positive decay tension −13…−30 dB (now −55…−74) |
| Filter f0 top point 20058 Hz | wrong: FL's cap is 3.0 rad/sample (21056 Hz) |
| Control blocks aligned to the host buffer | a transition moved by up to one block with the note's place in the buffer (hold 5000: −57 dB, now −68) |
| Filter modulation without per-tick evaluation | −14…−52 dB (now −59…−72) |
| LPx2 compensation from a table | worst in-FL case −44 dB (now −47); between the table's points 5 to 9 dB worse; at maximum resonance without the per-tick slew −32 / −45 dB (now −59.5 / −59.3) |
| A stretch range clamped to x20 | past x20 the source read too fast and ended early (a 1 s sine at x24 fell silent at 20.3 s; BASS_EfEm_C2 at about x25 left the last fifth of the view flat) |
| 1:1 frames before an attack reaching back only one frame | attacks early from x4 on (e3 generic / Stretch / Stretch Pro -19 / -25 / -35 ms at x4, -67 / -76 / -131 ms at x6) |
| Fitting Stretch / Stretch Pro to FL: 168 FL renders (707 kick / snare / open hat, DL Groover, DL Ninety, a sung "ah", HIP_Bass, e3_loop; MUL x0.5 ... x4, PITCH +/-7 and +/-12 semitones, MUL x2 + 7 semitones), our stretcher alone on FL's decode of each, scored by envelope + band-spectrum + fine-spectrum distance, coordinate search over frame size, transient threshold, overlap, the hop rule, the start / attack gains, attack holding, e3's high-band grains and (Stretch Pro) the formant lifter, correction limit and how far the envelope follows the pitch, DL Ninety and the snare held out | the settings in use were already at the optimum: the best candidates gained 0.5% (Stretch) and 1.7% (Stretch Pro, its envelope following the pitch by a quarter) and made the held-out sources worse. A 4096-point frame trades envelope (2.15 -> 3.13 dB) for spectrum (fine 5.62 -> 4.94); e3's high-band grains lose in both modes. The remaining distance is the engine itself (FL's Stretch and Stretch Pro are zplane's elastique; FL's credits and changelog) |
| e3's grains in Stretch Pro (FL's Stretch Pro has e3's split) | spectrum 2.16 -> 2.26 over the MUL cases; PITCH cases worse (the grains do not keep the formants) |
| Slice starts from 512-sample blocks | up to 415 samples early, and a slice in the "ah" at 2048; Slice stretch envelope 2.39, spectrum 3.47, null -1.77 dB; Slice map mean -11.8 dB (the "ah" +1.4, e3_loop at x2 +2.0); e2 transient spectrum 3.69 |
| Slice map with a 2 ms fade-in and no high-pass end fade | Slice map mean -26.0 dB (now -58.5 with the edges, -61.4 with the cuts); the "ah" -71 dB and e3_loop at x2 -11.3 with the starts alone |
| Cut slices without the fade under the next slice | at x0.667 e3_loop -33.6 dB, DL Groover -52.0 |
| The 942-sample end fade timed to the play head | worse: -23.8 / -43.3 dB |
| Slice starts from the sign change of L + R | wrong in 8 of 16 (the louder channel's crossing further back) |
| e2 speech with a 384 fallback where no period is found | envelope / spectrum 1.88 / 2.70 (now 1.89 / 2.45) |
| Mono modes' grains on the waveform's peaks, crossing over at 0.50 | envelope / spectrum changed by less than 0.1 dB (mixed) and a slow swell started up to 48 samples away from FL's; waits for the soloist's mark placement to be measured |
| Mono modes with the longer period range and the last period held | e3 mono spectrum 3.83 -> 3.51 but envelope 2.16 -> 2.32, e2 mono 2.06 / 5.10 -> 2.51 / 5.17 (the kick and DL Groover worse, e3_loop better); waits for FL's trackers |
| A longer frame for e2 transient, a quarter or an eighth of it as the hop | envelope 2.42 -> 3.16 / 3.02 dB (attacks smear) |
| Stretch Pro with e3's split and grains, each grain read at 1 / PITCH so that the output's resampling leaves its spectral envelope in place | the 20 cases' band spectrum 3.77 -> 4.05 dB (a voice closer at both pitches, a kick at -700 cents further); both versions miss FL's Rhodes with PITCH by 9-13 dB, which neither models |
| Polynomial atan2 / sincos in the vocoder | no faster than MSVC's, output -41 dB away |
| A leaner read callback; interpolator tap checks | no gain |
| Deferring the non-peak bins' synthesis phasors until a bin becomes a peak | bit-identical, no faster |
| Spectrum view with a 2048-point FFT | about 2 dB dark on real sounds; 3.06 dB per block on average (now 2.37) |
| Colourful-waveform bands at 162 / 1312 Hz without the filter tree | too much high band in low notes |
| Multiband colour blocks of 512 frames (fitted on 2 s samples at 160 frames per pixel, where a drum's click is under a pixel) | 707 Kick's first 40 columns 38.6 RGB steps (now 8.0) |
| Redrawing every mode's stretched view (up to 1.2.1) | differs from FL in Resample, Stretch and Stretch Pro, which FL draws unstretched |
| SLIDE default raw 820 (0:12, up to 1.6.0) | FL's fresh Sampler stores raw 500 |

Measured and left as they are: Crossfade (our measured curves null, −77…−82 dB) and the end-of-sample fade (it nulls,
−75 dB). Open: e3 in the plugin as FL restarts it, the shortest trims (−48 dB), LENGTH 0 with a start (−56…−59 dB).

## Provenance

How each part of Chan Sampler was made (the release check requires this section):

- **Measured:** FL Studio's renders of its Sampler (null tests, impulse and click responses, sweeps and noise at many
  settings, in `nulltest/`), the values FL reports for each knob, and captures of its channel settings window
  (positions, sizes, colours, gradient directions, font metrics, timings, drag laws).
- **Fitted or identified from FL's output:** every law and constant in the tables above (the interpolation and
  resampling kernels, the envelope, LFO, filter, echo, reverb and declick laws, the trim filters), held as formulas and
  tables of our own in `Source/` and `measurements/ChanSampler/`. The time-stretch modes are our own algorithms, tuned against
  FL's renders.
- **Redrawn:** the editor, drawn in code from measured positions and sizes in Chan Sampler's own palette with our own
  shading; every icon is our own glyph. No table of FL's on-screen brightness and no outline traced from FL's screen
  is in the repository or its history.
- **Kept for compatibility:** the import of `.fst` presets from the musician's own library, which finds their samples
  in their own FL folders, and the arpeggiator's chord list in the order of FL's chord menu (standard chord names and
  intervals), so a preset's choice carries over.
- **Published on recorded approval:** the measured data in `measurements/ChanSampler/` (kernels, matrices, curves and
  tables measured or fitted from FL's output, with vendor names kept out of the file names; approval recorded
  2026-11-09). No render of FL's and no file FL wrote is among them.
- **Shipped on recorded approval:** none. The repository keeps one file of ours under `.provenance`: a project state
  saved by Chan Sampler 1.2.0, for the host test that old projects load unchanged.
- None of Image-Line's code, artwork, samples, preset files, fonts or other files ships with Chan Sampler or sits in
  this repository. Files FL writes (projects, renders) stay on the developer's computer (`nulltest/local/`, ignored).
