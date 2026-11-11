#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <type_traits>

// Every measured, fitted and design constant of the DSP, in one struct so the offline renderer (tools/CsRender.cpp) can
// override any of them by name while fitting (tune:key=value on its command line, or tuning=<file>); the plug-in uses
// the defaults. An array's entries are keys of their own: the name and the index (interpWindow0 ...). Each group names
// the MEASUREMENTS.md section or the script that decided it. What stays in the code: the encodings of FL's knobs (raw
// ranges), its tick grid (96 per beat), the whole-number terms of a measured law's shape, array sizes and numeric
// guards, and the large measured tables, which are headers of their own (TrimTail.h, Chords.h). Add a field and a line
// in forEach () per constant.
namespace cs::dsp
{
struct Tuning
{
    // Playback interpolation (MEASUREMENTS.md "Measured laws"; nulltest/fit_window.py): the sample interpolator's
    // window, a 7-term cosine series over 15 input samples each side. Read once, when its table is first built.
    double interpWindow[7] = { 0.362684569, 0.509910674, 0.132162708, -0.006084749,
                               0.001926633, -0.000879890, 0.000296751 };

    // Rate conversion on load (MEASUREMENTS.md "Measured laws", Sample-rate conversion;
    // nulltest/measure_src_kernel.py): an 88-tap windowed sinc, the window a 9-term cosine series.
    int srcHalfTaps = 44;
    double srcWindow[9] = { 0.498387586, 0.490065433, 0.009996963, 0.002969411, -0.002640382,
                            0.002159460, -0.001503978, 0.000750707, -0.000168888 };

    // Envelopes (MEASUREMENTS.md "Measured laws", Envelope rows; nulltest/probe_env_tick.py, probe_env_time.py,
    // probe_env_hold.py): the fastest move from 0 to 1, in samples at 44.1 kHz; the time knob's measured times (raw
    // value, ms) and the fit below the first point, t = a e^(b p) - c, whose c is also the log offset between the
    // points.
    double envFullRangeSamples = 389.5;
    float envTimeKnots[28] = { 1000, 1500, 2500, 5000, 7500, 10000, 12500, 15000, 17500, 20000, 22500, 25000, 27500, 30000,
                               32500, 35000, 37500, 40000, 42500, 45000, 47500, 50000, 52500, 55000, 57500, 60000, 62500, 65536 };
    float envTimeMs[28] = { 7.812f, 11.088f, 19.085f, 44.334f, 76.798f, 119.861f, 175.285f, 247.253f, 341.0f, 463.086f, 622.265f,
                            828.828f, 1098.203f, 1448.203f, 1904.46f, 2498.024f, 3270.312f, 4276.015f, 5584.774f, 7288.515f,
                            9504.701f, 12391.015f, 16148.047f, 21036.328f, 27396.947f, 35677.991f, 46458.69f, 63999.14f };
    double envTimeOffsetMs = -63.6541;
    double envTimeLowA = 0.0646133, envTimeLowB = 6.8996, envTimeLowC = 0.0636541;
    // Envelope tension (raw -128 ... 128) to the curve exponent, fitted to FL renders.
    float tensionKnots[10] = { -128.0f, -101.0f, -96.0f, -64.0f, -32.0f, 0.0f, 32.0f, 64.0f, 96.0f, 128.0f };
    float tensionK[10] = { 6.19f, 4.62f, 4.15f, 2.13f, 0.56f, 0.0f, -0.58f, -2.18f, -4.29f, -6.45f };

    // LFOs (MEASUREMENTS.md "Measured laws", LFO; nulltest/cases_lfo.py, measure_lfo_speed.py): the period in ticks at
    // 130 BPM, a base^pos - c (the predelay on the same law), at least lfoMinPeriodTicks; the pulse's edge over 4
    // ticks.
    double lfoPeriodA = 13.5755, lfoPeriodBase = 982.5, lfoPeriodC = 13.2745;
    double lfoMinPeriodTicks = 3.0;
    double lfoEdgeStep = 0.27205, lfoEdgeAtThree = 0.81615, lfoEdgeLast = 0.18385;

