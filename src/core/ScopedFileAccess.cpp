#include "ScopedFileAccess.h"
#include <array>

namespace MagDrumFiles
{
bool copyURLToFile(const juce::URL& source, const juce::File& destination,
                   const std::function<bool()>& shouldCancel, juce::String& error)
{
    if (!source.isLocalFile())
    {
        error = "Choose a local file that MagDrum can import.";
        return false;
    }
    if (source.getLocalFile() == destination)
        return destination.existsAsFile();
    if (shouldCancel && shouldCancel()) return false;

    std::unique_ptr<juce::InputStream> input(source.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)));
    if (input == nullptr)
    {
        error = "MagDrum could not read the selected file.";
        return false;
    }
    const auto parent = destination.getParentDirectory();
    if (!parent.createDirectory().wasOk() && !parent.isDirectory())
    {
        error = "MagDrum could not create its shared import folder.";
        return false;
    }
    destination.deleteFile();
    auto output = destination.createOutputStream();
    if (output == nullptr)
    {
        error = "MagDrum could not create a copy of the selected file.";
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    while (!input->isExhausted())
    {
        if (shouldCancel && shouldCancel())
        {
            output.reset();
            destination.deleteFile();
            return false;
        }
        const int bytesRead = input->read(buffer.data(), static_cast<int>(buffer.size()));
        if (bytesRead < 0 || (bytesRead > 0 && !output->write(buffer.data(), static_cast<size_t>(bytesRead))))
        {
            error = "MagDrum could not finish copying the selected file.";
            output.reset();
            destination.deleteFile();
            return false;
        }
        if (bytesRead == 0) break;
    }
    output->flush();
    const bool succeeded = output->getStatus().wasOk();
    output.reset();
    if (!succeeded)
    {
        error = "MagDrum could not finish writing the selected file.";
        destination.deleteFile();
        return false;
    }
    return true;
}

bool copyFileToURL(const juce::File& source, const juce::URL& destination, juce::String& error)
{
    std::unique_ptr<juce::InputStream> input(source.createInputStream());
    std::unique_ptr<juce::OutputStream> output(destination.createOutputStream());
    if (input == nullptr || output == nullptr)
    {
        error = "MagDrum could not access the selected destination.";
        return false;
    }
    std::array<char, 16 * 1024> buffer{};
    const auto expectedBytes = input->getTotalLength();
    std::int64_t bytesWritten = 0;
    while (!input->isExhausted())
    {
        const int bytesRead = input->read(buffer.data(), static_cast<int>(buffer.size()));
        if (bytesRead < 0)
        {
            error = "MagDrum could not finish reading the file.";
            return false;
        }
        if (bytesRead == 0) break;
        if (!output->write(buffer.data(), static_cast<size_t>(bytesRead)))
        {
            error = "MagDrum could not finish writing the file.";
            return false;
        }
        bytesWritten += bytesRead;
    }
    output->flush();
    if (expectedBytes >= 0 && bytesWritten != expectedBytes)
    {
        error = "MagDrum could not finish writing the file.";
        return false;
    }
    return true;
}
}
