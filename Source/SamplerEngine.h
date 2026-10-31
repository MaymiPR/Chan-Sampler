#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>

#include "DeclickCurves.h"
#include "Dsp.h"
#include "SampleData.h"
#include "TimeStretch.h"

// Per-block snapshot of every realtime parameter.
struct EngineParams
{
    // Playback
    float startOffset = 0, loopStart = 0, loopEnd = 1;
    bool useLoop = false, pingPong = false;
    DeclickMode declick = DeclickMode::outOnly;

    int pitchRange = 2;   // MIDI pitch bend range in semitones

    // Pitch
    int rootNote = 60;
    float fineTune = 0, pitch = 0;

    // Time stretching: MUL scales the length, TIME (beats, 0 = off) sets it.
    StretchMode stretchMode = StretchMode::resample;
    float stretchMul = 1, stretchTime = 0;
    double songSeconds = -1;      // song position at the buffer start (playing), for Global LFOs
    float formant = 0;      // Stretch Pro formant shift, cents

    // One envelope + LFO per INS target
    struct Target
    {
        bool envOn = false;
        float envAmt = 0;
        dsp::Envelope::Params env;
        dsp::Lfo::Params lfo;
        bool envTempo = false;    // envelope times x 120 / tempo
    };
    Target panT, volT, xT, yT, pitchT;

    // Filter (Mod X = cutoff, Mod Y = resonance)
    FilterType filterType = FilterType::fastLP;
    float cutoff = 1, resonance = 0;

    // Levels adjustment
    float volume = 1, pan = 0, xOfs = 0, yOfs = 0;

    // Tracking
    float velPan = 0, velX = 0, velY = 0, velMid = 0.5f;
    float keyPan = 0, keyX = 0, keyY = 0;
    int keyMid = 60;

    // Polyphony
    int slideTicks = 4;   // portamento / mono glide length in song ticks (FL: N steps)
    bool mono = false, porta = false, cutItself = false;

    // Arpeggiator: direction 0 = off, 1 up, 2 down, 3 up/down, 4 up/down sticky, 5 random
    int arpDir = 0, arpRange = 1, arpRepeat = 1, arpChord = 0;
    float arpTime = 1, arpGate = 1;   // time in steps (1/16 notes), gate as fraction of a step
    bool arpSlide = false;

    // Echo delay / fat mode: each echo is an extra voice
    float echoFeed = 0, echoPan = 0, echoX = 0, echoY = 0, echoPitch = 0, echoTime = 3;
    int echoCount = 4;
    bool echoPing = false, echoFat = false;

    double bpm = 120.0;   // host tempo for arpeggio / echo timing
};

// Offsets an echo voice carries relative to the note that spawned it.
struct NoteMods
{
    float gain = 1, pan = 0, cut = 0, res = 0, pitch = 0;
    bool slide = false;
    int echo = 0;   // 0 = original note, k = k-th echo
    bool arp = false;   // played by the arpeggiator
    bool preview = false;      // sample-view click: drives the playback bar
    bool legatoOnly = false;   // arpeggio slide step after the first: never starts a note
};

class SamplerEngine
{
public:
    static constexpr int maxVoices = 64;

    void prepare (double sampleRate);
    // cuts: sample offsets in this block where the channel is cut by its
    // group (FL's "By"), ascending; may be null.
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                 const SampleData* sample, const EngineParams& p, const std::vector<int>* cuts = nullptr);

    // Sample offsets in the last rendered block where notes started (for the
    // channel's Cut group).
    const std::vector<int>& getNoteStarts() const noexcept { return noteStarts; }
    int getNumActiveVoices() const noexcept;

    // Sample-view preview (called on the audio thread, outside MIDI): only
    // these notes move the playback bar, as in FL (host notes do not).
    void previewNote (int note, bool on, const EngineParams& p);

    // Play head of the newest sounding preview voice as a fraction of the sample
    // (-1 when silent), for the sample view's playback bar. Written by the
    // audio thread after each block, read by the UI.
    float getPlayhead() const noexcept { return playhead.load(); }
    void updatePlayhead (const SampleData* sample) noexcept;

    // The time stretchers (about 2 MB per voice) are made the first time a
    // stretch mode is needed, never on the audio thread: call
    // ensureStretchers () from the message thread, or from a non-realtime
    // render. A note that starts before they exist plays resampled.
    static bool usesStretcher (StretchMode m) { return m != StretchMode::resample && m != StretchMode::sliceMap; }
    void ensureStretchers();
    bool hasStretchers() const noexcept { return stretchersReady.load (std::memory_order_acquire); }

