// RefRender: the offline VST3 host of the measurements and null tests. Renders stereo float32 files through a
// plug-in, in one of two forms that share the loader and the render loop.
//
//   RefRender plugin.vst3 --list
//       prints every component of the file as Name@uid, one per line.
//   RefRender plugin.vst3 "Name[@uid]" jobs.txt [fs=48000] [block=512] [bpm=120] [vary=1]
//       the jobs form: one render per line of jobs.txt, a fresh instance per job. The null device as jobs.txt
//       renders nothing and prints the latency.
//   RefRender plugin.vst3 "Name[@uid]" in.f32 out.f32 [fs=48000] [block=512] [bpm=120] [sc=file.f32]
//             [preroll=seconds] [name=text | name:=normalised | state=file ...] [at=seconds:setting ...]
//       the single form: one render, the settings in the order given.
//
// The plug-in is named by the name a host shows, or as Name@uid for a component of a file that holds several (a
// shell; --list prints them). A file changes its component list once it has a host context, so a Name@uid instance
// is created up to three times, the earlier ones kept loaded, until the right component comes up.
// in.f32, out.f32 and the side-chain file are interleaved stereo float32 at fs; the output is the main output with
// the reported latency removed. A mono main bus takes the left channel, the side-chain bus its own count (1 or 2).
// The host tempo is bpm (4/4, playing, the position counted from the first input sample).
// vary=1 (jobs form): irregular blocks of 1 ... block samples from a fixed seed, as a DAW delivers them.
//
// jobs.txt: one render per line:  in.f32 out.f32 [token ...]
//   param:Name=text        set a parameter by its displayed value (param:Character=0.25)
//   norm:Name=0.5          set a parameter by its normalised value
//   auto:Name@sample=0.7   change a parameter (normalised) at that input sample; the block is split there
//   at=seconds:setting     change a setting (name=text, name:=normalised or state=file) at that time of the input,
//                          sample exact; before 0: in the pre-roll, at a block's start
//   state=file             load a raw plug-in state before the parameters
//   sc=file.f32            side-chain input (the bus is enabled for a render that feeds it; needs a fresh instance)
//   notes=key:start:length:velocity;...   MIDI notes for an instrument: start and length in seconds, velocity 1 ... 127
//   raw=1                  no latency compensation (the output is the plug-in's output sample for sample)
//   reset=0                keep the instance of the previous job (default: a fresh instance per job)
//   settle=ms              run the message loop this long after setting the parameters (a plug-in that applies them
//                          asynchronously would otherwise take them partway through the render)
//   preroll=seconds        render that much silence first (not written), the parameters set after its first block
//                          (a plug-in can show but not process values set before its first block)
// The single form takes sc=, preroll=, at= and the settings as arguments; a setting there is name=text,
// name:=normalised or state=file (a whole state, as getStateInformation writes it), and every parameter of that
// name is set. The jobs form sets the parameters before it prepares a fresh instance, the single form after, each
// as its harnesses expect.
//
// Prints the latency and what was rendered. Exit code 0 only when every render was written: a missing or unreadable
// jobs, input, side-chain or state file, an input without frames, an unknown job token or parameter name, a
// side-chain file for a plug-in without an enabled side-chain bus, a plug-in that does not load and an output that
// cannot be written each end the run with "error: ..." on stderr and exit code 1; a wrong command line ends it with
// exit code 2. A failed render leaves no output file.
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#ifdef _WIN32
 #include <windows.h>
#endif

using namespace juce;

static int fail (const String& message)
{
    std::fflush (stdout);
    std::fprintf (stderr, "error: %s\n", message.toRawUTF8());
    std::fflush (stderr);
    return 1;
}

static File fileAt (const String& path) { return File::getCurrentWorkingDirectory().getChildFile (path); }

// interleaved float32 samples of a file; false when it is missing or unreadable
static bool readF32 (const String& path, std::vector<float>& samples)
{
    const File f (fileAt (path));
    MemoryBlock mb;
    if (! f.existsAsFile() || ! f.loadFileAsData (mb)) return false;
    samples.resize (mb.getSize() / sizeof (float));
    if (! samples.empty()) std::memcpy (samples.data(), mb.getData(), samples.size() * sizeof (float));
    return true;
}

