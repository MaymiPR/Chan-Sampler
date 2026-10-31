# Changelog

## 1.4.1

Sound
- Slice map nulls against FL at MUL x2 and x1.5 (-52 to -83 dB, from -11 to
  -32 on a drum loop): slices start on a zero crossing, on the sample FL
  starts them, without a fade-in, and end with FL's fade instead of a hard
  stop. Below x1, a slice that reaches the end of the sample fades out where
  FL's does (a kick: -62 dB, from -19), and a slice the next one cuts off
  fades out under it as in FL instead of stopping (a drum loop: -53 dB, from
  -34). In stereo samples a slice starts at the zero crossing FL picks (the
  nearest in either channel).

Files
- Opus files (.opus) load.

Build
- Built on JUCE 9.0.4, with the sound unchanged (the null tests against FL
  give the same results as on JUCE 8). The Windows installer is a 64-bit
  program.

## 1.4.0

Sound
- SMP LENGTH: a segment shorter than FL's shortest is lengthened as FL does
  instead of held at the shortest (a 30-frame trim plays about 70 frames, as
  in FL, not 40), and a short trim at the very end of the sample now fades
  like FL's instead of ringing on for 10 ms. Short segments' end filter is
  closer to FL's (-47 to -59 dB in FL, from -2 to -27 dB).
- Reversed SMP LENGTH trims null against FL (-65 to -85 dB, from -46 to
  -64): the end filter sat a frame late on reversed segments.
- Arpeggiator: TIME's step lengths now match FL's at every setting (13 TIME
  values were a tick out), and GATE ends on whole ticks as in FL (a 7-tick
  step at 50% gate holds 3 ticks).
- e3 generic / Auto: stretched past x2 (MUL or TIME), more of the band goes
  through the phase vocoder, as in FL (above about x3.4, all of it); up to
  x2 nothing changes.
- Slice stretch now stretches each slice the way FL does (e3 generic per
  slice, as FL's engine does), instead of a waveform-overlap method, and the
  slice modes find their slices where FL finds them (on the sample where a
  hit starts; no slice in a sound that just fades in). Slice map's null
  against FL went from -12 to -26 dB on average.
- LPx2's level compensation follows FL's own form between the measured
  points (up to 9 dB closer to FL where the old table interpolated).

## 1.3.1

Fixes
- The echo panel's switches read "Ping pong" and "Fat mode" in full again
  (their last letter was cut off), ending just before their lights as in FL.

## 1.3.0

Look
- Every control is drawn as FL Studio draws it, pixel by pixel from
  captures of its channel settings window (the layout stays as it was):
  knobs with FL's bevel, body shading, value dot and full-turn arc in FL's
  per-control colours; the filter knobs' brushed body, wide bevel and tick
  marks; the round switches with their lit glow; drop-down fields, push
  buttons, digi wheels and the fine tune / Mid sliders; panel header tabs
  with FL's slant and dark edges; FL's sheet, panel and text colours and
  its 11.7 px / 7 px bold Segoe UI sizes; the sheet, LFO and effects icons
  traced from FL; the keyboard's 91 px octaves and 8 px black keys; the
  sample view's frame, 0.1 s grid at 13.6 px and plain-grey overview. Hover
  and pressed states follow FL too: knobs and the digi wheel lighten under
  the mouse (the wheel's triangles go), buttons and file buttons sink a
  pixel when pressed, nothing else reacts to hovering.
- Second pass from the same captures: the filter knobs' shading is read
  from FL's screen and interpolated (brushed body, bevel lit from above,
  shadow); the slider thumbs are fitted balls with FL's lit rim, glow and
  foot light over FL's tick strip; the digi wheels get FL's soft-edged box
  and its light from the left; push buttons (Reset, Cut self, the file
  buttons) get FL's frame, edges and sizes; the icons are traced from FL's
  anti-aliased pixels to a fraction of a pixel; knob captions sit on the
  pixel grid; the effects view gets FL's well behind BOOST, its small CLIP
  switch and the A / B reverb pill; the panels reach FL's right edge; the
  keyboard glides when scrolled and the preset arrows flash when clicked.
- Third pass: every label, knob and switch audited against FL's pixels and
  moved onto FL's positions (Tracking is FL's own panel and layout, the
  arpeggiator's directions are FL's symbols, its and the echo's rows sit
  where FL's do, Cut / By right-aligned, Fine tune at the right, the LFO
  knobs 10 px higher); hover highlights fade in and out on knobs, filter
  knobs (measured hovered shading), sliders, digi wheels, drop-down arrows
  and panel menu arrows.