    // Portamento (MEASUREMENTS.md "Tracking, root note, polyphony"; nulltest/cases_untested.py): the glide in ticks,
    // round (a base^(raw / 1660) - c).
    double slideTicksA = 0.92049, slideTicksBase = 209.414, slideTicksC = 0.15;
    // Arpeggio time (MEASUREMENTS.md "Measured laws", Arpeggiator; measurements/arp_ticks_dense.txt,
    // measure_arp_period.py): half ticks round (a (base^(raw / 1000) - 1)).
    double arpTicksA = 1.84558, arpTicksBase = 24.99798;

    // Levels: FL's volume law (base^x - 1) / (base - 1), and the note velocity curve, the same law of v / unity; the
    // velocity middle value's curve (base^(m / unity) - 1) / (base - 1) (MEASUREMENTS.md "Measured laws", Velocity and
    // Levels; "Tracking, root note, polyphony").
    float volumeLawBase = 11.0f;
    float velocityBase = 11.0f, velocityUnity = 100.0f, velocityMidBase = 6.5f;

    // The channel filter (MEASUREMENTS.md "Measured laws", Filter rows, and "Filter modulation"; nulltest/cases_lfo.py,
    // filter_lib.py): the Fast LP's f = (base^u - 1) / (base - 1), linear below the knee; the resonance r at raw 0, 32
    // ... 640; the biquads' centre frequency (Hz at Mod X raw knots) and its exponential rise past the last knot, their
    // Q (at Mod Y raw knots), the 3.0 rad cap; LPx2's level compensation 1 - Q (a - w) b; the SVF's f base and damping
    // of each stage.
    double onePoleBase = 601.0, onePoleKnee = 1.0 / 64.0;
    double resonanceTable[21] = { 0.0, 0.1815353218, 0.3301693593, 0.4518676647, 0.5515086796, 0.6330945280,
                                  0.6998893266, 0.7545816900, 0.7993627980, 0.8360268703, 0.8660461431,
                                  0.8906255328, 0.9107490105, 0.9272267638, 0.9407179614, 0.9517637502,
                                  0.9608078304, 0.9682128425, 0.9742758112, 0.9792400659, 0.9833044645 };
    double biquadF0Knots[30] = { 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184,
                                 192, 200, 208, 216, 224, 232, 240 };
    double biquadF0Hz[30] = { 202.3, 260.4, 328.2, 406.2, 495.2, 596.3, 710.8, 840.3, 986.5, 1151.6, 1338.3, 1549.4, 1788.1, 2058.1,
                              2363.6, 2709.2, 3100.0, 3542.0, 4041.8, 4606.6, 5245.0, 5966.5, 6782.1, 7704.2, 8747.1, 9927.0, 11262.1,
                              12772.8, 14481.6, 16412.3 };
    double biquadF0Rise = 3.99;
    double biquadQKnots[30] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184,
                                192, 200, 208, 216, 224, 256 };
    double biquadQ[30] = { 0.1000, 0.1319, 0.1660, 0.2035, 0.2440, 0.2876, 0.3345, 0.3850, 0.4393, 0.4980, 0.5615, 0.6302, 0.7046,
                           0.7851, 0.8722, 0.9662, 1.0676, 1.1769, 1.2946, 1.4213, 1.5580, 1.7056, 1.8651, 2.0380, 2.2253, 2.4283,
                           2.6475, 2.8827, 3.1322, 4.3697 };
    double biquadMaxRadians = 3.0;
    double lpx2CompA = 3.19702, lpx2CompB = 0.046912;
    double svfBase = 26.0, svfDamping = 0.8, svfDamping2 = 0.6;

    // The voices (MEASUREMENTS.md "Control timing" and "Measured laws", Declick rows): the control block, the pitch
    // modulation's lead (the envelope's and the LFO's, in samples) and depth (semitones at full), the Transient modes'
    // in-fade (samples at 44.1 kHz), each declicking mode's in- and out-fade (ms, the order of FL's menu).
    int controlBlockSamples = 32;
    double pitchEnvLeadSamples = 13.0, pitchLfoLeadSamples = 2.5, pitchModSemitones = 1.28;
    double transientInSamples = 175.0;
    float declickInMs[6] = { 0, 4, 4, 20, 100, 200 };
    float declickOutMs[6] = { 10, 10, 20, 20, 100, 200 };