private:
    enum class KillShape { cosine, transient, crossfade };   // out-fade curve

    struct Voice
    {
        bool active = false, released = false, killing = false, inLoop = false;
        int note = 0, dir = 1;
        float velocity = 1, targetNote = 0;
        // Glide (measured in FL): the pitch moves from its note to targetNote in
        // glideTicks equal steps on the song's tick grid (each at the sample
        // nearest the tick), the first at the event's tick. FL adds the steps to
        // a float in cents, so long glides end a little flat (kept: it nulls).
        float glideCents = 0, glideStep = 0;  // running pitch and step, cents
        double blockNote = 0;                 // note used for the block's increment
        int glideTicks = 0, glideDone = 0;
        double glideTick0 = 0;                // song tick of the first step
        double pos = 0;
        uint64_t age = 0;

        // Index order: pan, vol, x, y, pitch
        dsp::Envelope env[5];
        dsp::Lfo lfo[5];
        dsp::VoiceFilter filter;
        bool filterOn = false;

        // At the end of the sample FL holds the last value and fades it out.
        bool holding = false;
        float holdL = 0, holdR = 0, lastL = 0, lastR = 0;

        int fadeInPos = 0, fadeInLen = 0;
        bool fadeInLinear = false;     // true: transient step removal instead of a gain fade
        bool fadeInCrossfade = false;  // true: the Crossfade mode's measured in-fade
        float stepL = 0, stepR = 0;
        int killPos = 0, killLen = 0;
        KillShape killShape = KillShape::cosine;
        float killLowL = 0, killLowR = 0;   // the transient out-fade's low-pass state

        float gain = 0, gainTarget = 0, panValue = 0;
        double volLfoStart = 0;   // note time at the start of the control block (per-sample LFOs)
        double songLfoStart = 0;  // song time at the start of the control block (Global LFOs)
        float panBase = 0;        // pan without the LFO
        double pitchMul = 1;      // pitch LFO factor (per sample)
        int ctlLeft = 0;          // samples left in the current control block
        bool envStarted = false;  // the volume envelope has produced output
        float gainStep = 0, panL = 1, panR = 1;
        double increment = 1;     // source frames per output sample for pitch
        double timeRate = 1;      // source frames per output sample for the play head (stretch modes)

        // Mod X / Y envelopes as FL runs them for the filter: evaluated once per
        // tick from the note start and interpolated linearly, the first tick
        // holding the second knot (measured). A copy of the envelope runs to
        // the knots.
        struct ModKnots
        {
            dsp::Envelope env;
            double envTime = 0, nextKnot = 0;   // note seconds
            float prev = 0, next = 0;
        };
        ModKnots knots[2];                // Mod X, Mod Y
        bool filterPerSample = false;     // filter modulated: coefficients per sample
        dsp::VoiceFilter coefPrev, coefNext;   // coefficients at the tick knots around now
        double filterKnot = -1;           // note seconds of coefNext (-1: recompute)
        float cutBase = 1, resBase = 0;   // unmodulated cutoff / resonance

        bool stretched = false;   // plays through its voice's stretcher (decided at note-on)
        // e3 / e2 loops as FL plays them (it stretches the sample ahead of
        // time): the loop's first pass is stretched straight through,
        // recorded in the voice's loop take and repeated. takeLen = 0 while
        // recording; the loop recorded, to see it moved.
        bool takeLoop = false;
        int takeLen = 0, takeAt = 0, takeStart = 0, takeEnd = 0;
        int onsetIdx = 0;         // next transient for the slice modes
        double slicePos = -1;     // slice map: read head inside the current slice
        float sliceLowL = 0, sliceLowR = 0;   // slice map: the slice-end high-pass state
        double sliceFade = 0;     // slice map: how far into its end fade the slice is (0: not fading)
        double tailPos = -1;      // slice map: a slice the next one cut, fading out under it
        double tailFade = 0;
        int tailEnd = 0;
        float tailLowL = 0, tailLowR = 0;
        bool firstBlock = true;
        NoteMods mods;
    };

    struct DeclickSpec { float inMs; bool transientIn; float outMs; bool bleed; };
    static DeclickSpec declickSpec (DeclickMode m);

    // Notes scheduled ahead (arpeggio steps, echoes), in absolute samples.
    struct Scheduled
    {
        int64_t time;
        bool on;
        int note;
        float velocity;
        NoteMods mods;
    };

    // Host events, the arpeggiator and echoes (scheduled notes)
    void handleNoteOn (int64_t time, int note, float velocity, const EngineParams& p);
    void handleNoteOff (int64_t time, int note, const EngineParams& p);
    void endArpNote (int note, const EngineParams& p, bool keyReleased);
    void playNoteOn (int64_t time, int note, float velocity, NoteMods mods, const EngineParams& p);
    void playNoteOff (int64_t time, int note, int echo, const EngineParams& p);
    void schedule (const Scheduled& e);
    void advanceArp (int64_t until, const EngineParams& p);
    void cutByGroup (const EngineParams& p);
    double samplesPerStep (const EngineParams& p) const { return sampleRate * 60.0 / juce::jmax (1.0, p.bpm) / 4.0; }
    double songTimeAt (const EngineParams& p, int bufferPos) const
    {
        return (p.songSeconds >= 0.0 ? p.songSeconds : (double) now / sampleRate) + bufferPos / sampleRate;
    }

    // Voices
    void noteOn (int note, float velocity, NoteMods mods, const SampleData& s, const EngineParams& p);
    void noteOff (int note, int echo, const EngineParams& p);
    void releaseVoice (Voice& v, const EngineParams& p);
    void startKill (Voice& v, const EngineParams& p);
    Voice& allocateVoice (const EngineParams& p);
    void renderRange (juce::AudioBuffer<float>& out, int start, int num, const SampleData& s, const EngineParams& p);
    void updateControls (Voice& v, int numSamples, const SampleData& s, const EngineParams& p);
    void refreshControls (Voice& v, const SampleData& s, const EngineParams& p);
    void startModKnots (Voice& v, const EngineParams& p);
    float modEnvelopeAt (Voice& v, int xy, double noteSeconds, const EngineParams& p);
    void releaseModKnots (Voice& v, double noteSeconds, const EngineParams& p);
    void setupFilterAt (Voice& v, double noteSeconds, double songSeconds, const EngineParams& p);

    // Mono / portamento glide
    void eraseMonoKey (int key);
    void startGlide (Voice& v, int note, const EngineParams& p);
    void beginGlide (Voice& v, float fromCents, int note, const EngineParams& p) const;
    double soundingNote (Voice& v, const EngineParams& p, double songSeconds) const;

    std::array<Voice, maxVoices> voices;
    std::array<std::unique_ptr<TimeStretcher>, maxVoices> stretchers;   // one per voice, see ensureStretchers ()
    // Per voice, made with the stretchers: a stretched loop's first pass
    // (0.74 s at 44.1 kHz; longer loops fold into the stretcher as before).
    static constexpr int loopTakeFrames = 32768;
    struct LoopTake { std::vector<float> l, r; };
    std::array<LoopTake, maxVoices> loopTakes;
    std::atomic<bool> stretchersReady { false };
    std::mutex stretcherLock;   // ensureStretchers () only, never the audio thread
    std::atomic<float> playhead { -1.0f };
    double sampleRate = 44100.0;
    uint64_t ageCounter = 0;
    float bend = 0;
    float lastNote = -1;
    std::array<int, 128> monoKeys {};       // mono: earlier keys to return to (FL's key stack)
    int numMonoKeys = 0;
    const SampleData* currentSample = nullptr;

    int64_t now = 0;                        // absolute sample clock
    int blockPos = 0;                       // offset in the current block
    int ctlBufferPos = 0;                   // buffer offset of the control block being set up
    uint64_t eventSerial = 0;               // bumped by anything that changes a voice
    std::vector<int> noteStarts;            // see getNoteStarts()
    std::vector<Scheduled> pending;         // reserved; never grows on the audio thread
    static constexpr size_t maxPending = 4096;

    // Arpeggiator state
    std::vector<int> held;                  // held keys in press order (reserved)
    std::vector<float> heldVel;
    int64_t arpNextStep = -1;
    int64_t arpStart = 0;     // time of the first step; step k sits at round (k * step) after it
    int64_t arpSteps = 0;
    int arpIndex = 0, sustainedNote = -1;
    juce::Random arpRandom { 1234 };
};