- Fourth pass, with FL's animations captured frame by frame: hover
  highlights fade in over about 170 ms and out over 300 ms; drop-down and
  panel menu arrows turn from right to down (the envelope's grows FL's
  amber frame); the keyboard scrolls 22 px a wheel notch with FL's 42 ms
  glide; a pressed button darkens and recovers instantly and a toggled Cut
  self looks like an untoggled one, as in FL; the loop link is FL's icon on
  the caption row; START and END cannot be turned without a sample; digi
  numbers are centred between the box's edge and its triangles; Polyphony
  and Tracking share the Misc row equally.
- Fifth pass, from FL's gestures measured with the mouse driven: pressing
  and dragging a knob or slider add nothing to the hover highlight (no jump
  on the press, none on release); the filter knob's highlight fades out as
  FL's exponential; a hovered slider grows its groove and lifts its ball
  wherever the mouse is on it; switches toggle on mouse-down and their
  lights crossfade in over 180 ms and out over 235 ms (CLIP too); Ctrl
  drags a quarter as fast, Alt+click resets, a double-click no longer
  resets, a wheel notch is 8 px of drag (half with Ctrl, the other way on
  a slider), a press on a slider's track walks the ball towards the mouse;
  the drop-down arrows are FL's crisp 4 x 7 px triangles 9 px from the
  right edge on every list (the Mode list now shares the animated field),
  the fields' widths and the envelope and echo arrows re-measured; the
  effects view's icon is FL's traced glyph in FL's colours; the A / B
  pill's B is no longer cut off; the envelope and LFO displays redraw the
  moment a value changes (they followed a 20 Hz timer, up to 250 ms late).
- The Keyboard panel's Fine tune slider now follows the Reset button, with
  the right-click hint after it (asked for; FL keeps it at the right).

Added
- Undo and redo of the user's edits: a drag is one step recorded on release,
  switches, lists, menus, panel presets and a loaded, replaced or removed
  sample are steps, edits that change nothing are skipped, 100 steps are
  kept. Ctrl+Z undoes, Ctrl+Y (or Ctrl+Shift+Z) redoes, also in FL Studio:
  JUCE's VST3 editor ignored the keys a host offers it (patched at configure
  time, cmake/PatchJuceVst3Keys.cmake), and with nothing to undo or redo the
  key goes on to the host. The two halves of a field beside the presets do the
  same and name the step. Sample steps tell the host the state changed.