// runs the message loop for a while (this host has no event loop of its own)
static void pumpMessages (int ms)
{
    const auto end = Time::getMillisecondCounter() + (uint32) ms;
    while (Time::getMillisecondCounter() < end)
    {
       #ifdef _WIN32
        MSG m;
        while (PeekMessage (&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage (&m); DispatchMessage (&m); }
       #endif
        Thread::sleep (5);
    }
}

struct FixedHead : AudioPlayHead
{
    double bpm = 120.0, fs = 48000.0;
    int64 pos = 0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (pos);
        p.setTimeInSeconds ((double) pos / fs);
        p.setPpqPosition ((double) pos / fs * bpm / 60.0);
        p.setIsPlaying (true);
        return p;
    }
};

// notes=key:start:length:velocity;... as note-on and note-off events at input sample positions
static std::multimap<int, MidiMessage> parseNotes (const String& spec, double fs)
{
    std::multimap<int, MidiMessage> events;
    for (const auto& note : StringArray::fromTokens (spec, ";", ""))
    {
        const auto f = StringArray::fromTokens (note, ":", "");
        if (f.size() < 4) continue;
        const int key = f[0].getIntValue();
        const int on = (int) std::lround (f[1].getDoubleValue() * fs);
        const int off = on + (int) std::lround (f[2].getDoubleValue() * fs);
        events.insert ({ on, MidiMessage::noteOn (1, key, (uint8) jlimit (1, 127, f[3].getIntValue())) });
        events.insert ({ off, MidiMessage::noteOff (1, key) });
    }
    return events;
}

// One thing to set: a parameter by its displayed text or its normalised value, or a whole state from a file.
struct Setting
{
    enum class Kind { text, norm, state };
    Kind kind = Kind::text;
    String name, value;                 // state: value is the file
    MemoryBlock data;                   // state: the file's bytes, read when the command line is

    // name=text, name:=normalised or state=file; false (the error printed) for another form or an unreadable file
    bool parse (const String& a)
    {
        if (a.startsWith ("state=")) return file (a.substring (6));
        if (! a.contains ("=")) { fail ("'" + a + "' is not a setting (name=text, name:=normalised or state=file)"); return false; }
        const bool normalised = a.contains (":=");
        kind = normalised ? Kind::norm : Kind::text;
        name = a.upToFirstOccurrenceOf (normalised ? ":=" : "=", false, false);
        value = a.fromFirstOccurrenceOf (normalised ? ":=" : "=", false, false);
        return true;
    }

    bool file (const String& path)
    {
        kind = Kind::state;
        value = path;
        if (fileAt (path).loadFileAsData (data)) return true;
        fail ("state file missing or unreadable: " + path);
        return false;
    }

    bool known (AudioPluginInstance& plugin) const
    {
        if (kind == Kind::state) return true;
        for (auto* p : plugin.getParameters())
            if (p->getName (128).equalsIgnoreCase (name)) return true;
        return false;
    }

    void apply (AudioPluginInstance& plugin, bool echo) const
    {
        if (kind == Kind::state)
        {
            plugin.setStateInformation (data.getData(), (int) data.getSize());
            if (echo) std::printf ("  state from %s (%d bytes)\n", value.toRawUTF8(), (int) data.getSize());
            return;
        }
        for (auto* p : plugin.getParameters())
            if (p->getName (128).equalsIgnoreCase (name))
            {
                p->setValueNotifyingHost (kind == Kind::norm ? value.getFloatValue() : p->getValueForText (value));
                if (echo) std::printf ("  %s = %s (%.6f)\n", name.toRawUTF8(), p->getText (p->getValue(), 64).toRawUTF8(), (double) p->getValue());
            }
    }
};

// One render: the files, the settings, the changes while rendering.
struct Job
{
    String in, out, sc, state;
    std::vector<Setting> settings;
    std::vector<std::pair<int, Setting>> changes;        // (sample of the input, what to set)
    std::multimap<int, MidiMessage> notes;
    bool raw = false, reset = true;
    int settle = 0;
    double preroll = 0.0;
    std::vector<float> x, scx;
    MemoryBlock stateData;

