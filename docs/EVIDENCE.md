# Evidence ledger: FL Studio Channel Sampler → Chan Sampler

Chan Sampler's behaviour is matched to FL Studio 2026 (26.1.7) by **null testing**. Each case is rendered twice, once by FL and once by Chan Sampler; one render is polarity-flipped and the two are summed. The residual shows how closely they match. No FL code, images or impulse responses are copied. Every law below is a formula or table fitted to FL's *output*.

Sources:
- **[M]** Image-Line's online manual: chansettings_sampler / _ins / _misc, plus screenshots for colours.
- **[B-FORM]** The layout of FL's channel-settings window. REA evidence `ev_1003b2d847ac512b3de6d6965dcedfd8f0e1f3089e99fe5f6f6d6471264aeff1` (`inspect_pe_resources` on `FLEngine_x64.dll`, SHA-256 `6d9c5ab0…8e02`). The RCDATA `TPLUGINFORM` Delphi form was decoded for control geometry, captions and option lists.
- **[PYFLP]** The open-source PyFLP project's tables of FLP event IDs and raw parameter ranges. Some offsets have moved in FL 2026; see "Format notes".
- **[NULL]** Runtime measurements and null tests, in `nulltest/`:
  1. `nulltest/lib/build_project.py` / `nulltest/lib/flrender.py` patch FL's Basic template and render it with `FL64.exe /R /Ewav`.
  2. `ChanSamplerRender.exe` renders the same case through the real plugin processor.
  3. `nulltest/lib/nulltest.py` aligns, gain-matches and subtracts the two renders.

## How to read the null figures

FL's command-line export is 16-bit, so the best possible residual is roughly −65 to −95 dB, depending on signal level:
- **NULL:** better than −60 dB, i.e. a match.
- **close:** −30 to −60 dB.
- **diff:** worse than −30 dB.

Test cases use FL velocity 128, which maps exactly to MIDI 127.

Current suite: **82 cases: 62 NULL, 14 close, 6 diff** (full table at the end).

## Measured laws