- Short samples (FL's hi-hats and anything under about 0.4 s) draw at their
  full height again, spiky as in FL: zoomed in, the line runs through each
  pixel's lowest and highest sample instead of their average, which had
  shrunk a hat to a fifth of its height. The sample view's grid stops at
  0.1 s lines, as FL's does.
- The filter knobs turn: under the mouse, the five lobes of the collar round
  the cap turn with the value as in FL (they stayed put); the envelope's
  tension knobs have no value dot and draw their arc from the top (the
  volume release tension's ran from the bottom), and their TENSION captions
  are letter-spaced as in FL.
- FL Studio: the space bar plays and stops again after touching a knob,
  while holding one and after typing a value. A click moved the Windows
  focus into the plugin's own window, where FL stops acting on the keys the
  plugin leaves; the focus now goes back to FL's window for the plugin after
  a click (not while a value is typed or a menu is open), so FL offers every
  key to the plugin, the plugin takes Ctrl+Z / Ctrl+Y and FL the rest.
- Sixth pass, measured in FL: a wheel notch moves the value at once and
  the knob or ball eases to it (about 36 ms, exponential); the list and menu
  arrows rotate rigidly a quarter turn (160 ms in, 200 ms back, no wait) and
  stay turned while their menu is open; the effects view's icon is refitted
  to FL's pixels (pointed cap, hairline walls, two-stroke filament, stand).

Fixed
- SLIDE greyed and ungreyed one click behind Porta / Mono.

Closer to FL
- e3 generic, Auto, e2 generic and e2 transient stretch at FL's internal
  ratio grid and resample the rest, as FL does: e3 generic's in-FL pitch
  cases null about 2 dB deeper on average (most on sustained tones such as
  a Rhodes), and when squeezing, the treble now ripples like FL's.
- e3 generic splices its upper band at irregular, FL-like periods on noisy
  sounds (hats, cymbals) instead of every 256 samples, which added a
  periodic flutter.
- e3 generic and Auto restart the phases of the band below 1.485 kHz after
  an attack the way FL does (from that band's loudest point a little after
  the attack), so sustained sound after a hit lines up with FL's: the
  in-FL pitch cases null 0.2 dB deeper on average and a sung vowel up a
  fifth 10 dB deeper; the same vowel down 3 or up 12 semitones nulls worse,
  as FL finds a slow swell's start earlier than Chan Sampler does.

Fixes
- The volume, filter and pitch envelopes' hold and decay start on FL's tick
  after an attack (FL's attack sometimes lasts a tick longer, from its
  rounding): the default envelope nulls to about -80 dB at any tempo, from
  -45 to -54 dB.

## 1.2.3

New
- The sample menu's keyboard shortcuts, as in FL: Ctrl+C / Ctrl+V (Cmd on
  macOS) copy and paste the sample's audio while the window is active, and
  the underlined letters choose an entry while a menu is open (the knob menus'
  too).

Faster
- Stretch and e2 transient use about 9% less CPU, Stretch Pro about 6%, e2
  generic and e3 generic 3-5%, on stereo samples as well as mono; the sound
  is bit-for-bit the same.

Fixes
- Arpeggiator TIME "Hold" now works as in FL: the arpeggio's first note
  plays and holds until every key is up (it used to step once a bar).
- The spectrum view is closer to FL's on real sounds: it was about 2 dB
  too dark.
- SMP LENGTH trims now end on the frame FL ends on (they were often a frame
  long, from rounding), and a trim that lands in silence fades where the
  sound stops, as in FL: the trim cases null about 15 dB deeper.
- The LPx2 filter near maximum resonance matches FL at the start of each note:
  its level settles over two ticks there, as in FL, instead of one.

## 1.2.2

Faster
- Each Chan Sampler uses about 4 MB of memory instead of about 130 MB until
  a stretch mode is used (the time stretchers are made the first time one
  is needed): ten instances in Resample went from about 1.3 GB to 40 MB.
- Less CPU: Resample and Slice map about a quarter less (the interpolator's
  sines), and Stretch, Stretch Pro and e2 transient about a quarter less on
  mono samples (one channel's spectral work, copied). The audio thread no
  longer builds strings to read the parameters.

Fixes
- Loading a project or preset no longer keeps the sample that was loaded
  before when the project has none, or its file is missing or unreadable:
  no sample plays, the editor shows the missing file's name and path, the
  project keeps the path when saved again, and the sample reloads once the
  file is back. An FL preset (.fst) whose sample can't be found or read
  likewise loads without the previous sample.
- Saving the sample, an envelope preset or a preset tells you when it fails
  (a full disk, a read-only folder) instead of failing silently; the
  preset bar keeps its old name then. Opening a file that isn't a Chan
  Sampler preset says so, and an unreadable envelope preset too.
- The locate button's tooltip names Finder on macOS and the file's folder
  on Linux (it said Explorer everywhere).
- Switching a playing note from Resample to a stretch mode no longer reads
  from a stretcher left over from an earlier note; that note carries on
  resampled, and the next note is stretched.
- In Resample, Stretch and Stretch Pro the sample view shows the file as it
  is, whatever PITCH, MUL and TIME say, as FL does (it redrew the
  stretched sound). The e3, e2, slice and Auto modes still show the
  sample as it plays, as in FL.
- Loop points saved in a compressed WAV (FL's own library samples, Ogg
  inside a WAV) are read: such a sample now loops like in FL instead of
  playing once. Its ACID tempo is read too.
- Stretched far (MUL x4, or TIME stretching as much), drum hits no longer
  sound early in the stretch modes (they came 20-35 ms early at x4 and up
  to 130 ms beyond); they now land within a few milliseconds.
- Stretch Pro sounds closer to FL's: it now stretches like Stretch (as
  FL's two modes do) and keeps the voice's formants when pitched, so drums
  smear much less (distance to FL's renders 2.92 / 4.34 -> 1.94 / 3.77 dB;
  a drum loop at x4: 7.4 / 11.0 -> 3.4 / 3.4).
- Loops in the e3 and e2 modes play like FL's: FL stretches the sample
  ahead of time and repeats the stretched loop, where Chan Sampler stretched
  the loop again on every pass (a looped Rhodes: 10-14 dB from FL's
  spectrum, now 2.5-4). Loops up to about 0.7 s long once stretched.
- Loading a sample sets the root note it stores, as FL does: the WAV's
  "inst" note, else its "smpl" root note, else C5 (FL's piano and bass
  multisamples, for example, now play in tune). Projects and presets keep
  their own root note; factory presets keep the sample's.
- Stretched more than 20 times (TIME on a short sample: three bars of a
  0.24 s bass is x25), the stretch modes ran out of sample early and fell
  silent, which the sample view showed as a shortened waveform; they now
  last the whole TIME, as FL's do (a 707 kick at TIME 16 beats: 7.38 s in
  both).
- The sample view's bit-depth badge reads 32 while it shows a stretched
  copy (e3 / e2 / slice / Auto), as FL's does.
- The TIME knob turns like FL's: one 16th step per pixel, whole steps only,
  and a short hold at every bar. It moved three times as fast and in
  fractions of a step, so a small turn squeezed a sample into a sliver of
  a beat where FL's lands on whole steps.

## 1.2.1

New
- Click CHAN SAMPLER in the top bar for a menu in FL's style: the version,
  About Chan Sampler (version, plugin format, license, credits), the project
  on GitHub, Report an Issue and the license.

Fixes
- With more than one Chan Sampler in a project, the loop start / end values
  the DAW shows (automation, generic editor) could be counted against
  another instance's sample; each now uses its own.

Downloads
- Each release has the Windows installer, the macOS installer and the Linux
  tarball (now with the version in its name); the Windows and macOS zips are
  gone. Every download includes the licenses of the third-party software in
  Chan Sampler (THIRD-PARTY-NOTICES.txt).

Other
- Every build is now checked to open a project saved by 1.2.0 with every
  setting unchanged; each release from here on adds its own.
- No compiler warnings on Linux (deprecated JUCE calls replaced, unused code
  removed).
- For contributors: changes are built and tested automatically, a
  contributor guide (CONTRIBUTING.md), the null-test tools in folders with a
  generator for the stretch-mode test signals, LF line endings.

## 1.2.0

Closer to FL's Sampler
- Every stretch mode was measured against FL's renders (clicks, noise, a
  kick, a Rhodes, a voice and two drum loops at -700 / +700 cents and x1.5 /
  x0.667) and rebuilt to match:
  - e3 mono and e2 mono splice the sound one pitch period at a time, as
    FL's do, and keep a voice's formants when PITCH moves.
  - e2 speech uses the same engine, with the pitch resampled like FL's.
  - Stretch, e2 generic and e2 transient handle attacks like e3; e2 generic
    plays its highs as pieces of the sound, as FL's does.
  - Every one of these modes is closer to FL's renders, by up to half (e3
    mono's spectrum 6.4 -> 3.8 dB from FL's, e2 transient's envelope 5.0 ->
    2.5 dB).
