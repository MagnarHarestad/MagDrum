#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

class DeviceRealtimeRenderer final : private juce::AudioIODeviceCallback
{
public:
    using ShouldCancel = std::function<bool()>;

    explicit DeviceRealtimeRenderer(juce::AudioProcessor&);
    ~DeviceRealtimeRenderer() override;

    bool open(juce::String& error);
    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    void attach();
    void detach();
    bool render(int midiNote, int velocity, int noteSamples, juce::AudioBuffer<float>& capture,
                ShouldCancel shouldCancel, bool& cancelled);

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int totalNumInputChannels,
        float* const* outputChannelData, int totalNumOutputChannels, int numSamples,
        const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void finishFromAudioThread(bool wasCancelled, bool failed = false) noexcept;

    juce::AudioProcessor& plugin;
    juce::AudioDeviceManager deviceManager;
    juce::AudioBuffer<float> processBuffer;
    juce::AudioBuffer<float> processView;
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float>* captureBuffer = nullptr;
    double sampleRate = 0.0;
    int blockSize = 0;
    int note = 0;
    int velocity = 0;
    int noteLengthSamples = 0;
    int framesCaptured = 0;
    std::atomic<bool> requestActive { false };
    std::atomic<bool> cancellationRequested { false };
    std::atomic<bool> requestFinished { false };
    std::atomic<bool> requestCancelled { false };
    std::atomic<bool> requestFailed { false };
    std::mutex waitMutex;
    std::condition_variable waitCondition;
    bool callbackAttached = false;
};