| Feature | FL behaviour (measured) | Evidence |
|---|---|---|
| Velocity | gain = (11^(v/100) − 1)/10, FL velocity v 0–128 (100 = unity, 128 = ×2.05). MIDI m → v = m·128/127 | NULL |
| Levels VOL / PAN | VOL = FL's channel volume: gain (11^x − 1)/10, x = raw/12800 (knob position raw/25600), default raw 10000 = 78% = 0.551. PAN raw 0..12800 (centre 6400), constant power with unity centre: L = √2·cos θ, R = √2·sin θ, θ = (pan+1)·π/4 (50% left = L ×1.307, R ×0.541). FL also applies its own channel levels to plugin channels, so in-FL tests leave Chan Sampler's FL channel at default | NULL −74…−84 dB (7 cases, in FL) |
| End of sample | Holds the last value and fades it out with a cosine over the declick out-time (10 ms for Out only) | NULL −75 dB |
| Boost | gain = 1 + 16·knob (raw/256), up to ×17; CLIP hard-clips at ±1 | NULL −76…−95 dB |
| EQ | y[n] = x[n] + 0.75·eq·y[n−1] (one-pole; right = more lows) | NULL −66…−69 dB |
| Fade IN / OUT | Linear slopes relative to sample length: IN = clamp(1 − (1−x)·4k), OUT = clamp(1 − x·4k) | NULL −67…−75 dB |
| Sample start / Length | Doubles at params+128 / +136. start = round(start·(N−1)); length = ceil(fraction · what remains) (floating point: 0.3·88200 rounds up to 26461); at least 40 frames (LENGTH 0 plays 40). **Reverse applies to the trimmed segment.** The trimmed start and end are linear filters, not gain fades (measured with clicks, superposition verified; nulltest/probes/tail_measure.py, nulltest/probes/head_measure.py, nulltest/probes/tail_short.py → Source/TrimTail.h): at the end each output keeps its own frame and subtracts a weighted average of the frames behind it, up to twice its distance from the end (low frequencies fade first, DC reaches 0 at the end); at the start each output mixes the input from n to about 3n over 22 frames. The shortest segment (LENGTH 0 = 40 frames) has its own measured table and carries two silent frames after it. Order: start filter, end filter, then Reverse. A segment of pure digital silence around the cut renders silent in FL START exactly 0.5 (a half-sample tie): the play position rounds the tie up but the length count rounds it down, so the segment runs one frame past the sample end (silence; the end then holds that silence) — modelled, NULL −69 dB with and without Reverse. Fades are applied before Reverse (measured) | Full length NULL −66…−90 dB at any start, with or without Reverse; trimmed: NULL/close −46…−64 dB (shorter segments weigh the tiny tail residual more) |
| Crossfade | Knob position = raw/1024; loop start = position/2 of the sample (FL truncates). Without loop points the sample loops from there to the end; the last stretch of that length blends linearly into the audio before the loop start | NULL −82…−84 dB (5 cases, in FL) |
| Output level | FL's plugin wrapper outputs twice what a default Sampler channel does, so a default Chan Sampler must output 0.5: VOL default 0.551 × OUT −0.844 dB | NULL −78.8 dB (defaults, in FL) |
| Arpeggiator chords | Auto, Auto (sustain), (none) plus FL's 66 chord presets (names and intervals as in FL's `System/Config/Chords.map`); presets build on the lowest held key | FL data file |
| Normalize, Reverse, Polarity, Swap, Fade stereo (cos/sin), Remove DC (mean) | Exactly as named | NULL −73…−82 dB |
| Trim | Cuts after the last frame whose louder channel exceeds raw/32768; at 0 nothing is cut (trailing digital silence is kept) | NULL −76 dB |
| Ring mod | ×((1−mix) + mix·m), m advances (raw+8)/5760 cycles/sample; **sin on L, cos on R**; a mono file stays mono, so sin on both | NULL −61…−74 dB (stereo, mono, 48 kHz) |
| Precomputed cut/res | y = y1 + f(x − y1) + r(y1 − y2), f = (601^u − 1)/600; r measured at raw resonance 0, 32 … 640 (cutoff open: y = x + r(y1 − y2)), cubic in log(1 − r) | NULL −70…−100 dB (all settings tested) |
| Stereo delay | Delays one channel by \|raw − 2048\| samples (≤ 2048); right = delays left; the sample grows | NULL −76 dB |
| Pogo | Length-preserving time warp: (1−\|a\|)x + \|a\|·w(x), w = sin(πx/2) or 1 − (2/π)asin(1−x); 4-point Hermite with silence outside the sample | NULL −75 dB (full range) |
| Playback interpolation | 30-tap windowed sinc, k(d) = c·sinc(c·d)·w(d), \|d\| < 15 source frames, c = min(1, 1/ratio); the window stays 15 frames wide; w = 7-term cosine series (nulltest/probes/kernel_extract.py, nulltest/probes/window_fit.py) | < 1 LSB vs 686k measured points; pitched notes NULL |
| Sample-rate conversion | Files at another rate are converted to the project rate on load with an 88-tap windowed sinc (\|d\| < 44, near-Hann 9-term window), then played through the 30-tap kernel | 1 LSB; 48 kHz mono sweep NULL |
| Envelope graph | Stage width = knob position × ⅓ of the view (decay × (1 − sustain)); no sustain plateau; release drawn from the sustain level at full width; delay is empty space; curves bend with each tension | matches FL screenshots (9 settings) |
| Arpeggiator | Parameters event: direction @40, range @44, chord @48 (−2 Auto, −1 Auto (sustain) = FL default, 0 (none), k = preset k−1), time @52, gate @56 (raw/48), slide @60, repeat @92. Step k starts at round(k·step) after the first. TIME position = (raw−450)/997, raw 1447 = Hold; FL plays whole ticks: round(3.88852·25^p − 1.08184) (77 raws measured). Arp notes end at the gate end or key release (cut with the declick fade when the volume envelope is off), unlike normal notes. Auto (sustain) holds a single key and restarts it on the next step once the sample has run out. Slide = legato: the sounding note takes the next pitch at the step, no glide; once it runs out later steps stay silent. FL sends All-notes-off right before the note-off | NULL −61…−83 dB (22 cases); one TIME value −46 dB (half-sample ties) |
| Stretch modes | FL keeps pitch and length independent in every stretch mode (key change keeps the length; MUL / TIME change the length, not the pitch; FL renders). Chan Sampler (TimeStretch.cpp, own code): pitch-synchronous grains for e3 / e2 mono and e2 speech, e3's vocoder (and e3's high-band grains for e2 generic) for Stretch / e2 generic / e2 transient, WSOLA for slice stretch, Stretch Pro = 4096-point vocoder with cepstral formant preservation and FORMANT shift (+/-1200 cents, centre = preserve). Other modes against FL (FL renders of clicks, noise and saws at MUL x1.5 / x0.667, and of a kick, a Rhodes, a voice and two drum loops at -700 / +700 cents and x1.5 / x0.667): FL's e3 mono and e2 mono are time-domain splicers (noise: the output is pieces of the source, a splice every ~900 / ~480 samples at x1.5, each jumping one estimated period, ~250 / ~140 samples on noise), clicks land on time (+/-20 samples) at full level, and PITCH keeps the spectral envelope (a voice's 99% roll-off stays at 12.7 kHz at -700 and +700 cents; e3 / e2 generic, e2 transient and Stretch move it with the pitch; e3 mono and e2 mono null at -13...-20 dB against each other on a Rhodes); e3 mono plays a note's first ~384 samples 1:1, e2 mono splices at once. e2 speech is time-domain with long splices (~1150 samples at x1.5). e2 generic's and Stretch Pro's high band is pieces of the source (noise correlation 0.8 at x1.5), Stretch's and e2 transient's spectral (0.2). FL's e2 generic / transient smear a click over ~-300 samples and run at a quantised ratio (x1.5 plays as 1024 / 683). Envelope / band-spectrum distance to FL over those 20 cases (dB, before -> after): e3 mono 3.34 / 6.38 -> 2.16 / 3.83, e2 mono 2.76 / 7.28 -> 2.06 / 5.10, e2 speech 2.33 / 3.25 -> 1.88 / 2.70, e2 transient 4.95 / 3.83 -> 2.45 / 3.07, Stretch 2.12 / 2.82 -> 1.77 / 2.04, e2 generic 2.57 / 2.87 -> 2.55 / 2.85 (e3 generic 1.73 / 1.99; Stretch Pro unchanged at 2.93 / 4.37). Elastique is proprietary, so these cannot null; log-spectrogram distance to FL's renders 7-10 dB. e3 pitch (nulltest/cases/e3_pitch_cases.py: FL's 707 Kick, a Rhodes, a voice, two drum loops, -1200 ... +1200 cents, in FL): FL's e3 on noise plays resampled stretches of the source with constant offsets for 400-700 samples, then jumps (a time-domain-like engine), and keeps a kick's envelope at any pitch. Ours: the vocoder's synthesis hop shrinks below +1 octave (0.5 x the analysis rate) and an attack is held 1:1 only up to the frame centred on it, so a kick an octave down no longer lasts twice as long; mean envelope distance 2.64 -> 2.01 dB, band-spectrum distance 2.61 -> 2.44 dB, raw null +0.0 -> -0.5 dB (sample-level nulls stay near 0 dB past the first attack). Attacks (e3 generic, 1 kHz clicks at -1200 / -600 / +600 / +1200 cents): FL sounds every attack at the same place, -6 / -1 / +4 / +5 samples from the source's (ours now the same within a sample, attack found to the sample, the 1:1 frames placed with a per-pitch shift table); each click nulls at -22…-43 dB after a level fit; a note's start is 1:1 from its first sample (in-FL +600 cents: -37 dB over the first 256 samples). Sustained tonal material stays uncorrelated (≈0 dB) | nulltest/stretch/stretch_check.py, nulltest/stretch/stretch_vs_fl.py, e3_clicks.wav |
| Envelope on the tick grid | FL works the envelope out once per song tick (96 per beat) from the note start and interpolates linearly; every FL render is piecewise linear on that grid to the 16-bit floor (nulltest/probes/env_tick_probe.py). Each stage lasts N = ceil(time / tick + ½) ticks (none for no time), counted from the note start for delay and attack; the hold's last tick is the decay's first (with no decay time, the last tick before the fall). From tick to tick the level moves at most the full range per 8.83 ms (389.5 samples): that is what smooths a jump and limits very short attacks (the old "424-sample ramps" and "19.23 ms attack floor" were this, seen at 130 BPM). The first tick holds the second's level when there is no delay or attack. A note-off starts the release on the next tick (FL's notes end on ticks; within a sample of one counts as on it); once the release is silent FL ends the voice with the declick fade. Default release tension (−101) exponent 4.62 | NULL −63…−99 dB at 65, 100, 130 and 160 BPM, Tempo off and on (nulltest/cases/fl_cases.py, nulltest/cases/tempo_env_cases.py: 41 of 44 cases; the other three −45…−54 dB, see Known gaps) |
| MUL (Resample) | 2^(raw/10000) at params+104; tape-style speed 1/MUL | NULL −68…−80 dB (×¼ … ×4) |
| LFO | Event 218 lfo_* fields. Tempo-synced: FL evaluates the LFO once per song tick (96/beat, from the note start) and interpolates linearly; the first tick holds tick 1's value. Period ticks = max(3, round(13.5755·982.5^p − 13.2745)), p = speed position (42 speeds measured); predelay ticks = same law on the predelay raw, no minimum (11 values). Attack fade in seconds like the envelope times. Shapes start at 0 going up; triangle in phase with the sine; pulse edges cross over 4 ticks (0.27205 per tick for 3 ticks, then the rest). Volume: gain × (1 + lfo); pan: + 2 × lfo (clipped); pitch envelope and LFO in cents (raw amount = cents, ±128) and half a tick early (−2.5 samples); Mod X / Y: + lfo (full scale). Note: with the volume envelope off a note plays to the end of the sample (note-off ignored), so test samples must be shorter than the gap | NULL −67…−96 dB (17 volume/pan/pitch cases), pitch envelope −60 dB |
| Stretch fields | Parameters event: PITCH int cents @100, MUL @104 (2^(raw/10000)), mode @108 (0 Resample, 1 e3 generic, … 9 e2 speech, per pyflp); TIME @96 as a float in ticks at 192 per beat ("1 bar" = 768.0; found by searching the raw bytes of a GUI-saved trial project, whose event stream is scrambled but whose 215 payload is intact). Resample + TIME plays the whole sample in TIME at the host tempo, tape-style (in-FL NULL −62…−77 dB; TIME values that aren't powers of two show a ~3e-8 rate difference, −57 dB). Auto + TIME / PITCH / MUL is bit-identical to FL's e3 generic (−300 dB), i.e. elastique. Past the sample end FL keeps interpolating at the advancing position with clamped neighbours while the declick fades (fixed: noise case −37 → −72 dB). TIME menu: (none) / Autodetect… / Project tempo (sample length at the host tempo) / 1 beat … 4 bars, long menu without Type in value; hint shows bars:steps:ticks at 24 ticks per step. Turning PITCH in Resample switches FL to Auto, which shows the method it picked (e3 generic) | FL GUI readout |
| Start offset | Fraction of the sample (FL 2026: double at params+144) | NULL −74 dB |
| Declick in-fade | Only when starting past frame 0: none / 4 ms / 4 ms / 20 ms / 100 ms / Crossfade's own curve (8750 samples at 44.1 kHz, a degree-10 polynomial fit, max error 0.6 LSB of 3192). Transient modes remove only the start step: x − x0·(1 − S) | NULL −77…−82 dB (nulltest/cases/declick_cases.py) |
| Declick out / cut | Cosine 10 ms (Out only) / 20 / 20 / 100 ms; Crossfade's own curve (8781 samples, polynomial fit). Transient (no bleeding) is not a gain fade: a one-pole high-pass (input minus a one-pole low-pass starting at 0) whose coefficient sweeps up, 1 − exp(−ΔL) with L(n) = 0.002015·(e^(0.0228 n) − 1), so a held value decays smoothly while noise rings on, then a closing gain table silences it by 440 samples (separated with DC, noise and sine renders). A cut voice keeps playing while fading | NULL −64…−98 dB (DC, noise, sine; all six modes) |
| Cut itself | New note fades earlier voices with the declick out-fade | NULL −80 dB |
| Envelope times | Knob position = (raw − 100)/65436; the minimum is zero time. FL's own times, without the tick rounding: attacks measured at six tempos, each giving the time to a tick, intersected (nulltest/probes/env_time_probe.py, env_times.txt; 28 points from raw 1000, typically ±0.3 ms). Close to t = 0.0646 e^(6.90 p) − 0.0637 s; the table interpolates log (t + 0.0637) between points. The earlier table, read at 130 BPM, was 2.6–6.8 ms long (half to one and a half ticks) | NULL −66…−99 dB (11 cases at 130 BPM, in FL) |
| Envelope shape | Linear at tension 0; tension = (e^{kx}−1)/(e^k−1) with k fitted per tension; decay/release = attack curve reversed. The decay runs the whole curve from 1 to 0 over its time and stops at the sustain level (positive decay tension: −13…−30 → −55…−74 dB) | NULL |
| Decay / release | Decay is rate-based (time = full 1→0 travel). Release joins its curve where the curve equals the current level (from sustain 0.39 at tension −101 it takes 79% of the time) | NULL −65…−78 dB |
| Sustain | Linear (raw/128) | measured |
| Precomputed reverb | Event 139 = type·65536 + mix (0–256). Identified from impulse responses (nulltest/rvb): Schroeder reverb per channel. Parallel combs with feedback g = 10^(−3D/T60), T60 = mix/256 s, then allpasses (g 0.7), then a smoothing filter; dry 0.7. **A:** combs 1777/1847/1993/2137 × 0.15, allpasses 43/127/389, output w/2 + w[−1] + w[−2]/2. **B:** combs 2467/2767/3217/3533/3877/4127 × 0.027, allpasses 67/97 (R: 79)/101/197/599, output one-pole y = w + 0.7y[−1]. The tail is then trimmed after the last sample ≥ 65/32768. Default type B | Model matches FL's IR to 0.29 LSB; NULL −60…−95 dB (14 cases, in FL; the impulse residual is 1 LSB) |
| Knob mapping | Every knob's 0..1 position equals FL's "Copy value" text, collected per knob in FL's GUI (e.g. PITCH ±1200 cents, MUL 2^(4p−2), TIME beats/192, LFO speed raw = 200 + p·65336, slide raw/1660 (glide ticks below), arp time table, echo time p·8 steps, echo pan ±2). Right-click menu: Reset / Copy value / Paste value (xx%) / Type in value… | unit tests + in-FL nulls |
| Filter: Fast LP | Same recursion as the precomputed filter (Mod X/256 = u, Mod Y/256 = res). Below u = 1/64 the coefficient falls linearly to 0, so Mod X 0 is silence (the precomputed filter keeps its 1/64 floor) | NULL/close; cutoff 0 and 2/256 in FL: silent / 16-bit floor |
| Filter: LP/BP/HP/BS | RBJ biquads (direct form I); f0 and Q from tables every 8 raw, traced tick by tick with Mod X / Mod Y LFOs (agree with the static points). Above raw 240 f0 = 16412·e^(3.99(u − 0.9375)), and modulation can push it past the knob's end up to FL's cap of 3.0 rad/sample (21056 Hz at 44.1 kHz, reached at Mod X 100%; the old 20058 Hz top point was wrong) | NULL −65…−77 dB static |
| Filter: SVF LP | Chamberlin SVF, f = (26^u − 1)/25, q = 1 − 0.8·res | NULL −65 dB |

## Tempo / Global switches, LFO graph, Levels VOL (measured in FL)

- Event 218 flags per target: 1 = envelope Tempo, 2 = LFO Tempo, 32 = LFO Global (4 is set on Volume by default). Hints: "Tempo-based time", "Global / retriggered LFO phase".
- LFO period / predelay: the knob gives a length T in ticks at 130 BPM (13.5755 * 982.5^p - 13.2745); FL rounds T * ref / 130 to whole ticks with ref = the project tempo (Tempo off: the same length in seconds at any tempo, at tick resolution) or 120 (Tempo on: a fixed musical length). The earlier "tempo-synced" description held only at 130 BPM. Matches FL at 65 / 100 / 130 BPM (-62...-77 dB, nulltest/probes/tempo_probe.py).
- Envelope Tempo: times are for 120 BPM (x 120 / tempo), then rounded to the song's ticks like any other. The "~1 % slower at 65 BPM" seen earlier with Tempo off was the tick rounding of stage lengths.
- Global: the LFO phase runs from the song start (host position); it is at full value from the note's first sample. -77 dB.
- LFO graph: drawn in knob units, not time: on a 130 px view one period is 8 + 64 x SPEED, the predelay 64 x DELAY, the linear fade 64 x ATT; height = AMT (negative starts downwards).
- FUNC Levels VOL is FL's "Volume multiplier" (event 229 offset 4, raw / 12800, default 100% = unity, gain (11^x - 1)/10, hint "100%  0.0dB  1.00"); FL's channel volume (event 219, 78%) is applied by FL to plugin channels too, so the plugin's output trim supplies the fixed 0.5 (-6.02 dB).

## Host features and robustness (tests)

- `ChanSamplerHostTest <Chan Sampler.vst3> nulltest/signals/sine.wav` (it expects that 2 s sine) hosts the built VST3 through JUCE's VST3 hosting (as a DAW does): 119 automatable parameters (plus JUCE's MIDI-CC pass-through parameters); volume automation mid-note; pitch bend; sustain pedal (CC64 holds note-offs; FL handles the pedal itself); other CCs are ignored (map them to parameters in the host); save / reload into a new instance restores every parameter (host and plugin side), the sample and the editor size, and plays bit-identically. All pass.
- `ChanSamplerRender statetest <sample>`: in-process round trip of all 144 parameters, sample and window width, bit-identical audio.
- `nulltest/probes/robust_check.py`: 44.1 / 48 / 96 kHz hosts with stereo float, mono 16-bit, stereo 24-bit and 48 kHz files all play at the right pitch and length; a 10-minute file loads and plays in 0.4 s.
- CPU (`ChanSamplerRender bench`, one core, % of real time, 32 / 64 looping voices): Resample 14 / 35, Stretch 18 / 42, Stretch Pro 25 / 53, e3 generic and Auto 16 / 36 (vocoder below 1.485 kHz, phase work on those bins only, + high-band grains; 37 / 83 in 1.1.0), e2 generic 16 / 36, e2 transient 19 / 43, e3 mono 6 / 12, e2 mono 6 / 12, e2 speech 5 / 11, slice stretch 10 / 27, slice map 12 / 23 (the bench's notes run from C2 up to B5, and a stretcher's work grows with the note's pitch ratio). Most of the stretch modes' time is FFTs: the stretcher uses its own (Source/FastFFT.h, radix-4 Stockham on split arrays with SIMD butterflies; JUCE's results to -133 dB, 3-3.6x faster than JUCE's fallback FFT at 256..8192 points, the twiddle tables shared between voices), which took e3 generic from 29 / 67 to 16 / 36; the high band's filter skips blocks of digital silence. The phase vocoder (Stretch Pro, e3 / e2 generic and transient) packs both channels into one complex FFT each way, keeps phases as unit phasors so only spectral peaks need an angle, and rebuilds Stretch Pro's formant envelope (64 cepstral coefficients) with a quarter-size FFT: Stretch Pro went from 39 / 118 to 19 / 54, the other vocoder modes from 26 / 77 to 14 / 40. Its output matches the earlier code to -95 dB at the start of a note; the earlier code's phase accumulator lost precision as notes went on (-41 dB after 3.6 s), the phasors do not. Automating cutoff, resonance, pan, volume and fine tune every block adds 0.3–0.4% (16 voices: 5.1 → 5.5% Resample, 6.1 → 6.4% Stretch Pro). The editor scales as vectors (Retina / high-DPI sharp; the waveform is computed at the screen's physical resolution).
- Fixed while testing: the stretch modes stopped after one pass of a loop (the stretcher's read position is now folded into the loop on every pass, forward and ping-pong). FL's linked loop START / END now apply to the user's edits only (knobs, menu, sample view); host automation sets the values it shows, and the engine treats an END below START as START.

## Echo delay (in FL)

Event 209 = 5 × u32: feedback (raw / 25600), pan (centre 6400; ours (raw − 6400)/6400), pitch (cents), echoes, time (raw / 48 steps). NULL −95…−108 dB for feedback, count, time and pan (nulltest/cases/echo_cases.py); echo pitch close (−57/−59 dB: only the first ~6 frames of each pitched echo differ).

- **Mod X / Y:** event 138 byte 2 = Mod X, byte 0 = Mod Y, knob = (raw − 128)/128 (a GUI save of X +12% / Y −12% gave 143 / 113; checked by sound). Echo k moves the filter value by 1 − (1 − |m|)^k towards the knob's side and clamps to 0..1: Mod X −50% gives cutoffs 0.5, 0.25, 0.125… from open; +25% from 0.25 gives 0.5, 0.69, 0.83. NULL −71…−86 dB (7 cases).
- **Fat mode:** echoes keep the normal timing, are all the same level 2G/(3·count + 2) (G = feed gain 2·feed) and the original note becomes 1.5 − count·that (the levels always sum to 1.5). Pan lowers an echo on both channels by cos(π/4 · min(1, k·|pan|)); Ping pong has no effect. NULL −97…−107 dB (5 cases).
- **Ping pong:** NULL −102 dB. The in-FL test projects now end with a placeholder note so FL renders the whole second half (it stops a fixed tail after the last note, which cut long echo trails).

## Filter modulation (in FL, nulltest/cases/lfo_cases.py)

Read from FL's output by fitting the filter coefficients in short windows against the known noise input. With an envelope or LFO on Mod X / Y, FL works the values out once per tick of the note (the first tick holds the second knot, as for the LFOs) and then:
- **Fast LP:** the cutoff coefficient f moves linearly between the tick values (in coefficient terms, not knob terms: the mid-tick bump is the proof); the resonance term holds the end-of-tick value.
- **LP / BP / HP / BS and SVF:** all coefficients hold the end-of-tick value for the whole tick, switching on the sample nearest the tick.
- Biquads behave as direct form I when the coefficients change.
NULL −59…−72 dB for LFO / envelope on Mod X and Mod Y with Fast LP, LP, HP and SVF (was −14…−52 dB).

## Tracking, root note, polyphony (in FL, nulltest/cases/untested_cases.py)

- **Tracking** (event 228 ×2, velocity then key: mid, pan, mod X, mod Y as int32; knob = raw/256): velocity offset = (raw/128)·(f(vel) − M(mid))/M(128), with f FL's velocity-to-gain curve (11^(v/100) − 1)/10 and M(m) = (6.5^(m/100) − 1)/5.5 for the middle value 0..128. Key offset = (raw/128)·(key − mid)/24 (50% = 0.5 per octave). Pan, Mod X and Mod Y alike. NULL −62…−76 dB (15 cases); velocity to Mod X −55 dB (2 cases).
- **Root note:** its own event 135 (u32), written only when not C5 (60); fine tune = Levels pitch (cents). NULL −77…−80 dB.
- **Mono / porta glide:** the pitch moves in N equal steps on the song's tick grid (each on the sample nearest its tick, the first at the event's tick); FL adds the steps to a float32 in cents from note·100, so long glides end a little flat (−0.00012 semitones after 83 steps; reproduced). N = round(0.92049·209.414^(raw/1660) − 0.15), 17 slide values measured (raw 0 → 1, 500 → 4, 820 → 13, 1000 → 23, 1660 → 193); FL's Slide readout shows N − 1 ticks. Mono with or without Porta is identical. Mono keeps a key stack: releasing the sounding key glides back to the previous key and keeps the note going; with none left the note is released (with the volume envelope off the sample plays on). Releasing another key removes it from the stack only with the volume envelope on. Porta (poly) glides each new voice from the last note. NULL −70…−82 dB (21 cases).
- **Max polyphony:** loaded by FL (shown in its GUI) but no effect in its renders (1 and 2 voices, different and same keys, envelope on and off): every voice plays. Chan Sampler shows the knob and ignores it too.
- **Overlapping notes on one key:** a note-off ends the oldest voice only. NULL −76 dB.
- **Test harness:** a tick at 130 BPM is 11025/52 samples, so the two halves of an in-FL render share sub-sample note and tick positions only when the gap is a multiple of 13 beats; multi-note cases use 26 (a 16-beat gap moves later notes by a sample, which ruins a noise null).

