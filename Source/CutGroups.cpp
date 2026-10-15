#include "CutGroups.h"
#include <algorithm>
#include <mutex>

namespace cutgroups
{
    namespace
    {
        // Lock-free ring of the latest triggers. A slot is valid when its
        // sequence number equals its write index + 1 (seqlock: checked again
        // after the fields are read).
        constexpr uint64_t ringSize = 1024;

        struct Slot
        {
            std::atomic<uint64_t> seq { 0 };
            std::atomic<uint32_t> source { 0 };
            std::atomic<int> group { 0 };
            std::atomic<int64_t> time { 0 };
        };

        struct Bus
        {
            std::atomic<uint64_t> written { 0 };
            Slot slots[ringSize];
        };

        Bus& bus()
        {
            static Bus b;
            return b;
        }

        constexpr int64_t staleSamples = 192000;   // ~1 s or more at any common rate
    }

    uint32_t newInstanceId()
    {
        static std::atomic<uint32_t> next { 1 };
        return next.fetch_add (1);
    }

    namespace
    {
        std::mutex numbersLock;
        std::vector<int> numbersInUse;
    }

    int claimNumber()
    {
        const std::lock_guard<std::mutex> l (numbersLock);
        for (int n = 1; n <= 99; ++n)
            if (std::find (numbersInUse.begin(), numbersInUse.end(), n) == numbersInUse.end())
            {
                numbersInUse.push_back (n);
                return n;
            }
        return 99;
    }

    void releaseNumber (int number)
    {
        const std::lock_guard<std::mutex> l (numbersLock);
        if (auto it = std::find (numbersInUse.begin(), numbersInUse.end(), number); it != numbersInUse.end())
            numbersInUse.erase (it);
    }

    void post (uint32_t source, int group, int64_t hostTime)
    {
        if (group <= 0)
            return;
        auto& b = bus();
        const auto index = b.written.fetch_add (1);
        auto& s = b.slots[index % ringSize];
        s.seq.store (0, std::memory_order_release);
        s.source.store (source, std::memory_order_relaxed);
        s.group.store (group, std::memory_order_relaxed);
        s.time.store (hostTime, std::memory_order_relaxed);
        s.seq.store (index + 1, std::memory_order_release);
    }

    Reader::Reader (uint32_t selfId)
        : self (selfId), cursor (bus().written.load())
    {
        waiting.reserve (256);
    }

    void Reader::collect (int by, int64_t hostTime, int numSamples, std::vector<int>& offsets)
    {
        offsets.clear();
        auto& b = bus();
        const auto written = b.written.load (std::memory_order_acquire);
        if (written - cursor > ringSize)
            cursor = written - ringSize;   // fell behind: the oldest are gone

        for (; cursor < written; ++cursor)
        {
            auto& s = b.slots[cursor % ringSize];
            if (s.seq.load (std::memory_order_acquire) != cursor + 1)
                break;   // still being written: read it next block
            const auto source = s.source.load (std::memory_order_relaxed);
            const auto group = s.group.load (std::memory_order_relaxed);
            const auto time = s.time.load (std::memory_order_relaxed);
            if (s.seq.load (std::memory_order_acquire) != cursor + 1)
                continue;   // overwritten while reading
            if (source != self && group > 0 && waiting.size() < 256)
                waiting.push_back ({ group, time });
        }

        for (size_t i = 0; i < waiting.size();)
        {
            const auto& w = waiting[i];
            bool done = true;
            if (by > 0 && w.group == by)
            {
                if (w.time < 0 || hostTime < 0)
                    offsets.push_back (0);                                   // no timeline: now
                else if (w.time < hostTime)
                {
                    if (hostTime - w.time < staleSamples) offsets.push_back (0);   // missed: as soon as possible
                }
                else if (w.time < hostTime + numSamples)
                    offsets.push_back ((int) (w.time - hostTime));
                else if (w.time - hostTime < staleSamples)
                    done = false;                                            // a later block
            }
            if (done)
            {
                waiting[i] = waiting.back();
                waiting.pop_back();
            }
            else
            {
                ++i;
            }
        }
        std::sort (offsets.begin(), offsets.end());
    }
}
