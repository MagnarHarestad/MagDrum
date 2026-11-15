#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace magdrum::sequencer
{
struct PlaybackEvent
{
    double positionBeats = 0.0;
    int midiNote = 36;
    std::uint8_t velocity = 100;
};

class HostPlaybackCursor
{
public:
    HostPlaybackCursor(const std::vector<PlaybackEvent>& sequenceEvents,
                       double lengthBeats,
                       double startBeat,
                       double tempoBpm,
                       double sampleRate)
        : events(sequenceEvents), patternLengthBeats(lengthBeats), hostBeat(startBeat)
    {
        if (events.empty() || !std::isfinite(patternLengthBeats) || patternLengthBeats <= 0.0
            || !std::isfinite(hostBeat) || !std::isfinite(tempoBpm) || tempoBpm <= 0.0
            || !std::isfinite(sampleRate) || sampleRate <= 0.0)
            return;

        beatsPerSample = tempoBpm / (60.0 * sampleRate);
        if (!std::isfinite(beatsPerSample) || beatsPerSample <= 0.0)
            return;

        valid = true;
        if (hostBeat >= 0.0)
        {
            patternBeat = std::fmod(hostBeat, patternLengthBeats);
            if (patternBeat < 0.0)
                patternBeat += patternLengthBeats;
            const double halfSample = beatsPerSample * 0.5;
            eventIndex = static_cast<size_t>(std::lower_bound(events.begin(), events.end(), patternBeat - halfSample,
                [](const PlaybackEvent& event, double beat) { return event.positionBeats < beat; }) - events.begin());
        }
    }

    bool isValid() const noexcept { return valid; }

    template <typename Trigger>
    void triggerAtSample(Trigger&& trigger)
    {
        if (!valid)
            return;

        if (hostBeat >= 0.0)
        {
            const double halfSample = beatsPerSample * 0.5 + 1.0e-12;
            while (eventIndex < events.size() && events[eventIndex].positionBeats <= patternBeat + halfSample)
            {
                const auto& event = events[eventIndex++];
                if (event.positionBeats >= patternBeat - halfSample)
                    trigger(event);
            }
        }

        const double previousHostBeat = hostBeat;
        hostBeat += beatsPerSample;
        if (previousHostBeat < 0.0 && hostBeat >= 0.0)
        {
            patternBeat = 0.0;
            eventIndex = 0;
        }
        else if (previousHostBeat >= 0.0)
        {
            patternBeat += beatsPerSample;
            if (patternBeat >= patternLengthBeats)
            {
                patternBeat = std::fmod(patternBeat, patternLengthBeats);
                eventIndex = 0;
            }
        }
    }

private:
    const std::vector<PlaybackEvent>& events;
    double patternLengthBeats = 0.0;
    double hostBeat = -1.0;
    double beatsPerSample = 0.0;
    double patternBeat = 0.0;
    size_t eventIndex = 0;
    bool valid = false;
};
}