    // at=seconds:setting
    bool change (const String& spec, double fs)
    {
        Setting s;
        if (! spec.contains (":") || ! s.parse (spec.fromFirstOccurrenceOf (":", false, false))) return false;
        changes.push_back ({ (int) std::llround (spec.upToFirstOccurrenceOf (":", false, false).getDoubleValue() * fs), s });
        return true;
    }

    // the files of the job: a job that cannot run fails before the plug-in is touched
    bool load()
    {
        if (! readF32 (in, x)) { fail ("input file missing or unreadable: " + in); return false; }
        if (x.size() < 2) { fail ("input file has no frames: " + in); return false; }
        if (sc.isNotEmpty() && ! readF32 (sc, scx)) { fail ("side-chain file missing or unreadable: " + sc); return false; }
        if (state.isNotEmpty() && ! fileAt (state).loadFileAsData (stateData)) { fail ("state file missing or unreadable: " + state); return false; }
        std::stable_sort (changes.begin(), changes.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        return true;
    }

    // one line of a jobs file; false (the error printed) for a token this host does not know
    bool parseLine (StringArray tok, double fs)
    {
        for (auto& t : tok) t = t.unquoted();
        in = tok[0]; out = tok[1];
        for (int i = 2; i < tok.size(); ++i)
        {
            const auto& a = tok[i];
            Setting s;
            if (a.startsWith ("sc=")) sc = a.substring (3);
            else if (a.startsWith ("notes=")) notes = parseNotes (a.substring (6), fs);
            else if (a.startsWith ("state=")) state = a.substring (6);
            else if (a == "raw=1" || a == "raw=0") raw = a == "raw=1";
            else if (a == "reset=0" || a == "reset=1") reset = a == "reset=1";
            else if (a.startsWith ("settle=")) settle = a.substring (7).getIntValue();
            else if (a.startsWith ("preroll=")) preroll = a.substring (8).getDoubleValue();
            else if (a.startsWith ("at=")) { if (! change (a.substring (3), fs)) return false; }
            else if (a.startsWith ("param:") || a.startsWith ("norm:") || a.startsWith ("auto:"))
            {
                const auto body = a.fromFirstOccurrenceOf (":", false, false);
                s.kind = a.startsWith ("param:") ? Setting::Kind::text : Setting::Kind::norm;
                s.name = body.upToFirstOccurrenceOf ("=", false, false);
                s.value = body.fromFirstOccurrenceOf ("=", false, false);
                if (! body.contains ("=")) { fail ("'" + a + "' has no value (job " + out + ")"); return false; }
                if (a.startsWith ("auto:"))
                {
                    const int at = s.name.fromLastOccurrenceOf ("@", false, false).getIntValue();
                    s.name = s.name.upToLastOccurrenceOf ("@", false, false);
                    changes.push_back ({ at, s });
                }
                else settings.push_back (s);
            }
            else { fail ("unknown job token '" + a + "' (job " + out + ")"); return false; }
        }
        return true;
    }
};

// The plug-in of the command line and its instances.
struct Loader
{
    AudioPluginFormatManager fm;
    PluginDescription desc;
    bool byUid = false;
    std::vector<std::unique_ptr<AudioPluginInstance>> keepLoaded;
    FixedHead head;
    double fs = 48000.0;
    int block = 512;
    int nIn = 0, nOut = 0;
    int mainIn = 0, scAt = 0, scIn = 0;     // channels fed from the input; the side-chain's first channel and its count
    bool verbose = true;

    Loader() { fm.addFormat (std::make_unique<VST3PluginFormat>()); }

    static OwnedArray<PluginDescription> components (const String& file)
    {
        OwnedArray<PluginDescription> types;
        VST3PluginFormat vst3;
        vst3.findAllTypesForFile (types, file);
        return types;
    }

    // Name@uid (the uid a whole number) names a component without a scan of the file; a plain name is looked up
    bool find (const String& file, const String& name)
    {
        const auto uid = name.fromLastOccurrenceOf ("@", false, false);
        if (name.contains ("@") && uid.isNotEmpty() && uid.containsOnly ("-0123456789"))
        {
            byUid = true;
            desc.pluginFormatName = "VST3";
            desc.fileOrIdentifier = file;
            desc.name = name.upToLastOccurrenceOf ("@", false, false);
            desc.uniqueId = uid.getIntValue();
            desc.numInputChannels = desc.numOutputChannels = 2;
            return true;
        }
        for (auto* t : components (file))
            if (t->name == name) { desc = *t; return true; }
        fail ("no plug-in named '" + name + "' in " + file);
        return false;
    }

