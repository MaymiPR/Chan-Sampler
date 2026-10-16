# Evidence ledger: FL Studio Channel Sampler → Chan Sampler

Chan Sampler's behaviour is matched to FL Studio 2026 (26.1.7) by **null testing**. Each case is rendered twice, once by FL and once by Chan Sampler; one render is polarity-flipped and the two are summed. The residual shows how closely they match. No FL code, images or impulse responses are copied. Every law below is a formula or table fitted to FL's *output*.

Sources:
- **[M]** Image-Line's online manual: chansettings_sampler / _ins / _misc, plus screenshots for colours.
- **[B-FORM]** The layout of FL's channel-settings window. REA evidence `ev_1003b2d847ac512b3de6d6965dcedfd8f0e1f3089e99fe5f6f6d6471264aeff1` (`inspect_pe_resources` on `FLEngine_x64.dll`, SHA-256 `6d9c5ab0…8e02`). The RCDATA `TPLUGINFORM` Delphi form was decoded for control geometry, captions and option lists.
- **[PYFLP]** The open-source PyFLP project's tables of FLP event IDs and raw parameter ranges. Some offsets have moved in FL 2026; see "Format notes".
- **[NULL]** Runtime measurements and null tests, in `nulltest/`:
  1. `build_project.py` / `flrender.py` patch FL's Basic template and render it with `FL64.exe /R /Ewav`.
  2. `ChanSamplerRender.exe` renders the same case through the real plugin processor.
  3. `nulltest.py` aligns, gain-matches and subtracts the two renders.

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
| Sample start / Length | Doubles at params+128 / +136. start = round(start·(N−1)); length = ceil(fraction · what remains) (floating point: 0.3·88200 rounds up to 26461); at least 40 frames (LENGTH 0 plays 40). **Reverse applies to the trimmed segment.** The trimmed start and end are linear filters, not gain fades (measured with clicks, superposition verified; nulltest/tail_measure.py, head_measure.py, tail_short.py → Source/TrimTail.h): at the end each output keeps its own frame and subtracts a weighted average of the frames behind it, up to twice its distance from the end (low frequencies fade first, DC reaches 0 at the end); at the start each output mixes the input from n to about 3n over 22 frames. The shortest segment (LENGTH 0 = 40 frames) has its own measured table and carries two silent frames after it. Order: start filter, end filter, then Reverse. A segment of pure digital silence around the cut renders silent in FL START exactly 0.5 (a half-sample tie): the play position rounds the tie up but the length count rounds it down, so the segment runs one frame past the sample end (silence; the end then holds that silence) — modelled, NULL −69 dB with and without Reverse. Fades are applied before Reverse (measured) | Full length NULL −66…−90 dB at any start, with or without Reverse; trimmed: NULL/close −46…−64 dB (shorter segments weigh the tiny tail residual more) |
| Crossfade | Knob position = raw/1024; loop start = position/2 of the sample (FL truncates). Without loop points the sample loops from there to the end; the last stretch of that length blends linearly into the audio before the loop start | NULL −82…−84 dB (5 cases, in FL) |
| Output level | FL's plugin wrapper outputs twice what a default Sampler channel does, so a default Chan Sampler must output 0.5: VOL default 0.551 × OUT −0.844 dB | NULL −78.8 dB (defaults, in FL) |
| Arpeggiator chords | Auto, Auto (sustain), (none) plus FL's 66 chord presets (names and intervals as in FL's `System/Config/Chords.map`); presets build on the lowest held key | FL data file |
| Normalize, Reverse, Polarity, Swap, Fade stereo (cos/sin), Remove DC (mean) | Exactly as named | NULL −73…−82 dB |
| Trim | Cuts after the last frame whose louder channel exceeds raw/32768; at 0 nothing is cut (trailing digital silence is kept) | NULL −76 dB |
| Ring mod | ×((1−mix) + mix·m), m advances (raw+8)/5760 cycles/sample; **sin on L, cos on R**; a mono file stays mono, so sin on both | NULL −61…−74 dB (stereo, mono, 48 kHz) |
| Precomputed cut/res | y = y1 + f(x − y1) + r(y1 − y2), f = (601^u − 1)/600; r measured at raw resonance 0, 32 … 640 (cutoff open: y = x + r(y1 − y2)), cubic in log(1 − r) | NULL −70…−100 dB (all settings tested) |
| Stereo delay | Delays one channel by \|raw − 2048\| samples (≤ 2048); right = delays left; the sample grows | NULL −76 dB |
| Pogo | Length-preserving time warp: (1−\|a\|)x + \|a\|·w(x), w = sin(πx/2) or 1 − (2/π)asin(1−x); 4-point Hermite with silence outside the sample | NULL −75 dB (full range) |
| Playback interpolation | 30-tap windowed sinc, k(d) = c·sinc(c·d)·w(d), \|d\| < 15 source frames, c = min(1, 1/ratio); the window stays 15 frames wide; w = 7-term cosine series (nulltest/kernel_extract.py, window_fit.py) | < 1 LSB vs 686k measured points; pitched notes NULL |
| Sample-rate conversion | Files at another rate are converted to the project rate on load with an 88-tap windowed sinc (\|d\| < 44, near-Hann 9-term window), then played through the 30-tap kernel | 1 LSB; 48 kHz mono sweep NULL |
| Envelope graph | Stage width = knob position × ⅓ of the view (decay × (1 − sustain)); no sustain plateau; release drawn from the sustain level at full width; delay is empty space; curves bend with each tension | matches FL screenshots (9 settings) |
| Arpeggiator | Parameters event: direction @40, range @44, chord @48 (−2 Auto, −1 Auto (sustain) = FL default, 0 (none), k = preset k−1), time @52, gate @56 (raw/48), slide @60, repeat @92. Step k starts at round(k·step) after the first. TIME position = (raw−450)/997, raw 1447 = Hold; FL plays whole ticks: round(3.88852·25^p − 1.08184) (77 raws measured). Arp notes end at the gate end or key release (cut with the declick fade when the volume envelope is off), unlike normal notes. Auto (sustain) holds a single key and restarts it on the next step once the sample has run out. Slide = legato: the sounding note takes the next pitch at the step, no glide; once it runs out later steps stay silent. FL sends All-notes-off right before the note-off | NULL −61…−83 dB (22 cases); one TIME value −46 dB (half-sample ties) |
| Stretch modes | FL keeps pitch and length independent in every stretch mode (key change keeps the length; MUL / TIME change the length, not the pitch; FL renders). Chan Sampler (TimeStretch.cpp, own code): WSOLA for Stretch / mono / speech / slice stretch, phase vocoder with identity phase locking and transient resets for e3/e2 generic and transient, Stretch Pro = 4096-point vocoder with cepstral formant preservation and FORMANT shift (+/-1200 cents, centre = preserve). Elastique is proprietary, so these cannot null; log-spectrogram distance to FL's renders 7-10 dB | stretch_check.py, stretch_vs_fl.py |
| Envelope jump ramps | Rises (after delay) and releases follow measured 424-sample shapes (jump_up / jump_down tables); falls at hold end keep the two-slope ramp. Default release tension (-101) exponent 4.62 | NULL -61...-77 dB (all but hold 5000: -57) |
| MUL (Resample) | 2^(raw/10000) at params+104; tape-style speed 1/MUL | NULL −68…−80 dB (×¼ … ×4) |
| LFO | Event 218 lfo_* fields. Tempo-synced: FL evaluates the LFO once per song tick (96/beat, from the note start) and interpolates linearly; the first tick holds tick 1's value. Period ticks = max(3, round(13.5755·982.5^p − 13.2745)), p = speed position (42 speeds measured); predelay ticks = same law on the predelay raw, no minimum (11 values). Attack fade in seconds like the envelope times. Shapes start at 0 going up; triangle in phase with the sine; pulse edges cross over 4 ticks (0.27205 per tick for 3 ticks, then the rest). Volume: gain × (1 + lfo); pan: + 2 × lfo (clipped); pitch envelope and LFO in cents (raw amount = cents, ±128) and half a tick early (−2.5 samples); Mod X / Y: + lfo (full scale). Note: with the volume envelope off a note plays to the end of the sample (note-off ignored), so test samples must be shorter than the gap | NULL −67…−96 dB (17 volume/pan/pitch cases), pitch envelope −60 dB |
| Stretch fields | Parameters event: PITCH int cents @100, MUL @104 (2^(raw/10000)), mode @108 (0 Resample, 1 e3 generic, … 9 e2 speech, per pyflp); TIME @96 as a float in ticks at 192 per beat ("1 bar" = 768.0; found by searching the raw bytes of a GUI-saved trial project, whose event stream is scrambled but whose 215 payload is intact). Resample + TIME plays the whole sample in TIME at the host tempo, tape-style (in-FL NULL −62…−77 dB; TIME values that aren't powers of two show a ~3e-8 rate difference, −57 dB). Auto + TIME / PITCH / MUL is bit-identical to FL's e3 generic (−300 dB), i.e. elastique. Past the sample end FL keeps interpolating at the advancing position with clamped neighbours while the declick fades (fixed: noise case −37 → −72 dB). TIME menu: (none) / Autodetect… / Project tempo (sample length at the host tempo) / 1 beat … 4 bars, long menu without Type in value; hint shows bars:steps:ticks at 24 ticks per step. Turning PITCH in Resample switches FL to Auto, which shows the method it picked (e3 generic) | FL GUI readout |
| Start offset | Fraction of the sample (FL 2026: double at params+144) | NULL −74 dB |
| Declick in-fade | Only when starting past frame 0: none / 4 ms / 4 ms / 20 ms / 100 ms. Transient modes remove only the start step: x − x0·(1 − S) | NULL (Transient: −75 dB) |
| Declick out / cut | Cosine 10 / 10 / 20 / 20 / 100 ms; a cut voice keeps playing while fading | NULL −79…−82 dB |
| Cut itself | New note fades earlier voices with the declick out-fade | NULL −80 dB |
| Envelope times | Knob position = (raw − 100)/65436; measured 27-point table (linear below raw 5000, 64 s at max); the minimum is zero time; attack floor 19.23 ms. Every jump (a stage with no time) is smoothed by a fixed 9.59 ms ramp (0.54 of the way at 4.76 ms), and delay/hold end 4.76 ms early so the ramp is centred | NULL/close −57…−78 dB (11 cases, in FL) |
| Envelope shape | Linear at tension 0; tension = (e^{kx}−1)/(e^k−1) with k fitted per tension; decay/release = attack curve reversed | NULL/close |
| Decay / release | Decay is rate-based (time = full 1→0 travel). Release joins its curve where the curve equals the current level (from sustain 0.39 at tension −101 it takes 79% of the time) | NULL −65…−78 dB |
| Sustain | Linear (raw/128) | measured |
| Precomputed reverb | Event 139 = type·65536 + mix (0–256). Identified from impulse responses (nulltest/rvb): Schroeder reverb per channel. Parallel combs with feedback g = 10^(−3D/T60), T60 = mix/256 s, then allpasses (g 0.7), then a smoothing filter; dry 0.7. **A:** combs 1777/1847/1993/2137 × 0.15, allpasses 43/127/389, output w/2 + w[−1] + w[−2]/2. **B:** combs 2467/2767/3217/3533/3877/4127 × 0.027, allpasses 67/97 (R: 79)/101/197/599, output one-pole y = w + 0.7y[−1]. The tail is then trimmed after the last sample ≥ 65/32768. Default type B | Model matches FL's IR to 0.29 LSB; NULL −60…−95 dB (14 cases, in FL; the impulse residual is 1 LSB) |
| Knob mapping | Every knob's 0..1 position equals FL's "Copy value" text, collected per knob in FL's GUI (e.g. PITCH ±1200 cents, MUL 2^(4p−2), TIME beats/192, LFO speed raw = 200 + p·65336, slide raw/1660 (glide ticks below), arp time table, echo time p·8 steps, echo pan ±2). Right-click menu: Reset / Copy value / Paste value (xx%) / Type in value… | unit tests + in-FL nulls |
| Filter: Fast LP | Same recursion as the precomputed filter (Mod X/256 = u, Mod Y/256 = res). Below u = 1/64 the coefficient falls linearly to 0, so Mod X 0 is silence (the precomputed filter keeps its 1/64 floor) | NULL/close; cutoff 0 and 2/256 in FL: silent / 16-bit floor |
| Filter: LP/BP/HP/BS | RBJ biquads (direct form I); f0 and Q from tables every 8 raw, traced tick by tick with Mod X / Mod Y LFOs (agree with the static points). Above raw 240 f0 = 16412·e^(3.99(u − 0.9375)), and modulation can push it past the knob's end up to FL's cap of 3.0 rad/sample (21056 Hz at 44.1 kHz, reached at Mod X 100%; the old 20058 Hz top point was wrong) | NULL −65…−77 dB static |
| Filter: SVF LP | Chamberlin SVF, f = (26^u − 1)/25, q = 1 − 0.8·res | NULL −65 dB |

## Tempo / Global switches, LFO graph, Levels VOL (measured in FL)

- Event 218 flags per target: 1 = envelope Tempo, 2 = LFO Tempo, 32 = LFO Global (4 is set on Volume by default). Hints: "Tempo-based time", "Global / retriggered LFO phase".
- LFO period / predelay: the knob gives a length T in ticks at 130 BPM (13.5755 * 982.5^p - 13.2745); FL rounds T * ref / 130 to whole ticks with ref = the project tempo (Tempo off: the same length in seconds at any tempo, at tick resolution) or 120 (Tempo on: a fixed musical length). The earlier "tempo-synced" description held only at 130 BPM. Matches FL at 65 / 100 / 130 BPM (-62...-77 dB, nulltest/tempo_probe.py).
- Envelope Tempo: times are for 120 BPM (x 120 / tempo). The jump ramps and the shortest attack are counted in ticks (scaled by 130 / tempo). With Tempo off FL's envelope also runs ~1 % slower at 65 BPM (0.5 % at 120) — not modelled (-54...-60 dB).
- Global: the LFO phase runs from the song start (host position); it is at full value from the note's first sample. -77 dB.
- LFO graph: drawn in knob units, not time: on a 130 px view one period is 8 + 64 x SPEED, the predelay 64 x DELAY, the linear fade 64 x ATT; height = AMT (negative starts downwards).
- FUNC Levels VOL is FL's "Volume multiplier" (event 229 offset 4, raw / 12800, default 100% = unity, gain (11^x - 1)/10, hint "100%  0.0dB  1.00"); FL's channel volume (event 219, 78%) is applied by FL to plugin channels too, so the plugin's output trim supplies the fixed 0.5 (-6.02 dB).

## Host features and robustness (tests)

- `ChanSamplerHostTest <Chan Sampler.vst3> <sample>` hosts the built VST3 through JUCE's VST3 hosting (as a DAW does): 119 automatable parameters (plus JUCE's MIDI-CC pass-through parameters); volume automation mid-note; pitch bend; sustain pedal (CC64 holds note-offs; FL handles the pedal itself); other CCs are ignored (map them to parameters in the host); save / reload into a new instance restores every parameter (host and plugin side), the sample and the editor size, and plays bit-identically. All pass.
- `ChanSamplerRender statetest <sample>`: in-process round trip of all 144 parameters, sample and window width, bit-identical audio.
- `nulltest/robust_check.py`: 44.1 / 48 / 96 kHz hosts with stereo float, mono 16-bit, stereo 24-bit and 48 kHz files all play at the right pitch and length; a 10-minute file loads and plays in 0.4 s.
- CPU (one core, 32 looping voices): Resample 18%, Stretch 21%, Stretch Pro 70%, e3/e2 generic 45%, mono / speech modes 16-29%, slice modes 12-19%.
- Fixed while testing: the stretch modes stopped after one pass of a loop (the stretcher's read position is now folded into the loop on every pass, forward and ping-pong). FL's linked loop START / END now apply to the user's edits only (knobs, menu, sample view); host automation sets the values it shows, and the engine treats an END below START as START.

## Echo delay (in FL)

Event 209 = 5 × u32: feedback (raw / 25600), pan (centre 6400; ours (raw − 6400)/6400), pitch (cents), echoes, time (raw / 48 steps). NULL −95…−108 dB for feedback, count, time and pan (nulltest/echo_cases.py); echo pitch close (−57/−59 dB: only the first ~6 frames of each pitched echo differ).

- **Mod X / Y:** event 138 byte 2 = Mod X, byte 0 = Mod Y, knob = (raw − 128)/128 (a GUI save of X +12% / Y −12% gave 143 / 113; checked by sound). Echo k moves the filter value by 1 − (1 − |m|)^k towards the knob's side and clamps to 0..1: Mod X −50% gives cutoffs 0.5, 0.25, 0.125… from open; +25% from 0.25 gives 0.5, 0.69, 0.83. NULL −71…−86 dB (7 cases).
- **Fat mode:** echoes keep the normal timing, are all the same level 2G/(3·count + 2) (G = feed gain 2·feed) and the original note becomes 1.5 − count·that (the levels always sum to 1.5). Pan lowers an echo on both channels by cos(π/4 · min(1, k·|pan|)); Ping pong has no effect. NULL −97…−107 dB (5 cases).
- **Ping pong:** NULL −102 dB. The in-FL test projects now end with a placeholder note so FL renders the whole second half (it stops a fixed tail after the last note, which cut long echo trails).

## Filter modulation (in FL, nulltest/lfo_cases.py)

Read from FL's output by fitting the filter coefficients in short windows against the known noise input. With an envelope or LFO on Mod X / Y, FL works the values out once per tick of the note (the first tick holds the second knot, as for the LFOs) and then:
- **Fast LP:** the cutoff coefficient f moves linearly between the tick values (in coefficient terms, not knob terms: the mid-tick bump is the proof); the resonance term holds the end-of-tick value.
- **LP / BP / HP / BS and SVF:** all coefficients hold the end-of-tick value for the whole tick, switching on the sample nearest the tick.
- Biquads behave as direct form I when the coefficients change.
NULL −59…−72 dB for LFO / envelope on Mod X and Mod Y with Fast LP, LP, HP and SVF (was −14…−52 dB).

## Tracking, root note, polyphony (in FL, nulltest/untested_cases.py)

- **Tracking** (event 228 ×2, velocity then key: mid, pan, mod X, mod Y as int32; knob = raw/256): velocity offset = (raw/128)·(f(vel) − M(mid))/M(128), with f FL's velocity-to-gain curve (11^(v/100) − 1)/10 and M(m) = (6.5^(m/100) − 1)/5.5 for the middle value 0..128. Key offset = (raw/128)·(key − mid)/24 (50% = 0.5 per octave). Pan, Mod X and Mod Y alike. NULL −62…−76 dB (17 cases).
- **Root note:** its own event 135 (u32), written only when not C5 (60); fine tune = Levels pitch (cents). NULL −77…−80 dB.
- **Mono / porta glide:** the pitch moves in N equal steps on the song's tick grid (each on the sample nearest its tick, the first at the event's tick); FL adds the steps to a float32 in cents from note·100, so long glides end a little flat (−0.00012 semitones after 83 steps; reproduced). N = round(0.92049·209.414^(raw/1660) − 0.15), 17 slide values measured (raw 0 → 1, 500 → 4, 820 → 13, 1000 → 23, 1660 → 193); FL's Slide readout shows N − 1 ticks. Mono with or without Porta is identical. Mono keeps a key stack: releasing the sounding key glides back to the previous key and keeps the note going; with none left the note is released (with the volume envelope off the sample plays on). Releasing another key removes it from the stack only with the volume envelope on. Porta (poly) glides each new voice from the last note. NULL −70…−82 dB (21 cases).
- **Max polyphony:** loaded by FL (shown in its GUI) but no effect in its renders (1 and 2 voices, different and same keys, envelope on and off): every voice plays. Chan Sampler shows the knob and ignores it too.
- **Overlapping notes on one key:** a note-off ends the oldest voice only. NULL −76 dB.
- **Test harness:** a tick at 130 BPM is 11025/52 samples, so the two halves of an in-FL render share sub-sample note and tick positions only when the gap is a multiple of 13 beats; multi-note cases use 26 (a 16-beat gap moves later notes by a sample, which ruins a noise null).

