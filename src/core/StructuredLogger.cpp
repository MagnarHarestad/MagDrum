#include "StructuredLogger.h"

namespace
{
juce::CriticalSection loggerLock;
juce::File currentLogFile;
constexpr std::int64_t maximumLogBytes = 2 * 1024 * 1024;

juce::String levelName(StructuredLogger::Level level)
{
    switch (level)
    {
        case StructuredLogger::Level::info: return "info";
        case StructuredLogger::Level::warning: return "warning";
        case StructuredLogger::Level::error: return "error";
    }
    return "info";
}
}

void StructuredLogger::initialise(const juce::File& logDirectory)
{
    const juce::ScopedLock lock(loggerLock);
    if (!logDirectory.isDirectory() && !logDirectory.createDirectory().wasOk())
        return;
    currentLogFile = logDirectory.getChildFile("magdrum.jsonl");
}

juce::String StructuredLogger::formatEvent(Level level, const juce::String& event, Fields fields,
                                           const juce::String& timestamp)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("timestamp", timestamp.isNotEmpty() ? timestamp : juce::Time::getCurrentTime().toISO8601(true));
    object->setProperty("level", levelName(level));
    object->setProperty("event", event);
    for (const auto& field : fields)
        object->setProperty(field.first, field.second);
    return juce::JSON::toString(juce::var(object.release()), true);
}

void StructuredLogger::log(Level level, const juce::String& event, Fields fields)
{
    const juce::ScopedLock lock(loggerLock);
    if (currentLogFile == juce::File())
    {
        auto directory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("MagDrum").getChildFile("logs");
        if (!directory.isDirectory() && !directory.createDirectory().wasOk())
            return;
        currentLogFile = directory.getChildFile("magdrum.jsonl");
    }

    if (currentLogFile.getSize() >= maximumLogBytes)
    {
        auto previous = currentLogFile.getSiblingFile("magdrum.previous.jsonl");
        previous.deleteFile();
        if (!currentLogFile.moveFileTo(previous))
            return;
    }

    juce::FileOutputStream output(currentLogFile);
    if (!output.openedOk()) return;
    output.setPosition(currentLogFile.getSize());
    output.writeText(formatEvent(level, event, fields), false, false, "\n");
    output.flush();
}

juce::File StructuredLogger::getLogFile()
{
    const juce::ScopedLock lock(loggerLock);
    return currentLogFile;
}