## Content: regions and slice markers (in FL)

Sample flags (event 143): 1 resample, 2 load regions, 4 load slice markers, 8 use loop points, 256 keep on disk (FL's default 0x0B). Rendered in FL's Slice map with test tones carrying markers (nulltest: cue_tone.wav, strc_tone.wav, strc_jump.wav): **Load regions** slices at the WAV cue points, **Load slice markers** at the ACID "strc" chunk (28-byte header with the count, 24-byte entries, position in the second field; the first entry is a header entry). Loaded markers replace the detected transients (a hard level step at 0.5 s is a slice start without markers and falls inside the 0.3 s slice with them). Chan Sampler carries the markers through rate conversion, Sample Start and Reverse; its slice boundaries land within 2 samples of FL's (12211 / 28493 vs 12211 / 28491; level step 21032 vs 21029). Keep on disk and Resample only change how FL holds the file and are shown greyed.

## Sample view and tracking defaults (FL 2026 GUI, measured)

- Sample view (pixels sampled from FL captures of a mono and a stereo file): an 8 px strip (#202324) above a 108 px view (#1B1D1E); grid lines darker than the background, #131516 every 0.1 s of the sample and #090B0C every second (the same rule at 363 and 272 px per second), plus lines at +/-50% of full scale; one waveform lane, linear and full height (peak 0.774 = 42 of 53.5 px), white #F6FEFF for mono files and #F2EDED for stereo; grey "Sampler" watermark. Bar below: #101213 / #313334 edges, track #282A2B, grey overview #A3A5A6 with 14 px arrow buttons. The grid step is the finest at least 15 px apart among powers of ten and, from 0.1 s up, 2 and 5 times them (FL: 0.1 s at 27 and 36 px rather than 0.05 s at 18 px, 0.2 s where 0.1 s would be 9 px), and the next power of ten is darker (#090B0C) with a 1 px #17191A edge each side. Zoomed in (under 100 frames per pixel) FL shows a clean anti-aliased line through the wave instead of peaks; the scroll bar shows the overview only inside the visible window and hatches the rest. The badge at the bottom right is FL's sample format: 16 for 16-bit PCM, 32 for everything it holds as float (24 / 32-bit, compressed, compressed inside a WAV), with an icon of one notch for mono and two for stereo (measured with 16-bit mono, 24-bit stereo, compressed mono and 32-bit stereo files); it has no function of its own.
- Tracking: FL opens on the Key tab (Mid C5, Copy value 0.5); the velocity Mid default is 78% (0.78125) in a fresh FL Sampler too. The key Mid range is C0..C10 (Copy value = note / 120: G#7 = 0.76667).

## Sample view menu and markers (FL GUI, observed)

FL's sample view menu: Reload, Save as..., Locate file, Copy / Paste audio, Edit in audio editor / pitch corrector / time warper, Open in new Sampler / Granulizer / Slicer / Slicex channel, Detect tempo, Get properties, Spectrum view, Multichannel waveform view. Chan Sampler has FL's entries in FL's order (FL 2026 GUI): Reload (settings kept), Save as... (the processed sample, 24-bit WAV), Locate file, Copy / Paste (FL: "Copy audio to clipboard" / "Paste audio from clipboard", shortened at the user's request; the file and its audio as WAV data; pasted data is kept as a WAV in Documents/Chan Sampler/Pasted audio), Detect tempo, Get properties, Spectrum view, Multichannel view (greyed only while the spectrum is on, as in FL) and Colorful waveforms; the audio editor (Edison), pitch corrector, time warper and new-channel entries are FL's own, and FL's Ctrl+C / V / E shortcuts don't reach a plugin. Dragging the waveform out of the view drags the sample's file (to a DAW, an editor or a folder); when that file changes on disk the sample reloads, settings kept.
- Detect tempo (FL 2026 GUI): FL's "Tempo detection" dialog offers the estimate, the ranges 50 to 100, 75 to 150, 100 to 200 and 150 to 300 BPM, Type in (BPM), Type in (beats), the project tempo and No tempo. The choice is kept as the sample's tempo: the view and TIME stay as they were (FL then offers to set the project tempo, which a plugin cannot do). TIME > Autodetect... opens the same dialog and sets TIME to the sample's length in beats at that tempo: a 2-bar loop at 128 BPM gives 2:00:00, and FL's TIME hint reads "Clip tempo is 128 BPM". A range gives the best tempo inside it (FL: 128 for the loop, 63.998 for 50 to 100). Chan Sampler's detector autocorrelates the onset strength (spectral flux) and prefers tempos that make a loop a whole number of beats (4, 8, 16...): the test loop gives 128 / 64 / 128 / 256 for the four ranges, and FL's 20 legacy drum loops all give 120 (60 / 240 for the half and double ranges; their ACID chunks say 4 beats at 60 BPM, which is used as the starting tempo).
- Panel menus (FL 2026 GUI): the echo delay's ▸ menu has Reset, Echo - classic, Echo - ping pong, Fat - detuned, Fat - more detuned, Fat - detuned + time offset and Fat - 8 voices; their values read with Copy value (feed 0.25 / 0.875, pan, pitch +2 / +3 / +6 cents, time 3 steps or 0 or one 48th of a step, 4 or 2 echoes, Ping pong / Fat mode) are rebuilt exactly. The envelope's ▸ menu ("Channel envelope presets") holds no built-in presets in FL 2026: only Save as... and the user's saved ones, as here (Documents/Chan Sampler/Envelopes, the target page's envelope and LFO). Loaded region / slice markers show as 4 px orange (#FFB871) tabs at the top of the view, as FL draws them.

- Menu look (FL capture, pixels): #515456 background, 19 px rows, text 16 px in, disabled entries grey (#818789), 7 px separators with a 1 px #2F3336 rule, a 9 x 7 px tick in the left margin. The preset menu uses the same look.
- Get properties: FL opens the system's own file Properties window (Explorer's General / Security / Details sheet). At the user's request Chan Sampler shows the facts in an FL-styled sheet instead: name, location, format (container, bit depth, or the codec inside a compressed WAV), size, date, sample rate, channels, length, the bit depth FL holds it at, embedded loop points and markers.
- Spectrum view (FL captures of a 6 dB ladder of a 3 kHz tone, a 20 Hz-step sweep around 3 kHz, clicks, white noise and tones): black, frequency linear from 0 at the bottom to half the sample rate at the top. Each pixel row shows the loudest frequency between (row - 1) and row times Nyquist / (rows - 1) (the row changes at 3070 and 3275 Hz on a 109-row view). A click is 20 px wide at 16.2 samples per px, the shape of a 328-sample Blackman-Harris window (0.5 dB fit; a Hann or Gaussian does not fit), and its level and the noise level are those of that window to 0.4 dB; yet a steady tone stays two rows thick with black (-110 dB) next to it, which no single window gives. Chan Sampler multiplies the short window's spectrum by a long (Gaussian, sigma 152 samples) spectrum divided by its local peak over +/-32 bins (2048-point FFT): mean error 0.2-1.0 dB per pixel over the four captures, noise -25.1 dB against FL's -25.3. Levels rise 3 dB per octave (0 at 3 kHz; click and noise rows agree to 0.5 dB), and the palette is FL's by level from the ladder: black at -112 dB, deep blue at -100, purple at -84, red from -66, orange -36, yellow -24, white towards 0 dB. FL keeps the "Sampler" watermark over it.
- Colorful waveforms (FL Options > General settings: Off / Color map / Multiband; FL's own default is Off, Chan Sampler sets it per plugin from the sample menu, Multiband by default). Measured with 20 tones from 30 Hz to 20 kHz, four two-tone mixtures, a level ladder, a synthetic test (tone switches, a 100 dB decay, tones with weak noise, a -70 dB tone, silences) and FL's own 808 kick, 707 snare, 808 open hat, a tom, a stereo drum loop and a vocal, all in both modes. The level does not change the colour. Bands: first-order filters in a tree, low = low-pass 168 Hz, the rest (high-pass 152 Hz) split at 1284 Hz into mid and high (1.2 RGB steps on the tones and mixtures; the first fit, 162 / 1312 Hz without the tree, left too much high band in low notes). Time: FL's colours change before a switch and fade out after it, so each band's energy is taken in blocks that follow a rise at once and a fall smoothly, and a pixel reads the level at its centre (a quarter block late, interpolated). Multiband: 32-frame blocks, 700-frame fall (a 100 Hz to 6.4 kHz switch fits to 1.6 steps; the first fit, 512-frame blocks, came from 2 s samples at 160 frames per pixel, where a drum's click is under a pixel: FL's own 707 Kick, 909 Kick, 707 Snare, 808 CH, 707 Tamb and Alma Kick, 8-50 frames per pixel, start blue / teal in FL and only 32-frame blocks show it, 707 Kick's first 40 columns 38.6 -> 8.0 RGB steps). A pitched view is coloured from the stretched copy, as FL's is (707 Kick an octave down starts teal, not blue); Color map is much faster (64 frames, 100-frame fall), which is why FL's Color map flickers on low notes and stripes a vocal while Multiband stays steady. Silence keeps the last colour (the tom's tail stays red). Stereo samples use the mean of both channels' band energies (a stereo loop: 6.3 to 3.8 steps). Mono samples are drawn 5% brighter (R + G + B = 257 instead of 245; FL's mono wave colour is brighter too). Multiband: RGB in proportion to the band RMS. Color map: (0.4 mid + high) / (low + mid + high) on FL's palette (red-pink, orange, cream, green, teal, blue, violet). Result against the captures: Multiband 0.4-2.5 RGB steps on solid fills (hat 0.4, vocal 1.1, t1 2.1); Color map matches FL's colours and flicker character, single flickering columns cannot match. The scroll-bar overview is coloured the same way.
- File buttons (FL capture, pixel by pixel): 32 / 19 / 19 x 23 px boxes, a #141717 frame, a lighter inner edge and a fill darkening downwards; 1 px line icons: a 12 x 10 px folder, a 9 x 8 px folder over a small tree (locate), a 9 x 9 px cross.
- FL's message dialogs (captured): #202324 window with a 25 px title bar, the text on a #282C2D panel with a large grey sign, OK buttons with a dark frame and a darkening fill. Chan Sampler shows its messages (preset import, unreadable file) the same way inside the plugin window.

## FL Sampler presets (.fst)

A channel preset is the project event stream of one channel (FLhd / FLdt, the same events as a project). Source/FlPreset.cpp reads the Sampler's fields with the scales above; event 21 (channel type 0, or 4 without a plugin name) tells a Sampler preset from a generator's. The preset's own channel pan / volume (219) are folded into Levels, as a plugin channel applies its own. FL 4 and earlier store pans and volumes at 1/100 of today's scale (echo pan centre 64, not 6400; FL 9 onwards are current, measured across FL's preset library). Checked by writing each in-FL null case's Sampler channel out as a preset (nulltest/cases/fst_cases.py): the import renders bit-identically to the parameter settings that null in FL, in all 177 cases. The FL trial cannot save presets, so FL-written files are untested here.

## Loop points (in FL)

Event 251 (43 bytes, present once set): int 2, start, 0, end, 0, double −1.0, int −1, byte 0, int 0x7fffffff ×2, 0, 1. Use loop points = event 143 bit 8, Ping pong loop = event 20. The END frame is part of a forward loop; with loop points the crossfade length is crossfade/4 × the sample and moves the loop start that far in. Forward, short, ping-pong, crossfade and off: NULL −79…−83 dB.

## Control timing

Envelopes / LFOs are updated in 32-sample control blocks that start at each note's own start (not the host buffer), and an event that changes a voice refreshes its targets on that sample without moving the envelopes on. The volume envelope runs per sample. Before, a transition could move by up to one block depending on where the note fell in the host buffer (hold 5000: −57 → −68 dB; offline renders now match in-FL ones). Arp TIME 640 stays at −46 dB: 4 of ~100 steps fall on exact half-sample ties that FL rounds both ways (its internal arithmetic is not visible).

## Playback markers and loop knobs (FL GUI, measured)

FL defaults: Use loop points on, loop START = END = 0 (0 = no loop); the sample view shows no markers. The Start marker appears once the start offset is above 0; the loop once END is above 0 (START = END is drawn as one line). START moves alone while below END and pushes END to START + 1 sample when it reaches it; END moves alone but stops at START. FL draws the loop red even with Use loop points off (Chan Sampler draws an unused loop grey, by request). Loading a new sample resets start offset and loop points to 0 unless the file has an embedded loop, which sets the loop markers; Use loop points is not touched (FL leaves it as it was).

## Group (Cut / By) between instances

FL's channel Group is a host feature; Chan Sampler rebuilds it across its own instances (Source/CutGroups.*): every note start of an instance with Cut = n is posted on a process-wide lock-free bus with its host timeline position, and instances with By = n stop their voices there (release, or the declick fade without a volume envelope, as Cut self). An instance never cuts itself through its group (Cut self does that). Sample-exact when the cut instance is processed after the cutting one, otherwise at the start of its next block (`ChanSamplerRender cuttest dc6.wav`: 0 and 144 samples late at 512-sample blocks). With the transport stopped the cut happens at the next block. Bridged / sandboxed instances in other processes cannot see each other. FL's Time panel (gate / shift / swing / full porta) edits FL's note events and is not reproduced. FL has no Panning envelope (LFO only), so neither does Chan Sampler.

## Known gaps (diff / approximate)

As of 1.0:

- **Time-stretch modes:** our own WSOLA and phase-vocoder algorithms (Stretch Pro with cepstral formants). FL's Elastique is proprietary, so they match FL in duration, pitch and character, not sample for sample. Resample with TIME nulls.
- **Envelope hold end after another timed stage:** FL's rounding of a hold that follows an attack matches a single rule in 48 of 74 measured combinations (nulltest/probes/env_hold_probe.py) and is a tick off in the rest; neither separate nor cumulative rounding explains all of them. A tick off at the hold's end: −45…−54 dB (the default envelope at 65, 100 and 160 BPM, Tempo off). Above raw 47500 the table points are known to ±2 ms only (FL's per-tick steps drift slightly over thousands of ticks).
- **LPx2 at maximum resonance** on very short sounds: −32 dB (the first-tick glide of its level compensation). Otherwise LPx2 −44…−63 dB and SVF LPx2 bit-exact (nulltest/cases/filter_x2_cases.py): SVF LPx2 = two Chamberlin stages with q1 = 1 − 0.8 res, q2 = 1 − 0.6 res; LPx2 = two identical LP biquads (no Q floor) times a measured level compensation (resonance × cutoff table) that starts at unity and glides to its value over the note's first tick.
- **SMP trim tail on noise:** FL's tail depends on the content (not a linear filter), so very short trims of noisy samples differ slightly.
- **Arpeggiator TIME "Hold":** approximated as one bar.
- **LFO:** see the LFO row above. FL's pan envelope had no audible effect in CLI renders (amount ±128, sustain 128), so Chan Sampler has a panning LFO and no panning envelope.
- **Sample view:** the spectrum is a model of FL's (about 1 dB mean error per pixel); colourful waveforms match within 0.4–2.5 RGB steps on solid fills (a stereo loop about 4), Color map's column-to-column flicker matches in character only.
- **Not implemented** (FL host features or FL-only storage): Keep on disk and Resample (how FL holds the file), gate / shift / swing (FL's Time panel), FL's channel VOL / PAN / PITCH / RANGE / TRACK (host-side), Edit in audio editor / pitch corrector / time warper, Open in new channel, and the sample menu's keyboard shortcuts. Max polyphony is shown but, as in FL's renders, does nothing.

## UI layout (FL's own form)

Controls sit at the positions and sizes of FL's own `TPluginForm` controls: `SmpSheet`, `InsSheet`, `FuncSheet` and `TopPanel`, each sheet 561×399 under a 45 px top panel. The canvas is scaled to the window. Option lists use FL's own wording, e.g. `DeclickModeSelect` and the filter types. Panels for FL-only features were removed, and the arpeggiator / echo panels were rearranged to fit.

## Format notes (FL 2026 vs PyFLP)

- **Parameters event (215):** fields from offset 128 are doubles: Sample Start at 128, Length at 136, Start offset at 144. Normalize (80), Reverse polarity (81), declick mode (83), crossfade (84) and trim (88) are unchanged.
- **Five envelope/LFO events (218)** in INS order: Panning, Volume, Mod X, Mod Y, Pitch.
- **Root note** is event 135, not a Parameters field (offset 16 of 215 has no effect). Polyphony (221) = max u32, slide u32, flags byte (1 mono, 2 porta), as in PyFLP.

## Null-test results

| Case | Residual (dB) | Result |
|---|---|---|
| v128_base | -75.3 | NULL |
| v128_boost16 | -75.9 | NULL |
| v128_boost64 | -83.8 | NULL |
| v128_boost128 | -88.9 | NULL |
| v128_boost256 | -93.8 | NULL |
| v128_boost64clip | -94.9 | NULL |
| v128_eq0 | -68.5 | NULL |
| v128_eq64 | -66.0 | NULL |
| v128_eq192 | -66.0 | NULL |
| v128_eq256 | -68.6 | NULL |
| v128_fin64 | -74.2 | NULL |
| v128_fout64 | -75.0 | NULL |
| v128_fin256 | -70.6 | NULL |
| v128_fout256 | -72.8 | NULL |
| v128_fin1024 | -67.2 | NULL |
| v128_fout1024 | -72.7 | NULL |
| v128_start25 | -73.4 | NULL |
| v128_len50 | -33.0 | close |
| v128_norm | -78.4 | NULL |
| v128_rev | -77.5 | NULL |
| v128_inv | -75.3 | NULL |
| v128_swap | -75.3 | NULL |
| v128_fadest | -72.6 | NULL |
| v128_remdc | -82.0 | NULL |
| v128_trim16 | -75.7 | NULL |
| v128_trim64 | -76.3 | NULL |
| v128_trim256 | -77.0 | NULL |
| v128_rm256_128 | -62.1 | NULL |
| v128_rm128_64 | -61.2 | NULL |
| v128_rm256_0 | -62.0 | NULL |
| v128L_pccut512_0 | -60.6 | NULL |
| v128L_pccut768_0 | -67.7 | NULL |
| v128L_pccut256_0 | -52.7 | close |
| v128L_pccut640_320 | -71.9 | NULL |
| v128L_pccut640_640 | -52.6 | close |
| v128_sd0 | -75.6 | NULL |
| v128_sd1024 | -75.6 | NULL |
| v128_sd3072 | -77.2 | NULL |
| v128_sd4096 | -77.2 | NULL |
| v128_pogo0 | -64.1 | NULL |
| v128_pogo128 | -64.5 | NULL |
| v128_pogo384 | -63.7 | NULL |
| v128_pogo512 | -49.6 | close |
| v128_so0.1 | -74.0 | NULL |
| v128_so0.5 | -73.8 | NULL |
| v128_dk0_so | -74.8 | NULL |
| v128_dk0_cut | -79.4 | NULL |
| v128_dk1_so | -38.0 | close |
| v128_dk1_cut | -36.8 | close |
| v128_dk2_so | -74.6 | NULL |
| v128_dk2_cut | -80.6 | NULL |
| v128_dk3_so | -75.0 | NULL |
| v128_dk3_cut | -80.6 | NULL |
| v128_dk4_so | -76.7 | NULL |
| v128_dk4_cut | -81.6 | NULL |
| v128_dk5_so | -22.8 | diff |
| v128_dk5_cut | -29.9 | diff |
| v128_env_default | -44.0 | close |
| v128_env_att10000 | -37.8 | close |
| v128_env_dec20000 | -55.2 | close |
| v128_env_rel20000 | -82.7 | NULL |
| v128_env_tension | -24.2 | diff |
| v128_flt0_128_0 | -58.8 | close |
| v128_flt0_96_160 | -65.3 | NULL |
| v128_flt1_128_0 | -58.6 | close |
| v128_flt1_96_160 | -58.0 | close |
| v128_flt2_128_0 | -74.0 | NULL |
| v128_flt2_96_160 | -57.4 | close |
| v128_flt3_128_0 | -71.4 | NULL |
| v128_flt3_96_160 | -65.9 | NULL |
| v128_flt4_128_0 | -69.7 | NULL |
| v128_flt4_96_160 | -68.5 | NULL |
| v128_flt5_128_0 | 6.7 | diff |
| v128_flt5_96_160 | -11.1 | diff |
| v128_flt6_128_0 | -64.6 | NULL |
| v128_flt6_96_160 | -65.3 | NULL |
| v128_flt7_128_0 | -64.5 | NULL |
| v128_flt7_96_160 | -14.3 | diff |
| v128_xf64_noloop | -76.5 | NULL |
| v128_xf128_noloop | -76.6 | NULL |
| v128_xf256_noloop | -76.7 | NULL |
| v128_st25_len50 | -32.7 | close |