    // Echo delay (MEASUREMENTS.md "Echo delay"): the feedback knob's gain scale, the level below which echoes stop.
    float echoFeedScale = 2.0f, echoMinGain = 1.0e-3f;

    // The declick curves (MEASUREMENTS.md "Measured laws", Declick rows; DeclickCurves.h; nulltest/cases_declick.py):
    // Transient's out-fade, a high-pass whose L (n) = c (e^(b n) - 1), its length and closing gain (from gainStart,
    // every gainStep samples); slice map's slice ends; the Crossfade mode's fades, degree-10 polynomials over their
    // lengths.
    double declickTransientB = 0.0228, declickTransientC = 0.002015;
    int declickTransientOutLength = 440, declickTransientGainStart = 330, declickTransientGainStep = 5;
    float declickTransientGain[23] = { 0.9991f, 0.9958f, 0.9958f, 0.9958f, 0.9958f, 0.9927f, 0.9904f, 0.9890f, 0.9787f, 0.9670f,
                                       0.9652f, 0.9652f, 0.9621f, 0.9477f, 0.8931f, 0.8169f, 0.6981f, 0.5102f, 0.3225f, 0.1524f,
                                       0.0978f, 0.0098f, 0.0f };
    int declickSliceOutLead = 942;
    double declickSliceOutSpeed = 0.5;
    int declickSliceCutLead = 501;
    int declickCrossfadeInLength = 8750, declickCrossfadeOutLength = 8781;
    double declickCrossfadeInPoly[11] = { -6.876493618e-06, 0.0108449024, 5.072820867, 0.6442403826, -14.64723224, -20.27575314,
                                          125.7882847, -202.5588682, 163.4490074, -68.31700329, 11.83346862 };
    double declickCrossfadeOutPoly[11] = { 0.9999750188, -0.005822820603, -0.751950526, -1.28526727, 6.712290069, -31.06397872,
                                           79.25906093, -129.9780921, 133.2606847, -73.45669163, 16.30997087 };

    // Sample Start / Length (MEASUREMENTS.md "Measured laws", Sample start / Length; nulltest/cases_trim_short.py,
    // measure_trim_matrix.py): FL's shortest segment in frames; the end filter's model for segments shorter than its
    // table (TrimTail.h): window, sweep start, rate and offset, closing fade.
    int trimMinFrames = 40;
    int trimTailWindow = 178;
    double trimTailSweepStart = 5.01e-5, trimTailSweepRate = 9.87, trimTailSweepOffset = 1.3;
    int trimTailClosing = 44;

    // Precomputed effects (MEASUREMENTS.md "Measured laws": Boost, EQ, Ring mod, Fade, Crossfade, Precomputed reverb;
    // nulltest/cases_offline.py, fit_reverb_*.py): the boost's gain per knob, the tilt EQ's feedback, the ring
    // modulator's rate (raw + offset) / divisor cycles per sample, the fades' slope, the loop crossfade's share of the
    // length (with and without loop points).
    float boostGain = 16.0f;
    double tiltEqFeedback = 0.75;
    double ringModOffset = 8.0, ringModDivisor = 5760.0;
    float fadeSlope = 4.0f;
    double crossfadeLoopShare = 4.0, crossfadeAutoShare = 2.0;
    // The reverb: comb delays and gains of each type, allpass delays (B's second differs on the right), the allpass
    // gain, the dry level, B's output pole, the tail added and its cap (s), the trim level at the end.
    int reverbCombsA[4] = { 1777, 1847, 1993, 2137 };
    int reverbCombsB[6] = { 2467, 2767, 3217, 3533, 3877, 4127 };
    double reverbCombGainA = 0.15, reverbCombGainB = 0.16201 / 6.0;
    int reverbAllpassA[3] = { 43, 127, 389 };
    int reverbAllpassB[5] = { 67, 97, 101, 197, 599 };
    int reverbAllpassBRight = 79;
    double reverbAllpassGain = 0.7, reverbDry = 0.7, reverbPoleB = 0.7;
    double reverbTailSamples = 8000.0, reverbMaxTailSeconds = 20.0;
    float reverbTrimLevel = 0.00197f;