    // a new instance with its buses laid out, the play head set, not yet prepared; nullptr (the error printed).
    // sideChain: the render feeds the side-chain bus, so the bus stays enabled
    std::unique_ptr<AudioPluginInstance> create (bool sideChain)
    {
        String err;
        std::unique_ptr<AudioPluginInstance> p;
        for (int attempt = 0; attempt < (byUid ? 3 : 1); ++attempt)
        {
            p = fm.createPluginInstance (desc, fs, block, err);
            if (p == nullptr || ! byUid || p->getName() == desc.name) break;
            keepLoaded.push_back (std::move (p));
        }
        if (p == nullptr || (byUid && p->getName() != desc.name))
        {
            fail ("load failed: " + (err.isNotEmpty() ? err : "no plug-in named '" + desc.name + "' in " + desc.fileOrIdentifier));
            return nullptr;
        }
        // the main buses as the plug-in has them; the side-chain bus with the main input's channels (stereo, or
        // mono beside a mono input), else with the other count
        auto layout = p->getBusesLayout();
        const bool monoMain = layout.inputBuses.size() > 0 && layout.inputBuses[0].size() == 1;
        bool layoutSet = false;
        for (int attempt = 0; attempt < 2 && ! layoutSet; ++attempt)
        {
            for (int b = 1; b < layout.inputBuses.size(); ++b)
                layout.inputBuses.getReference (b) = (monoMain == (attempt == 0)) ? AudioChannelSet::mono() : AudioChannelSet::stereo();
            layoutSet = p->setBusesLayout (layout);
        }
        if (! layoutSet && verbose) std::printf ("side-chain layout refused\n");
        nIn = p->getTotalNumInputChannels(); nOut = p->getTotalNumOutputChannels();
        // setPlayConfigDetails switches every bus but the main ones off where the plug-in allows that: a render
        // without a side-chain file runs that way, one with a side-chain file gets the bus back
        p->setPlayConfigDetails (nIn, nOut, fs, block);
        if (sideChain && p->getBusCount (true) > 1 && ! p->getBus (true, 1)->isEnabled())
            p->setBusesLayout (layout);
        mainIn = jmin (2, p->getChannelCountOfBus (true, 0));
        scAt = p->getChannelCountOfBus (true, 0);
        scIn = sideChain && p->getBusCount (true) > 1 && p->getBus (true, 1)->isEnabled() ? jmin (2, p->getChannelCountOfBus (true, 1)) : 0;
        for (int b = 0; verbose && b < p->getBusCount (true); ++b)
            std::printf ("  in bus %d '%s': %d ch, enabled %d\n", b, p->getBus (true, b)->getName().toRawUTF8(),
                         p->getBus (true, b)->getNumberOfChannels(), (int) p->getBus (true, b)->isEnabled());
        verbose = false;
        p->setPlayHead (&head);
        return p;
    }
};

// Renders one job through the instance and writes its output. fresh: the instance is new and not yet prepared.
// prepareFirst: prepare it before the settings (the single form) instead of after them (the jobs form).
static int render (Loader& host, AudioPluginInstance& plugin, const Job& job, bool fresh, bool prepareFirst, bool vary, bool echo)
{
    const double fs = host.fs;
    const int block = host.block;
    if (job.sc.isNotEmpty() && host.scIn == 0)
        return fail ("sc= given, but the plug-in has no enabled side-chain bus"
                     + String (fresh ? "" : " (a side-chain needs a fresh instance: not with reset=0)") + " (" + job.out + ")");
    for (auto& s : job.settings)
        if (! s.known (plugin)) return fail ("no parameter '" + s.name + "' (" + job.out + ")");
    for (auto& c : job.changes)
        if (! c.second.known (plugin)) return fail ("no parameter '" + c.second.name + "' (" + job.out + ")");

    if (fresh && job.state.isNotEmpty())
    {
        plugin.setStateInformation (job.stateData.getData(), (int) job.stateData.getSize());
        pumpMessages (300);                                     // let a plug-in finish applying it asynchronously
    }
    const int preN = job.preroll > 0.0 ? jmax (2 * block, (int) std::llround (job.preroll * fs)) : 0;
    const bool early = prepareFirst || preN > 0;
    size_t nextChange = 0;
    if (fresh && early) plugin.prepareToPlay (fs, block);
    if (preN > 0)
    {
        AudioBuffer<float> pre (jmax (1, host.nIn, host.nOut), block);
        MidiBuffer preMidi;
        for (int pos = 0; pos < preN; pos += block)             // a first block of silence, the settings, the rest
        {
            if (pos == block)
                for (auto& s : job.settings) s.apply (plugin, echo);
            while (pos >= block && nextChange < job.changes.size() && job.changes[nextChange].first <= pos - preN)
                job.changes[nextChange++].second.apply (plugin, echo);
            pre.clear();
            host.head.pos = pos - preN;
            plugin.processBlock (pre, preMidi);
        }
    }
    else
    {
        for (auto& s : job.settings) s.apply (plugin, echo);
    }
    if (fresh && ! early) plugin.prepareToPlay (fs, block);
    if (job.settle > 0) pumpMessages (job.settle);

    const int latency = job.raw ? 0 : plugin.getLatencySamples();
    const int n = (int) (job.x.size() / 2);
    if (echo)
        std::printf ("in %d ch, out %d ch, latency %d, %d frames, side-chain %s, bpm %g\n", host.nIn, host.nOut, latency, n,
                     job.sc.isNotEmpty() ? "fed" : "off", host.head.bpm);
    std::vector<float> out ((size_t) n * 2, 0.0f);
    AudioBuffer<float> buf (jmax (1, host.nIn, host.nOut), block);
    MidiBuffer midi;
    auto nextNote = job.notes.begin();
    Random rng (1234);
    for (int pos = 0; pos < n + latency;)
    {
        while (nextChange < job.changes.size() && job.changes[nextChange].first <= pos)
            job.changes[nextChange++].second.apply (plugin, echo);
        int len = ! vary ? block : rng.nextInt (4) == 0 ? block : 1 + rng.nextInt (block);
        if (nextChange < job.changes.size()) len = jmin (len, job.changes[nextChange].first - pos);    // up to the next change
        buf.setSize (buf.getNumChannels(), len, false, false, true);
        midi.clear();
        for (; nextNote != job.notes.end() && nextNote->first < pos + len; ++nextNote)
            midi.addEvent (nextNote->second, jmax (0, nextNote->first - pos));
        buf.clear();
        for (int i = 0; i < len; ++i)
        {
            const int k = pos + i;
            if (k < n)
            {
                for (int c = 0; c < host.mainIn; ++c) buf.setSample (c, i, job.x[(size_t) k * 2 + (size_t) c]);
                if ((size_t) k * 2 + 1 < job.scx.size())
                    for (int c = 0; c < host.scIn; ++c) buf.setSample (host.scAt + c, i, job.scx[(size_t) k * 2 + (size_t) c]);
            }
        }
        host.head.pos = pos;
        plugin.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
        {
            const int k = pos + i - latency;
            if (k >= 0 && k < n)
            {
                out[(size_t) k * 2] = buf.getSample (0, i);
                out[(size_t) k * 2 + 1] = buf.getSample (host.nOut > 1 ? 1 : 0, i);
            }
        }
        pos += len;
    }
    File f (fileAt (job.out));
    f.deleteFile();
    FileOutputStream os (f);
    if (os.failedToOpen() || ! os.write (out.data(), out.size() * sizeof (float)))
        return fail ("cannot write the output file: " + job.out);
    if (! echo) std::printf ("job %s: %d frames, latency %d\n", job.out.toRawUTF8(), n, latency);
    std::fflush (stdout);
    return 0;
}

static int usage()
{
    std::printf ("usage: RefRender plugin.vst3 --list\n"
                 "       RefRender plugin.vst3 \"Name[@uid]\" jobs.txt [fs=48000] [block=512] [bpm=120] [vary=1]\n"
                 "       RefRender plugin.vst3 \"Name[@uid]\" in.f32 out.f32 [fs=] [block=] [bpm=] [sc=file.f32] [preroll=seconds]\n"
                 "                 [name=text | name:=normalised | state=file ...] [at=seconds:setting ...]\n");
    return 2;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    StringArray args;
    for (int i = 1; i < argc; ++i) args.add (String (argv[i]).unquoted());
    if (args.size() == 2 && args[1] == "--list")
    {
        const auto types = Loader::components (args[0]);
        for (auto* t : types) std::printf ("%s@%d\n", t->name.toRawUTF8(), t->uniqueId);
        return types.isEmpty() ? fail ("no plug-in found in " + args[0]) : 0;
    }
    if (args.size() < 3 || args[1] == "--list") return usage();
    // the single form names an output file where the jobs form has its first key=value
    const bool single = args.size() >= 4 && ! args[3].contains ("=");

    Loader host;
    Job one;
    bool vary = false;
    for (int i = single ? 4 : 3; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a.startsWith ("fs=")) host.fs = a.substring (3).getDoubleValue();
        else if (a.startsWith ("block=")) host.block = a.substring (6).getIntValue();
        else if (a.startsWith ("bpm=")) host.head.bpm = a.substring (4).getDoubleValue();
        else if (! single && (a == "vary=1" || a == "vary=0")) vary = a == "vary=1";
        else if (! single) { fail ("unknown argument '" + a + "'"); return 2; }
        else if (a.startsWith ("sc=")) one.sc = a.substring (3);
        else if (a.startsWith ("preroll=")) one.preroll = a.substring (8).getDoubleValue();
        else if (! a.contains ("=")) { fail ("unknown argument '" + a + "' (a setting is name=text or name:=normalised)"); return 2; }
    }
    if (host.fs <= 0.0 || host.block < 1 || host.head.bpm <= 0.0) { fail ("fs, block and bpm must be above zero"); return 2; }
    host.head.fs = host.fs;

