#include "FactoryPresets.h"
#include "PluginProcessor.h"

void applyFactoryPreset (ChanSamplerProcessor& processor, const FactoryPreset& preset)
{
    for (auto* p : processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, value] : preset.values)
    {
        auto* rp = processor.apvts.getParameter (id);
        jassert (rp != nullptr);   // a misspelt parameter ID
        if (rp != nullptr)
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
    }
}

// Units: envelope / LFO times and levels are knob positions (0..1); LFO speed
// is a period of 13.5755 * 982.5^p - 13.2745 FL ticks (96 per beat; 4.8 ms at
// 130 BPM with Tempo off); pitch amounts are +/-128 cents at full; echo time is
// in steps (1/16 notes); filter type 1 = LP.
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        // Chop: the sample plays exactly as long as the note (no attack, full
        // hold, sustain 0, release 0), the classic way to chop a sample
        // across the keys.
        { "Envelopes", "Chop",
          { { "volEnvOn", 1 }, { "volDel", 0 }, { "volAtt", 0 }, { "volHold", 1 }, { "volDec", 0 }, { "volSus", 0 }, { "volRel", 0 } } },
        { "Envelopes", "Pluck",
          { { "volEnvOn", 1 }, { "volDel", 0 }, { "volAtt", 0 }, { "volHold", 0 }, { "volDec", 0.3f }, { "volSus", 0 }, { "volRel", 0.15f } } },
        { "Envelopes", "Swell",
          { { "volEnvOn", 1 }, { "volDel", 0 }, { "volAtt", 0.45f }, { "volHold", 0 }, { "volDec", 0 }, { "volSus", 1 }, { "volRel", 0.45f } } },

        // Low-pass that opens on each note and closes over its decay
        { "Movement", "Filter sweep",
          { { "filterType", 1 }, { "cutoff", 0.25f }, { "resonance", 0.35f },
            { "xEnvOn", 1 }, { "xDel", 0 }, { "xAtt", 0 }, { "xHold", 0 }, { "xDec", 0.35f }, { "xSus", 0 }, { "xRel", 0.2f }, { "xEnvAmt", 0.75f } } },
        // Low-pass moved by a tempo-synced sine, one cycle per 1/8 note
        { "Movement", "Wobble 1/8",
          { { "filterType", 1 }, { "cutoff", 0.45f }, { "resonance", 0.5f },
            { "xLfoShape", 0 }, { "xLfoTempo", 1 }, { "xLfoSpeed", 0.2187f }, { "xLfoAmt", 0.6f }, { "xLfoAtt", 0 } } },
        // Volume LFO at about 6 Hz
        { "Movement", "Tremolo",
          { { "volLfoShape", 0 }, { "volLfoSpeed", 0.1832f }, { "volLfoAmt", 0.5f }, { "volLfoAtt", 0 } } },
        // Pitch LFO at about 5.5 Hz, +/-25 cents, easing in after a moment
        { "Movement", "Vibrato",
          { { "pLfoShape", 0 }, { "pLfoSpeed", 0.1923f }, { "pLfoAmt", 0.2f }, { "pLfoDel", 0.1f }, { "pLfoAtt", 0.15f } } },

        // One note at a time, gliding to each new note (808 basses)
        { "Playing", "808 glide",
          { { "mono", 1 }, { "porta", 1 }, { "slide", 0.6f } } },
        // Echoes every 1/8 note, alternating left and right
        { "Playing", "Echo ping-pong 1/8",
          { { "echoFeed", 0.3f }, { "echoCount", 6 }, { "echoTime", 2 }, { "echoPan", -0.8f }, { "echoPing", 1 } } },
        { "Playing", "Reverse",
          { { "pcReverse", 1 } } },
    };
    return presets;
}