    // Transients (MEASUREMENTS.md "Slices"; nulltest/measure_slice_starts.py): a block's energy over ratio x the four
    // before it plus floor x block, above peakShare of the loudest block; the hit's level share that starts it, how far
    // back the level before it is read (blocks), the shortest slice. The stretcher's attacks use the same rule in
    // single precision.
    int onsetBlock = 128;
    double onsetRatio = 8.0, onsetFloor = 1.0e-4, onsetPeakShare = 0.01;
    float onsetHitShare = 0.03f;
    int onsetLookBackBlocks = 8, onsetMinSpacing = 2048;
    float attackRatio = 8.0f, attackFloor = 1.0e-4f, attackPeakShare = 0.01f;

    // Time stretching (MEASUREMENTS.md "Time stretching", "The grain modes' periods" and "e3 generic in detail"): the
    // vocoder's frame at 44.1 kHz and transient threshold; the grains' frame, and per grain mode the period used when
    // none is found and how long a note's start plays 1:1; each mode's soft start (samples at x1.5 and x0.667, low band
    // and high band); the ratio grids; the attack span; e2 speech's splice length; the grains' period range, estimate
    // window, offset and reuse distance, the lag pull, score floor, dip and peak share of the period search, the
    // spacing of their onsets; e3's split (Hz) and its rise past e3SplitRiseFrom as (stretch - 1)^power; the low band's
    // level while an attack holds.
    double vocoderFrame = 2048.0;
    float vocoderTransientThreshold = 1.5f;
    double grainFrame = 256.0;
    double e3MonoFallback = 256.0, e3MonoHold = 384.0, e2MonoFallback = 128.0, e2MonoHold = 0.0;
    double e2SpeechFallback = 384.0, e2SpeechHold = 384.0;
    double stretchSoft15 = 57.0, stretchSoft067 = 405.0, stretchProSoft15 = 12.0, stretchProSoft067 = 267.0;
    double e2TransientSoft15 = -50.0, e2TransientSoft067 = 30.0, e2GenericSoft15 = -157.0, e2GenericSoft067 = -30.0;
    double e2GenericHfSoft15 = 112.0, e2GenericHfSoft067 = 96.0, e3HfSoft067 = 60.0;
    int e3RatioSteps = 512, e2GenericRatioSteps = 256;
    double attackSpan = 400.0;
    double e2SpeechSplice = 390.0;
    double grainMinPeriod = 40.0, grainMaxPeriod = 512.0, grainWindow = 512.0, grainEstimateOffset = 8.0, grainEstimateReuse = 512.0;
    double grainLagPull = 0.04, grainScoreFloor = 0.3;
    float grainDip = 0.5f, grainPeakShare = 0.9f;
    int grainOnsetSpacing = 1024;
    double e3SplitHz = 1485.0, e3SplitRiseFrom = 2.0, e3SplitRisePower = 3.0;
    float e3HoldGainStretch = 1.2f, e3HoldGainSqueeze = 1.1f;
    // e3's attacks (MEASUREMENTS.md "e3: attack timing", "e3: attack gain", "e3: low band after an attack"): where the
    // low band restarts after an attack (from, span) and the peak timing (reach, lead); the attack's shift by octaves
    // of PITCH (-1, -0.5, 0, 0.5, 1); the switch grid (offset and gap and their rise over log2 of the stretch, from
    // attackAlphaLow over attackAlphaRange; with PITCH the pitched offset and gap and the gap's change per octave), the
    // share of the peak power that starts and times an attack, the late-peak distance and the attack's level (base +
    // slope x the grid phase); how long an attack plays 1:1 by octaves of the stretch.
    double rephaseFrom = 660.0, rephaseSpan = 512.0, rephasePeakReach = 469.0, rephasePeakLead = 20.0;
    double attackShiftSamples[5] = { -8.3, -2.9, 0.0, 2.05, 3.35 };
    double attackOffset = 364.0, attackOffsetRise = 46.0, attackGap = 144.0, attackGapRise = 96.0;
    double attackAlphaLow = 0.585, attackAlphaRange = 1.17;
    double attackOffsetPitched = 360.0, attackGapPitched = 136.0, attackGapPerOctave = 6.0, attackGrid = 512.0;
    float attackUnderShare = 0.25f, attackPeakTiming = 0.9025f;
    double attackLatePeak = 64.0, attackGain = 1.13, attackGainSlope = 0.0019;
    double holdOctaves[6] = { -1.0, -0.585, -0.322, 0.322, 0.585, 1.0 };
    double holdSamples[6] = { 336.0, 368.0, 528.0, 704.0, 1000.0, 1150.0 };
    // Stretch Pro's formants: the cepstral lifter (coefficients at 44.1 kHz) and the correction's limit (log gain).
    double formantLifter = 64.0;
    float formantMaxGain = 6.0f;

