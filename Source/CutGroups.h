#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <vector>

// FL's channel Group (Cut / By) across plugin instances. Every Chan Sampler
// in the same host process shares this bus: an instance whose Cut group is N
// posts "group N triggered at host sample T" for each note it starts, and every
// other instance whose By group is N stops its voices at T.
//
// Timing: messages carry the host timeline position, so a cut lands on the
// exact sample when the cut instance processes the block after the cutting
// one; otherwise it lands at the start of its next block (at most one buffer
// late). Instances in other processes (bridged / sandboxed plugins) cannot
// see each other.
namespace cutgroups
{
    // A unique id per plugin instance (never 0).
    uint32_t newInstanceId();

    // The instance's number, FL's channel number for "Cut self": the smallest
    // number (1..99) no other open instance holds. Release it when the
    // instance goes away.
    int claimNumber();
    void releaseNumber (int number);

    // Posts a trigger; hostTime < 0 when the host timeline is unknown or stopped.
    void post (uint32_t source, int group, int64_t hostTime);

    // Reads the triggers posted since the last call. Each reader keeps its own
    // cursor; triggers of its own instance are skipped.
    class Reader
    {
    public:
        explicit Reader (uint32_t self);

        // Sample offsets in the block [hostTime, hostTime + numSamples) where
        // group `by` cuts, in ascending order. Triggers for later blocks are
        // kept until their block comes; stale ones (over a second away) are dropped.
        void collect (int by, int64_t hostTime, int numSamples, std::vector<int>& offsets);

    private:
        struct Item { int group; int64_t time; };
        uint32_t self;
        uint64_t cursor;
        std::vector<Item> waiting;
    };
}