- e3 generic / Auto with PITCH: attacks are timed on FL's grid (all 128
  measured noise bursts land where FL puts them), counted from where an
  attack is under way, so a ramping drum hit or a swelling voice is timed
  like FL's. When stretching, an attack's low end is as much louder as FL
  plays it at that point.
- A note that starts softly lines up like FL's in Stretch, Stretch Pro, e2
  generic and e2 transient (each of FL's modes has its own latency there).
- Every knob's drag distance is now measured in FL.

Faster
- The stretch modes use about half the CPU, with the same sound: e3 generic
  / Auto about 16% of one core for 32 voices (1.1.0: 37%), Stretch Pro 25%
  (36%), the mono and speech modes about 5%. The stretcher has its own,
  faster FFT, and e3's spectral part only works on the band it keeps.

Fixes
- e3 mono and e2 mono: clicks could land up to ~190 samples off, or drop
  out when squeezing; every click now lands on time at full level.

Other
- Regression tests for the stretch modes: e3's attack timing, band split
  and pitched notes' start and length, every mode's alignment of a soft
  start, the mono modes' clicks and formants, the FFT, and a stress test of
  every mode at extreme, changing ratios.

## 1.1.0

New formats and platforms
- CLAP plugin (Windows, macOS, Linux), included in the installers.
- Linux builds: VST3, CLAP and the standalone app (x86-64, glibc 2.35 or
  later).

Closer to FL's Sampler
- e3 generic and Auto are built like FL's e3 (measured from FL renders, see
  nulltest/E3_NOTES.md): a spectral stretch below 1.485 kHz and
  pitch-synchronous grains of the sound above it. Attacks are timed like
  FL's (the first sample near an attack's peak lands on time) and held as
  long as FL holds them. Against FL's renders of a kick, a Rhodes, a voice
  and two drum loops at -1200 ... +1200 cents, nulls improved from -2.0 to
  -2.9 dB, envelopes are 18% closer and spectra 18% closer. A true null is
  not possible: FL's engine (elastique) is proprietary.