    // Detect tempo (ours, checked against FL's estimates; nulltest/probe_tempo.py): the onset hop at 44.1 kHz, the
    // longest stretch read (s), the log compression, the autocorrelation weights of 2, 4 and 1/2 beats, the scan step
    // (BPM), and the loop lengths' preference (the share of the best score, the weights of powers of two and of
    // multiples of 4 beats).
    double tempoHop = 512.0, tempoMaxSeconds = 120.0;
    float tempoCompression = 100.0f;
    double tempoWeightDouble = 0.5, tempoWeightQuad = 0.25, tempoWeightHalf = 0.25, tempoStep = 0.25;
    double tempoLoopShare = 0.75, tempoPowerOfTwoWeight = 1.25, tempoBarWeight = 1.1;

    // Every constant by name: f (name, field) for each field above.
    template <typename F>
    void forEach (F&& f)
    {
        f ("interpWindow", interpWindow); f ("srcHalfTaps", srcHalfTaps); f ("srcWindow", srcWindow);
        f ("envFullRangeSamples", envFullRangeSamples); f ("envTimeKnots", envTimeKnots); f ("envTimeMs", envTimeMs);
        f ("envTimeOffsetMs", envTimeOffsetMs); f ("envTimeLowA", envTimeLowA); f ("envTimeLowB", envTimeLowB);
        f ("envTimeLowC", envTimeLowC); f ("tensionKnots", tensionKnots); f ("tensionK", tensionK);
        f ("lfoPeriodA", lfoPeriodA); f ("lfoPeriodBase", lfoPeriodBase); f ("lfoPeriodC", lfoPeriodC);
        f ("lfoMinPeriodTicks", lfoMinPeriodTicks); f ("lfoEdgeStep", lfoEdgeStep); f ("lfoEdgeAtThree", lfoEdgeAtThree);
        f ("lfoEdgeLast", lfoEdgeLast); f ("slideTicksA", slideTicksA); f ("slideTicksBase", slideTicksBase);
        f ("slideTicksC", slideTicksC); f ("arpTicksA", arpTicksA); f ("arpTicksBase", arpTicksBase);
        f ("volumeLawBase", volumeLawBase); f ("velocityBase", velocityBase); f ("velocityUnity", velocityUnity);
        f ("velocityMidBase", velocityMidBase); f ("onePoleBase", onePoleBase); f ("onePoleKnee", onePoleKnee);
        f ("resonanceTable", resonanceTable); f ("biquadF0Knots", biquadF0Knots); f ("biquadF0Hz", biquadF0Hz);
        f ("biquadF0Rise", biquadF0Rise); f ("biquadQKnots", biquadQKnots); f ("biquadQ", biquadQ);
        f ("biquadMaxRadians", biquadMaxRadians); f ("lpx2CompA", lpx2CompA); f ("lpx2CompB", lpx2CompB);
        f ("svfBase", svfBase); f ("svfDamping", svfDamping); f ("svfDamping2", svfDamping2);
        f ("controlBlockSamples", controlBlockSamples); f ("pitchEnvLeadSamples", pitchEnvLeadSamples);
        f ("pitchLfoLeadSamples", pitchLfoLeadSamples); f ("pitchModSemitones", pitchModSemitones);
        f ("transientInSamples", transientInSamples); f ("declickInMs", declickInMs); f ("declickOutMs", declickOutMs);
        f ("echoFeedScale", echoFeedScale); f ("echoMinGain", echoMinGain);
        f ("declickTransientB", declickTransientB); f ("declickTransientC", declickTransientC);
        f ("declickTransientOutLength", declickTransientOutLength); f ("declickTransientGainStart", declickTransientGainStart);
        f ("declickTransientGainStep", declickTransientGainStep); f ("declickTransientGain", declickTransientGain);
        f ("declickSliceOutLead", declickSliceOutLead); f ("declickSliceOutSpeed", declickSliceOutSpeed);
        f ("declickSliceCutLead", declickSliceCutLead); f ("declickCrossfadeInLength", declickCrossfadeInLength);
        f ("declickCrossfadeOutLength", declickCrossfadeOutLength); f ("declickCrossfadeInPoly", declickCrossfadeInPoly);
        f ("declickCrossfadeOutPoly", declickCrossfadeOutPoly);
        f ("trimMinFrames", trimMinFrames); f ("trimTailWindow", trimTailWindow); f ("trimTailSweepStart", trimTailSweepStart);
        f ("trimTailSweepRate", trimTailSweepRate); f ("trimTailSweepOffset", trimTailSweepOffset); f ("trimTailClosing", trimTailClosing);
        f ("boostGain", boostGain); f ("tiltEqFeedback", tiltEqFeedback); f ("ringModOffset", ringModOffset);
        f ("ringModDivisor", ringModDivisor); f ("fadeSlope", fadeSlope); f ("crossfadeLoopShare", crossfadeLoopShare);
        f ("crossfadeAutoShare", crossfadeAutoShare); f ("reverbCombsA", reverbCombsA); f ("reverbCombsB", reverbCombsB);
        f ("reverbCombGainA", reverbCombGainA); f ("reverbCombGainB", reverbCombGainB); f ("reverbAllpassA", reverbAllpassA);
        f ("reverbAllpassB", reverbAllpassB); f ("reverbAllpassBRight", reverbAllpassBRight);
        f ("reverbAllpassGain", reverbAllpassGain); f ("reverbDry", reverbDry); f ("reverbPoleB", reverbPoleB);
        f ("reverbTailSamples", reverbTailSamples); f ("reverbMaxTailSeconds", reverbMaxTailSeconds);
        f ("reverbTrimLevel", reverbTrimLevel);
        f ("onsetBlock", onsetBlock); f ("onsetRatio", onsetRatio); f ("onsetFloor", onsetFloor);
        f ("onsetPeakShare", onsetPeakShare); f ("onsetHitShare", onsetHitShare); f ("onsetLookBackBlocks", onsetLookBackBlocks);
        f ("onsetMinSpacing", onsetMinSpacing); f ("attackRatio", attackRatio); f ("attackFloor", attackFloor);
        f ("attackPeakShare", attackPeakShare);
        f ("vocoderFrame", vocoderFrame); f ("vocoderTransientThreshold", vocoderTransientThreshold); f ("grainFrame", grainFrame);
        f ("e3MonoFallback", e3MonoFallback); f ("e3MonoHold", e3MonoHold); f ("e2MonoFallback", e2MonoFallback);
        f ("e2MonoHold", e2MonoHold); f ("e2SpeechFallback", e2SpeechFallback); f ("e2SpeechHold", e2SpeechHold);
        f ("stretchSoft15", stretchSoft15); f ("stretchSoft067", stretchSoft067); f ("stretchProSoft15", stretchProSoft15);
        f ("stretchProSoft067", stretchProSoft067); f ("e2TransientSoft15", e2TransientSoft15);
        f ("e2TransientSoft067", e2TransientSoft067); f ("e2GenericSoft15", e2GenericSoft15);
        f ("e2GenericSoft067", e2GenericSoft067); f ("e2GenericHfSoft15", e2GenericHfSoft15);
        f ("e2GenericHfSoft067", e2GenericHfSoft067); f ("e3HfSoft067", e3HfSoft067); f ("e3RatioSteps", e3RatioSteps);
        f ("e2GenericRatioSteps", e2GenericRatioSteps); f ("attackSpan", attackSpan); f ("e2SpeechSplice", e2SpeechSplice);
        f ("grainMinPeriod", grainMinPeriod); f ("grainMaxPeriod", grainMaxPeriod); f ("grainWindow", grainWindow);
        f ("grainEstimateOffset", grainEstimateOffset); f ("grainEstimateReuse", grainEstimateReuse);
        f ("grainLagPull", grainLagPull); f ("grainScoreFloor", grainScoreFloor); f ("grainDip", grainDip);
        f ("grainPeakShare", grainPeakShare); f ("grainOnsetSpacing", grainOnsetSpacing); f ("e3SplitHz", e3SplitHz);
        f ("e3SplitRiseFrom", e3SplitRiseFrom); f ("e3SplitRisePower", e3SplitRisePower);
        f ("e3HoldGainStretch", e3HoldGainStretch); f ("e3HoldGainSqueeze", e3HoldGainSqueeze);
        f ("rephaseFrom", rephaseFrom); f ("rephaseSpan", rephaseSpan); f ("rephasePeakReach", rephasePeakReach);
        f ("rephasePeakLead", rephasePeakLead); f ("attackShiftSamples", attackShiftSamples); f ("attackOffset", attackOffset);
        f ("attackOffsetRise", attackOffsetRise); f ("attackGap", attackGap); f ("attackGapRise", attackGapRise);
        f ("attackAlphaLow", attackAlphaLow); f ("attackAlphaRange", attackAlphaRange);
        f ("attackOffsetPitched", attackOffsetPitched); f ("attackGapPitched", attackGapPitched);
        f ("attackGapPerOctave", attackGapPerOctave); f ("attackGrid", attackGrid); f ("attackUnderShare", attackUnderShare);
        f ("attackPeakTiming", attackPeakTiming); f ("attackLatePeak", attackLatePeak); f ("attackGain", attackGain);
        f ("attackGainSlope", attackGainSlope); f ("holdOctaves", holdOctaves); f ("holdSamples", holdSamples);
        f ("formantLifter", formantLifter); f ("formantMaxGain", formantMaxGain);
        f ("tempoHop", tempoHop); f ("tempoMaxSeconds", tempoMaxSeconds); f ("tempoCompression", tempoCompression);
        f ("tempoWeightDouble", tempoWeightDouble); f ("tempoWeightQuad", tempoWeightQuad); f ("tempoWeightHalf", tempoWeightHalf);
        f ("tempoStep", tempoStep); f ("tempoLoopShare", tempoLoopShare); f ("tempoPowerOfTwoWeight", tempoPowerOfTwoWeight);
        f ("tempoBarWeight", tempoBarWeight);
    }