    if (single)
    {
        one.in = args[2]; one.out = args[3]; one.reset = false;
        for (int i = 4; i < args.size(); ++i)                    // the settings and changes, once fs is known
        {
            const auto& a = args[i];
            if (a.startsWith ("fs=") || a.startsWith ("block=") || a.startsWith ("bpm=") || a.startsWith ("sc=") || a.startsWith ("preroll=")) continue;
            Setting s;
            if (a.startsWith ("at=")) { if (! one.change (a.substring (3), host.fs)) return 1; }
            else if (s.parse (a)) one.settings.push_back (s);
            else return 1;
        }
        if (! one.load() || ! host.find (args[0], args[1])) return 1;
        auto plugin = host.create (one.sc.isNotEmpty());
        if (plugin == nullptr) return 1;
        const int code = render (host, *plugin, one, true, true, false, true);
        plugin->releaseResources();
        return code;
    }

    StringArray lines;
    // the null device as the jobs file: nothing to render, the plug-in is loaded and its latency printed
    if (! (args[2].equalsIgnoreCase ("nul") || args[2] == "/dev/null"))
    {
        const File jobs (fileAt (args[2]));
        if (! jobs.existsAsFile()) return fail ("jobs file not found: " + args[2]);
        jobs.readLines (lines);
    }
    if (! host.find (args[0], args[1])) return 1;
    auto plugin = host.create (false);
    if (plugin == nullptr) return 1;
    plugin->prepareToPlay (host.fs, host.block);
    std::printf ("loaded %s: in %d ch, out %d ch, latency %d\n", plugin->getName().toRawUTF8(), host.nIn, host.nOut, plugin->getLatencySamples());

    for (auto& line : lines)
    {
        auto tok = StringArray::fromTokens (line, " ", "\"");
        tok.removeEmptyStrings();
        if (tok.isEmpty()) continue;
        if (tok.size() < 2) return fail ("a job needs an input and an output file: " + line);
        Job job;
        if (! job.parseLine (tok, host.fs) || ! job.load()) return 1;
        // a fresh instance per job: resetting through the plug-in's state is not reliable (a plug-in can apply its
        // state asynchronously, after the parameters set here)
        if (job.reset)
        {
            plugin->releaseResources();
            plugin.reset();
            plugin = host.create (job.sc.isNotEmpty());
            if (plugin == nullptr) return 1;
        }
        if (const int code = render (host, *plugin, job, job.reset, false, vary, false)) return code;
    }
    plugin->releaseResources();
    return 0;
}