## Content: regions and slice markers (in FL)

Sample flags (event 143): 1 resample, 2 load regions, 4 load slice markers, 8 use loop points, 256 keep on disk (FL's default 0x0B). Rendered in FL's Slice map with test tones carrying markers (nulltest: cue_tone.wav, strc_tone.wav, strc_jump.wav): **Load regions** slices at the WAV cue points, **Load slice markers** at the ACID "strc" chunk (28-byte header with the count, 24-byte entries, position in the second field; the first entry is a header entry). Loaded markers replace the detected transients (a hard level step at 0.5 s is a slice start without markers and falls inside the 0.3 s slice with them). Chan Sampler carries the markers through rate conversion, Sample Start and Reverse; its slice boundaries land within 2 samples of FL's (12211 / 28493 vs 12211 / 28491; level step 21032 vs 21029). Keep on disk and Resample only change how FL holds the file and are shown greyed.

## FL Sampler presets (.fst)

A channel preset is the project event stream of one channel (FLhd / FLdt, the same events as a project). Source/FlPreset.cpp reads the Sampler's fields with the scales above; event 21 (channel type 0, or 4 without a plugin name) tells a Sampler preset from a generator's. The preset's own channel pan / volume (219) are folded into Levels, as a plugin channel applies its own. FL 4 and earlier store pans and volumes at 1/100 of today's scale (echo pan centre 64, not 6400; FL 9 onwards are current, measured across FL's preset library). Checked by writing each in-FL null case's Sampler channel out as a preset (nulltest/fst_cases.py): the import renders bit-identically to the parameter settings that null in FL, in all 177 cases. The FL trial cannot save presets, so FL-written files are untested here.

## Loop points (in FL)

Event 251 (43 bytes, present once set): int 2, start, 0, end, 0, double −1.0, int −1, byte 0, int 0x7fffffff ×2, 0, 1. Use loop points = event 143 bit 8, Ping pong loop = event 20. The END frame is part of a forward loop; with loop points the crossfade length is crossfade/4 × the sample and moves the loop start that far in. Forward, short, ping-pong, crossfade and off: NULL −79…−83 dB.

## Control timing

Envelopes / LFOs are updated in 32-sample control blocks that start at each note's own start (not the host buffer), and an event that changes a voice refreshes its targets on that sample without moving the envelopes on. The volume envelope runs per sample. Before, a transition could move by up to one block depending on where the note fell in the host buffer (hold 5000: −57 → −68 dB; offline renders now match in-FL ones). Arp TIME 640 stays at −46 dB: 4 of ~100 steps fall on exact half-sample ties that FL rounds both ways (its internal arithmetic is not visible).

## Playback markers and loop knobs (FL GUI, measured)

FL defaults: Use loop points on, loop START = END = 0 (0 = no loop); the sample view shows no markers. The Start marker appears once the start offset is above 0; the loop once END is above 0 (START = END is drawn as one line). START moves alone while below END and pushes END to START + 1 sample when it reaches it; END moves alone but stops at START. FL draws the loop red even with Use loop points off (Chan Sampler draws an unused loop grey, by request). Loading a new sample resets start offset and loop points to 0 unless the file has an embedded loop.

## Group (Cut / By) between instances

FL's channel Group is a host feature; Chan Sampler rebuilds it across its own instances (Source/CutGroups.*): every note start of an instance with Cut = n is posted on a process-wide lock-free bus with its host timeline position, and instances with By = n stop their voices there (release, or the declick fade without a volume envelope, as Cut self). An instance never cuts itself through its group (Cut self does that). Sample-exact when the cut instance is processed after the cutting one, otherwise at the start of its next block (`ChanSamplerRender cuttest dc6.wav`: 0 and 144 samples late at 512-sample blocks). With the transport stopped the cut happens at the next block. Bridged / sandboxed instances in other processes cannot see each other. FL's Time panel (gate / shift / swing / full porta) edits FL's note events and is not reproduced. FL has no Panning envelope (LFO only), so neither does Chan Sampler.

## Known gaps (diff / approximate)

- **Crossfade declick mode, and Transient (no bleeding) fade-out:** curve shapes not matched (−23 to −38 dB).
- **LPx2, and SVF LPx2 with resonance:** the 4th-order structure is not identified (possibly 2× oversampling). Approximated with two stages.
- **Positive decay tension** and the attack/note-off timing quirk (FL processes some envelope events once per tick, 212 samples at 130 BPM): −24 to −38 dB in those cases.
- **LFO:** see the LFO row above (in-FL nulls, nulltest/lfo_cases.py). Mod X / Y modulation now nulls (see Filter modulation). FL's pan envelope had no audible effect in CLI renders (amount ±128, sustain 128), so it is left as implemented. Arp TIME "Hold" is approximated as one bar.
- **Time stretching (superseded):** see the Stretch modes row. Earlier note: our own: tape-style Resample, plus a granular stretcher (Hann grains, 50% overlap) for Stretch / Pro / e3 / e2 / Auto, and transient resets for the slice modes. FL's Elastique is proprietary, so these won't null; engine tests verify duration and pitch behaviour.
- **Channel envelope and echo preset menus:** FL compiles them in and doesn't store them as data; not reproduced.
- **Not implemented** (FL-only or proprietary): keep-on-disk, resample-to-mix-rate, region / slice markers, cut groups across channels, gate / shift / swing, channel VOL/PAN/PITCH/RANGE/TRACK (host-side).

## UI layout evidence (B-FORM)

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
