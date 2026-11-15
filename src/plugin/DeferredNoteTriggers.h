#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace magdrum::plugin
{
struct DeferredNoteTrigger
{
    int midiNote = 0;
    int velocity = 100;
};

// Audio-thread-only bounded storage for note-ons that arrive while the sample
// table is being updated. It makes no allocations and takes no locks.
class DeferredNoteTriggers
{
public:
    static constexpr std::size_t capacity = 2048;

    bool push(int midiNote, int velocity) noexcept
    {
        if (count == entries.size())
        {
            ++overflowCount;
            return false;
        }

        entries[count++] = {midiNote, velocity};
        return true;
    }

    template <typename Consumer>
    void drain(Consumer&& consume) noexcept(noexcept(consume(DeferredNoteTrigger{})))
    {
        for (std::size_t index = 0; index < count; ++index)
            consume(entries[index]);
        count = 0;
    }

    std::size_t size() const noexcept { return count; }
    std::uint64_t getOverflowCount() const noexcept { return overflowCount; }

private:
    std::array<DeferredNoteTrigger, capacity> entries{};
    std::size_t count = 0;
    std::uint64_t overflowCount = 0;
};
}
