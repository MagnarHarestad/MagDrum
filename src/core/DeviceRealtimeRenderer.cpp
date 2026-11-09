#include "DeviceRealtimeRenderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>

DeviceRealtimeRenderer::DeviceRealtimeRenderer(juce::AudioProcessor& processor)
    : plugin(processor)
{
}

DeviceRealtimeRenderer::~DeviceRealtimeRenderer()
{
    detach();
}

bool DeviceRealtimeRenderer::open(juce::String& error)
{
    const auto requestedOutputs = juce::jlimit(1, 2, plugin.getTotalNumOutputChannels());
    error = deviceManager.initialise(0, requestedOutputs, nullptr, true);
    if (error.isNotEmpty())
        return false;

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        error = "No usable audio output device is available for real-time plugin scanning.";
        return false;
    }

    sampleRate = device->getCurrentSampleRate();
    blockSize = device->getCurrentBufferSizeSamples();
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0
        || blockSize < 16 || blockSize > 8192)
    {
        error = "The selected audio device reported an unsupported sample rate or buffer size.";
        return false;
    }

    const int processChannels = juce::jmax(1, juce::jmax(plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels()));
    processBuffer.setSize(processChannels, blockSize);
    processView.setSize(processChannels, blockSize);
    processView.setDataToReferTo(processBuffer.getArrayOfWritePointers(), processChannels, 0, blockSize);
    midiBuffer.ensureSize(256);
    return true;
}

void DeviceRealtimeRenderer::attach()
{
    if (!callbackAttached)
    {
        deviceManager.addAudioCallback(this);
        callbackAttached = true;
    }
}

void DeviceRealtimeRenderer::detach()
{
    if (callbackAttached)
    {
        requestActive.store(false, std::memory_order_release);
        deviceManager.removeAudioCallback(this);
        callbackAttached = false;
    }
    captureBuffer = nullptr;
}

bool DeviceRealtimeRenderer::render(int midiNote, int midiVelocity, int noteSamples,
    juce::AudioBuffer<float>& capture, ShouldCancel shouldCancel, bool& cancelled)
{
    cancelled = false;
    if (!callbackAttached || capture.getNumChannels() != plugin.getTotalNumOutputChannels()
        || capture.getNumSamples() <= 0 || noteSamples < 1)
        return false;

    note = midiNote;
    velocity = midiVelocity;
    noteLengthSamples = noteSamples;
    framesCaptured = 0;
    captureBuffer = &capture;
    cancellationRequested.store(false, std::memory_order_relaxed);
    requestCancelled.store(false, std::memory_order_relaxed);
    requestFailed.store(false, std::memory_order_relaxed);
    requestFinished.store(false, std::memory_order_relaxed);
    requestActive.store(true, std::memory_order_release);

    const auto timeout = std::chrono::duration<double>(
        static_cast<double>(capture.getNumSamples()) / sampleRate + 5.0);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(waitMutex);
    while (!requestFinished.load(std::memory_order_acquire))
    {
        if (shouldCancel && shouldCancel())
            cancellationRequested.store(true, std::memory_order_release);

        if (waitCondition.wait_for(lock, std::chrono::milliseconds(20), [this]
            { return requestFinished.load(std::memory_order_acquire); }))
            break;

        if (std::chrono::steady_clock::now() < deadline)
            continue;

        // Stop the callback before releasing the destination buffer if a device stops responding.
        lock.unlock();
        detach();
        lock.lock();
        captureBuffer = nullptr;
        return false;
    }

    cancelled = requestCancelled.load(std::memory_order_acquire);
    const bool failed = requestFailed.load(std::memory_order_acquire);
    captureBuffer = nullptr;
    return !cancelled && !failed;
}

void DeviceRealtimeRenderer::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
    int totalNumInputChannels, float* const* outputChannelData, int totalNumOutputChannels,
    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (!requestActive.load(std::memory_order_acquire) || captureBuffer == nullptr)
        return;

    int callbackOffset = 0;
    while (callbackOffset < numSamples && requestActive.load(std::memory_order_relaxed))
    {
        if (cancellationRequested.load(std::memory_order_acquire))
        {
            finishFromAudioThread(true);
            return;
        }

        const int count = juce::jmin(blockSize, numSamples - callbackOffset,
            captureBuffer->getNumSamples() - framesCaptured);
        if (count <= 0)
        {
            finishFromAudioThread(false);
            return;
        }

        processBuffer.clear();
        const int pluginInputs = juce::jmin(plugin.getTotalNumInputChannels(), processBuffer.getNumChannels());
        for (int channel = 0; channel < pluginInputs && channel < totalNumInputChannels; ++channel)
        {
            if (inputChannelData[channel] != nullptr)
                processBuffer.copyFrom(channel, 0, inputChannelData[channel] + callbackOffset, count);
        }

        midiBuffer.clear();
        if (framesCaptured == 0)
            midiBuffer.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(velocity)), 0);
        if (noteLengthSamples >= framesCaptured && noteLengthSamples < framesCaptured + count)
            midiBuffer.addEvent(juce::MidiMessage::noteOff(1, note), noteLengthSamples - framesCaptured);

        processView.setDataToReferTo(processBuffer.getArrayOfWritePointers(), processBuffer.getNumChannels(), 0, count);
        plugin.processBlock(processView, midiBuffer);

        const int pluginOutputs = juce::jmin(plugin.getTotalNumOutputChannels(), processBuffer.getNumChannels());
        for (int channel = 0; channel < captureBuffer->getNumChannels(); ++channel)
        {
            const int sourceChannel = juce::jmin(channel, juce::jmax(0, pluginOutputs - 1));
            captureBuffer->copyFrom(channel, framesCaptured, processBuffer, sourceChannel, 0, count);
        }
        for (int channel = 0; channel < totalNumOutputChannels; ++channel)
        {
            if (outputChannelData[channel] == nullptr || pluginOutputs == 0)
                continue;
            const int sourceChannel = juce::jmin(channel, pluginOutputs - 1);
            juce::FloatVectorOperations::copy(outputChannelData[channel] + callbackOffset,
                processBuffer.getReadPointer(sourceChannel), count);
        }

        framesCaptured += count;
        callbackOffset += count;
        if (framesCaptured >= captureBuffer->getNumSamples())
            finishFromAudioThread(false);
    }
}

void DeviceRealtimeRenderer::audioDeviceAboutToStart(juce::AudioIODevice*)
{
}

void DeviceRealtimeRenderer::audioDeviceStopped()
{
    if (requestActive.load(std::memory_order_acquire))
        finishFromAudioThread(false, true);
}

void DeviceRealtimeRenderer::finishFromAudioThread(bool wasCancelled, bool failed) noexcept
{
    requestCancelled.store(wasCancelled, std::memory_order_relaxed);
    requestFailed.store(failed, std::memory_order_relaxed);
    requestActive.store(false, std::memory_order_release);
    requestFinished.store(true, std::memory_order_release);
    waitCondition.notify_one();
}