- Pitched drums keep their length: a kick an octave down no longer rings
  twice as long, and attacks are no longer smeared or doubled.
- The root note keyboard is FL's: all 128 notes behind a scroll bar (arrows,
  thumb, mouse wheel), FL's key colours and size, opening on FL's view.
- Knobs and sliders feel like FL's: FL's drag distance for each control, the
  hold at a two-sided control's centre (and PITCH's semitones), and a hidden
  cursor that drags past the screen's edges.
- The sample view draws the sample as PITCH, MUL and TIME play it, redrawn
  once a knob is let go; PITCH switches Resample to Auto when let go, and
  Auto reads e3 generic.
- Multiband waveform colours show a drum's click in its own colour, as FL
  does (707 Kick starts blue).

Fixes
- Stretched notes were up to 23 ms early or late depending on the pitch;
  steady sound in the spectral modes (e2, Stretch Pro) ran a few ms early.
- Pitched-down notes in the spectral modes could start late or fade in.

Other
- e3 generic uses more CPU than before (about 37% of one core for 32
  voices, was 14%).
- Releases list SHA-256 checksums; the install notes explain the security
  warnings for the unsigned downloads (SmartScreen, macOS's Open Anyway).

## 1.0.0

First stable release. Saves from 0.1.0 load unchanged: every parameter, the
sample and the window size (checked with `ChanSamplerRender compatstate`).
From now on, parameter IDs and the plugin codes stay fixed so projects keep
loading.

Sound (all null-tested against FL Studio):
- Filter modulation (envelopes, LFOs, Mod X / Y) matches FL, including LPx2 and
  SVF LPx2.
- All six declicking modes match FL, including Crossfade's fades and
  Transient (no bleeding)'s fade-out (a sweeping high-pass, not a volume fade).
- Envelopes match FL at any tempo, not only 130 BPM: they run on the song's
  ticks as FL's do, with FL's own stage times (re-measured at six tempos), and
  positive decay tension has FL's shape.
- Loops, echo Mod X / Y and fat mode, velocity / key tracking, mono / porta
  glide match FL. Max polyphony is ignored, as in FL's renders.
- FLAC, Ogg and MP3 samples match FL's decoding.
- Stretch Pro and the other phase-vocoder modes use half the CPU (Stretch Pro
  at 64 voices: 118% to 54% of a core) and stay steadier on long notes.

New:
- FL Studio Sampler presets (`.fst`): drop or import to get the sample and every
  setting.
- Content panel: Load regions / Load slice markers from WAV files (cue / ACID).
- Preset menu: save, load and browse `.cspreset` files, import `.fst`.
- Factory presets (settings only, keep the loaded sample): Chop, Pluck, Swell,
  Filter sweep, Wobble 1/8, Tremolo, Vibrato, 808 glide, Echo ping-pong 1/8,
  Reverse. A saved preset without a sample also keeps the loaded one.
- The sample in and out of other programs: drag the waveform out of the view
  into a DAW, an audio editor or Explorer / Finder; Copy / Paste through the
  system clipboard (the file and its audio); a sample file changed on disk (say,
  saved from the audio editor it was dragged into) reloads with the settings
  kept.
- Sample view right-click menu in FL's order: Reload, Save as, Locate file,
  Copy, Paste, Detect tempo, Get properties, Spectrum view,
  Multichannel waveform view, Colorful waveforms (Multiband / Color map).
- Detect tempo: FL's Tempo detection dialog (estimate, ranges, type in BPM or
  beats, project tempo, no tempo); TIME > Autodetect... sets TIME to the
  sample's length in beats, so a loop plays at the project's tempo. A WAV's
  ACID tempo is read when loaded.