    // sets a constant by name (an array entry by its name and index); false for an unknown key
    bool set (const std::string& key, double v)
    {
        bool found = false;
        forEach ([&] (const char* name, auto& field)
        {
            using T = std::remove_reference_t<decltype (field)>;
            if constexpr (std::is_array_v<T>)
            {
                for (size_t i = 0; i < std::extent_v<T> && ! found; ++i)
                    if (key == name + std::to_string (i)) { field[i] = (std::remove_extent_t<T>) v; found = true; }
            }
            else if (! found && key == name)
            {
                field = (T) v;
                found = true;
            }
        });
        return found;
    }

    // key=value lines, one per line; returns 0 or the number of the first bad line
    int load (const char* path)
    {
        std::ifstream f (path);
        if (! f) { std::printf ("cannot read tuning file %s\n", path); return 1; }
        std::string s;
        int n = 0;
        while (std::getline (f, s))
        {
            ++n;
            while (! s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
            if (s.empty() || s[0] == '#') continue;
            const auto eq = s.find ('=');
            if (eq == std::string::npos || ! set (s.substr (0, eq), std::strtod (s.c_str() + eq + 1, nullptr)))
            {
                std::printf ("bad tuning line %d: %s\n", n, s.c_str());
                return n;
            }
        }
        return 0;
    }
};

// the tuning in use: the defaults in the plug-in, overridden by CsRender
inline Tuning& tuning()
{
    static Tuning t;
    return t;
}
}