- Panel menus as in FL: the echo delay's presets (Echo - classic / ping pong,
  Fat - detuned / more detuned / detuned + time offset / 8 voices), and the
  envelope's presets (Save as, then pick from the list).
- Resizable window, sharp on Retina / high-DPI screens.

Look (measured from FL's screen):
- Sample view: FL's colours, grid, smooth waveform, format badge (16 / 32 with
  mono / stereo icon), file markers.
- Spectrum view modelled on FL's (sharp in time and frequency, FL's palette).
- Colourful waveforms measured on FL's own drum, loop and vocal samples.
- FL-styled menus, message dialogs, Properties sheet and file buttons.
- Tracking opens on the Key tab; Content shows only the options that do
  something.

Robustness:
- Offline rendering (export / bounce) never starts on an outdated sample, also
  right after loading a project or changing a precomputed setting.
- Changing the host's sample rate mid-session keeps every sample's pitch.
- Damaged or hostile files can't crash or hang the plugin (1306 corrupted
  files tested); a file that is too long or damaged gets a clear message.
- Long samples use a third of the memory (a 30-minute stereo file: 3.1 GB to
  0.8 GB).
- The spectrum and colourful waveforms look the same at 96 kHz as at 44.1 kHz.

Fixes:
- Loading a sample with loop points sets the loop markers but no longer
  switches Use loop points on (FL leaves the switch as it was).
- Trackpad zoom in the sample view no longer triggers by accident when clicking
  (macOS).
- macOS font widths match the Windows layout.

Builds: Windows installer and macOS .pkg (plus zips) with VST3, AU (macOS) and
the standalone app, validated with pluginval (strictness 10) and `auval`.
License: AGPLv3.

## 0.1.0

First public build.
