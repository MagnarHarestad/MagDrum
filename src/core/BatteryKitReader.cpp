#include "BatteryKitReader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// The bounded Hsin/DSIN and Battery 4 record layout follows the GPL-3.0
// implementation in joanroig/nitools (src/formats/ni_container.py and
// src/formats/battery.py). See THIRD-PARTY-NOTICES.md for attribution.

namespace
{
using Bytes = std::span<const std::uint8_t>;

class FormatError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

std::uint64_t little(Bytes data, size_t offset, size_t width)
{
    if (width > 8 || offset > data.size() || width > data.size() - offset)
        throw FormatError("Truncated Battery kit data.");
    std::uint64_t value = 0;
    for (size_t i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8);
    return value;
}

class Reader
{
public:
    explicit Reader(Bytes bytes, size_t sourceOffset = 0) : data(bytes), baseOffset(sourceOffset) {}

    size_t position() const noexcept { return cursor; }
    size_t remaining() const noexcept { return data.size() - cursor; }
    Bytes rest() const noexcept { return data.subspan(cursor); }

    Bytes take(size_t count)
    {
        if (count > remaining())
            throw FormatError("Truncated Battery kit record at byte " + std::to_string(baseOffset + cursor));
        const auto result = data.subspan(cursor, count);
        cursor += count;
        return result;
    }

    std::uint8_t byte() { return static_cast<std::uint8_t>(number(1)); }
    std::uint16_t word() { return static_cast<std::uint16_t>(number(2)); }
    std::uint32_t dword() { return static_cast<std::uint32_t>(number(4)); }
    std::uint64_t qword() { return number(8); }

    Reader sub(size_t count)
    {
        const auto offset = baseOffset + cursor;
        return Reader(take(count), offset);
    }

    void finish() const
    {
        if (cursor != data.size())
            throw FormatError("Unexpected Battery data at byte " + std::to_string(baseOffset + cursor)
                + " (" + std::to_string(remaining()) + " bytes remain).");
    }

    juce::String text()
    {
        const auto units = dword();
        if (units > 32768)
            throw FormatError("Battery text field is too large.");
        const auto encoded = take(static_cast<size_t>(units) * 2);
        std::string utf8;
        utf8.reserve(encoded.size());
        for (size_t i = 0; i < encoded.size(); i += 2)
        {
            std::uint32_t cp = static_cast<std::uint32_t>(encoded[i])
                | (static_cast<std::uint32_t>(encoded[i + 1]) << 8);
            if (cp == 0)
                throw FormatError("Battery text contains an embedded NUL.");
            if (cp >= 0xd800 && cp <= 0xdbff)
            {
                if (i + 3 >= encoded.size())
                    throw FormatError("Invalid Battery UTF-16 text.");
                const auto low = static_cast<std::uint32_t>(encoded[i + 2])
                    | (static_cast<std::uint32_t>(encoded[i + 3]) << 8);
                if (low < 0xdc00 || low > 0xdfff)
                    throw FormatError("Invalid Battery UTF-16 text.");
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
            else if (cp >= 0xdc00 && cp <= 0xdfff)
                throw FormatError("Invalid Battery UTF-16 text.");

            if (cp < 0x80) utf8.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                utf8.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                utf8.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            else if (cp < 0x10000)
            {
                utf8.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                utf8.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            else
            {
                utf8.push_back(static_cast<char>(0xf0 | (cp >> 18)));
                utf8.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
                utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                utf8.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
        }
        return juce::String::fromUTF8(utf8.data(), static_cast<int>(utf8.size()));
    }

private:
    std::uint64_t number(size_t width)
    {
        const auto value = little(data, cursor, width);
        cursor += width;
        return value;
    }
    Bytes data;
    size_t baseOffset = 0;
    size_t cursor = 0;
};

struct ClassHeader
{
    std::array<char, 4> tag{};
    std::uint32_t typeId = 0;
    Bytes payload;
    std::unique_ptr<ClassHeader> base;
    const ClassHeader* find(const char (&wantedTag)[5], std::uint32_t wantedType) const
    {
        for (auto* current = this; current != nullptr; current = current->base.get())
            if (std::memcmp(current->tag.data(), wantedTag, 4) == 0 && current->typeId == wantedType)
                return current;
        return nullptr;
    }
};

struct Object
{
    ClassHeader header;
    std::array<char, 4> referenceTag{};
    std::uint32_t referenceType = 0;
    std::vector<std::unique_ptr<Object>> children;
};

ClassHeader readClass(Bytes bytes, size_t offset, int depth)
{
    if (depth > 64 || bytes.size() < 12)
        throw FormatError("Invalid NI class header.");
    ClassHeader result;
    std::memcpy(result.tag.data(), bytes.data(), 4);
    result.typeId = static_cast<std::uint32_t>(little(bytes, 4, 4));
    const auto version = little(bytes, 8, 4);
    if (version != 1)
        throw FormatError("Unsupported NI class version.");

    if (std::memcmp(result.tag.data(), "DSIN", 4) == 0 && result.typeId == 1)
    {
        if (bytes.size() != 24)
            throw FormatError("Invalid NI base class size.");
        result.payload = bytes.subspan(12);
        return result;
    }
    if (bytes.size() < 20)
        throw FormatError("Truncated NI class.");
    const auto baseSize = little(bytes, 12, 8);
    if (baseSize == 0 || baseSize > bytes.size() - 20)
        throw FormatError("Invalid NI base class length.");
    result.base = std::make_unique<ClassHeader>(readClass(bytes.subspan(20, static_cast<size_t>(baseSize)), offset + 20, depth + 1));
    result.payload = bytes.subspan(20 + static_cast<size_t>(baseSize));
    return result;
}

Object readObject(Bytes bytes, size_t offset, int depth, size_t& consumed)
{
    if (depth > 64 || bytes.size() < 48)
        throw FormatError("Truncated NI object.");
    const auto objectSize = little(bytes, 0, 8);
    if (objectSize < 48 || objectSize > bytes.size())
        throw FormatError("Invalid NI object length at byte " + std::to_string(offset)
            + " (declares " + std::to_string(objectSize) + " bytes, has "
            + std::to_string(bytes.size()) + ").");
    if (little(bytes, 8, 4) != 1 || std::memcmp(bytes.data() + 12, "hsin", 4) != 0
        || little(bytes, 16, 4) != 1 || little(bytes, 20, 4) != 0)
        throw FormatError("Unsupported NI object header.");
    const auto headerSize = little(bytes, 40, 8);
    if (headerSize > objectSize - 48)
        throw FormatError("Invalid NI object class length.");
    const auto objectBytes = bytes.first(static_cast<size_t>(objectSize));
    Object result;
    result.header = readClass(objectBytes.subspan(48, static_cast<size_t>(headerSize)), offset + 48, 0);
    size_t cursor = 48 + static_cast<size_t>(headerSize);
    while (cursor < objectBytes.size())
    {
        if (objectBytes.size() - cursor < 12)
            throw FormatError("Truncated NI object reference.");
        auto child = std::make_unique<Object>();
        const auto fieldId = little(objectBytes, cursor, 4);
        (void) fieldId;
        child->referenceType = static_cast<std::uint32_t>(little(objectBytes, cursor + 8, 4));
        size_t childSize = 0;
        *child = readObject(objectBytes.subspan(cursor + 12), offset + cursor + 12, depth + 1, childSize);
        std::memcpy(child->referenceTag.data(), objectBytes.data() + cursor + 4, 4);
        child->referenceType = static_cast<std::uint32_t>(little(objectBytes, cursor + 8, 4));
        result.children.push_back(std::move(child));
        cursor += 12 + childSize;
    }
    consumed = static_cast<size_t>(objectSize);
    return result;
}

std::vector<const Object*> walk(const Object& root)
{
    std::vector<const Object*> objects;
    std::vector<const Object*> pending{&root};
    while (!pending.empty())
    {
        const auto* current = pending.back();
        pending.pop_back();
        objects.push_back(current);
        for (const auto& child : current->children)
            pending.push_back(child.get());
    }
    return objects;
}

Object readContainer(Bytes bytes)
{
    size_t consumed = 0;
    auto root = readObject(bytes, 0, 0, consumed);
    if (consumed != bytes.size())
        throw FormatError("Trailing bytes after NI object.");
    return root;
}

std::vector<std::uint8_t> decompressFastLz(Bytes source, size_t limit = 64u * 1024u * 1024u)
{
    if (source.empty())
        throw FormatError("Empty Battery compressed stream.");
    const auto first = source[0];
    const auto level = static_cast<unsigned>((first >> 5) + 1);
    if (level != 1 && level != 2)
        throw FormatError("Unsupported Battery compression level.");
    size_t cursor = 1;
    auto readByte = [&]() -> std::uint8_t
    {
        if (cursor >= source.size())
            throw FormatError("Truncated Battery compressed stream.");
        return source[cursor++];
    };
    auto control = static_cast<unsigned>(first & 31);
    std::vector<std::uint8_t> output;
    while (true)
    {
        if (control < 32)
        {
            const size_t count = control + 1;
            if (count > source.size() - cursor || count > limit - output.size())
                throw FormatError("Invalid Battery compressed literal length.");
            output.insert(output.end(), source.begin() + static_cast<std::ptrdiff_t>(cursor),
                          source.begin() + static_cast<std::ptrdiff_t>(cursor + count));
            cursor += count;
        }
        else
        {
            size_t count = (control >> 5) + 2;
            const auto high = (control & 31) << 8;
            if (count == 9)
            {
                while (true)
                {
                    const auto extra = readByte();
                    count += extra;
                    if (level == 1 || extra != 255)
                        break;
                }
            }
            const auto low = readByte();
            size_t distance = high + low + 1;
            if (level == 2 && high == 7936 && low == 255)
                distance = (static_cast<size_t>(readByte()) << 8) + readByte() + 8192;
            if (distance > output.size() || count > limit - output.size())
                throw FormatError("Invalid Battery compressed back-reference.");
            for (size_t i = 0; i < count; ++i)
            {
                const auto value = output[output.size() - distance];
                output.push_back(value);
            }
        }
        if (cursor == source.size())
            return output;
        control = readByte();
    }
}

const ClassHeader& findHeader(const Object& root, const char (&tag)[5], std::uint32_t type)
{
    const ClassHeader* found = nullptr;
    for (const auto* object : walk(root))
        if (const auto* header = object->header.find(tag, type))
        {
            if (found != nullptr)
                throw FormatError("Ambiguous Battery container class.");
            found = header;
        }
    if (found == nullptr)
        throw FormatError("Battery kit is missing a required container class.");
    return *found;
}

std::vector<std::uint8_t> decodeBatteryDocument(Bytes raw)
{
    const auto outer = readContainer(raw);
    const auto outerObjects = walk(outer);
    if (std::none_of(outerObjects.begin(), outerObjects.end(), [](const Object* object)
        { return object->referenceType == 3 && std::memcmp(object->referenceTag.data(), "4KIN", 4) == 0; }))
        throw FormatError("The selected file is not a supported Battery 4 kit.");

    const auto& compressed = findHeader(outer, "DSIN", 115);
    Reader compressedReader(compressed.payload);
    if (compressedReader.byte() != 0)
        throw FormatError("Unsupported Battery compression header.");
    const auto compressedSize = compressedReader.dword();
    auto compressedBytes = compressedReader.take(compressedSize);
    auto data = decompressFastLz(compressedBytes);
    compressedReader.take(8); // FastLZ bookkeeping stored by Battery.
    compressedReader.finish();

    const auto document = readContainer(data);
    const auto& header = findHeader(document, "DSIN", 109);
    Reader payload(header.payload);
    if (payload.dword() != 1)
        throw FormatError("Unsupported Battery document version.");
    auto legacy = payload.sub(payload.qword());
    payload.take(16);
    payload.finish();
    const auto legacyBytes = legacy.rest();
    return {legacyBytes.begin(), legacyBytes.end()};
}

using Chunks = std::vector<std::pair<std::uint16_t, Reader>>;

Chunks readChunks(Reader& reader)
{
    Chunks result;
    while (reader.remaining() > 0)
    {
        if (reader.remaining() < 6)
            throw FormatError("Truncated Battery chunk.");
        const auto tag = reader.word();
        const auto length = reader.dword();
        result.emplace_back(tag, reader.sub(length));
    }
    return result;
}

Reader takeOnlyChunk(Chunks& chunks, std::uint16_t wanted)
{
    Reader* match = nullptr;
    for (auto& [tag, payload] : chunks)
        if (tag == wanted)
        {
            if (match != nullptr)
                throw FormatError("Duplicate Battery data chunk.");
            match = &payload;
        }
    if (match == nullptr)
        throw FormatError("Battery kit is missing a required data chunk.");
    return *match;
}

juce::String readPath(Reader& reader, bool auxiliary, bool& embedded)
{
    const auto count = reader.dword();
    if (count > 256)
        throw FormatError("Battery sample path has too many components.");
    std::vector<juce::String> parts;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto token = reader.byte();
        if (token == 1)
        {
            if (index != 0 || reader.text().isNotEmpty())
                throw FormatError("Unsupported Battery volume root.");
            parts.emplace_back();
        }
        else if (token == 6 && auxiliary && index == 0)
            embedded = true;
        else if (token == 8 && auxiliary)
        {
            reader.text();
            embedded = true;
        }
        else if (token == 3)
            parts.emplace_back("..");
        else if (token == 2 || token == 4)
        {
            const auto part = reader.text();
            if (part.isEmpty() || part.containsChar('/') || part.containsChar('\\')
                || (token == 4 && index != count - 1))
                throw FormatError("Invalid Battery sample path component.");
            parts.push_back(part);
        }
        else
            throw FormatError("Unsupported Battery sample path token.");
    }
    juce::String result;
    for (size_t index = 0; index < parts.size(); ++index)
    {
        if (index > 0)
            result << "/";
        result << parts[index];
    }
    return result;
}

std::vector<juce::String> readPaths(Reader& reader, std::uint32_t& version)
{
    version = reader.word();
    if (version == 2)
    {
        if (reader.dword() != 1)
            throw FormatError("Unsupported Battery sample path table header.");
        bool ignoredEmbedded = false;
        readPath(reader, false, ignoredEmbedded);
    }
    else if (version != 3)
        throw FormatError("Unsupported Battery sample path table version.");

    const auto count = reader.dword();
    if (count > 100000)
        throw FormatError("Battery sample table exceeds the safety limit.");
    std::vector<juce::String> paths;
    paths.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        bool embedded = false;
        if (version == 3)
        {
            const auto header = reader.take(8);
            if (std::any_of(header.begin(), header.end(), [](std::uint8_t b) { return b != 0; }))
                throw FormatError("Unsupported Battery v3 sample resource header.");
        }
        paths.push_back(readPath(reader, false, embedded));
        if (version == 3)
            reader.take(20);
    }
    if (version == 2)
    {
        reader.take(static_cast<size_t>(count) * 12);
        const auto auxiliaryCount = reader.dword();
        if (auxiliaryCount > 100000)
            throw FormatError("Battery auxiliary resource table exceeds the safety limit.");
        for (std::uint32_t i = 0; i < auxiliaryCount; ++i)
        {
            bool ignoredEmbedded = false;
            readPath(reader, true, ignoredEmbedded);
        }
        for (std::uint32_t i = 0; i < auxiliaryCount; ++i)
            if (reader.word() > 1)
                throw FormatError("Unsupported Battery auxiliary resource flags.");
    }
    reader.finish();
    return paths;
}

std::pair<std::uint16_t, Reader> readBatteryObjectParts(Reader& reader,
    const std::vector<std::uint16_t>& allowedCommonTags, Reader& commonOut, Reader& bodyOut)
{
    if (reader.byte() != 1)
        throw FormatError("Unsupported Battery object version.");
    const auto commonTag = reader.word();
    commonOut = reader.sub(reader.dword());
    if (std::find(allowedCommonTags.begin(), allowedCommonTags.end(), commonTag) == allowedCommonTags.end())
        throw FormatError("Unsupported Battery object common record.");
    auto properties = reader.sub(reader.dword());
    bodyOut = reader.sub(reader.dword());
    return {commonTag, properties};
}

void parseCells(Reader& cellReader, std::vector<magdrum::battery::Cell>& cells)
{
    const auto count = cellReader.dword();
    if (count != 128)
        throw FormatError("Unsupported Battery cell capacity.");
    cells.resize(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Reader common({}), body({});
        auto [tag, properties] = readBatteryObjectParts(cellReader, {149}, common, body);
        (void) tag;
        cells[index].cellIndex = static_cast<int>(index);
        cells[index].name = properties.text();
        // The cell's common and body records vary by Battery version; their
        // bounded payloads are intentionally retained as opaque data.
    }
    cellReader.finish();
}

magdrum::battery::Kit parseBattery(Bytes raw)
{
    auto documentBytes = decodeBatteryDocument(raw);
    Reader document(documentBytes);
    auto topChunks = readChunks(document);
    std::uint32_t pathVersion = 0;
    auto pathReader = takeOnlyChunk(topChunks, 75);
    auto samplePaths = readPaths(pathReader, pathVersion);

    auto kitReader = takeOnlyChunk(topChunks, 40);
    Reader kitCommon({}), kitBody({});
    const std::vector<std::pair<std::uint16_t, size_t>> kitSchemas{
        {std::uint16_t{163}, size_t{93}}, {std::uint16_t{164}, size_t{89}},
        {std::uint16_t{168}, size_t{99}}, {std::uint16_t{171}, size_t{99}},
        {std::uint16_t{174}, size_t{113}}, {std::uint16_t{175}, size_t{113}},
        {std::uint16_t{178}, size_t{113}}};
    const std::vector<std::uint16_t> kitTags{163, 164, 168, 171, 174, 175, 178};
    auto [kitTag, kitProperties] = readBatteryObjectParts(kitReader, kitTags, kitCommon, kitBody);
    const auto schema = std::find_if(kitSchemas.begin(), kitSchemas.end(), [kitTag](const auto& item)
        { return item.first == kitTag; });
    if (schema == kitSchemas.end() || kitCommon.remaining() != schema->second)
        throw FormatError("Unsupported Battery kit grid settings version.");
    if (kitCommon.remaining() < 56)
        throw FormatError("Truncated Battery grid settings.");
    const auto rows = static_cast<int>(little(kitCommon.rest(), 48, 4));
    const auto columns = static_cast<int>(little(kitCommon.rest(), 52, 4));
    if (rows < 1 || rows > 8 || columns < 1 || columns > 16)
        throw FormatError("Invalid Battery kit grid dimensions.");
    // Reader text consumes the length prefix at the beginning of the object property record.
    Reader nameReader = kitProperties;
    const auto kitName = nameReader.text();
    kitReader.finish();

    auto children = readChunks(kitBody);
    kitBody.finish();
    auto cellReader = takeOnlyChunk(children, 51);
    std::vector<magdrum::battery::Cell> cells;
    parseCells(cellReader, cells);

    auto zoneReader = takeOnlyChunk(children, 52);
    const auto zoneCount = zoneReader.dword();
    if (zoneCount > 100000)
        throw FormatError("Battery kit has too many sample zones.");
    for (std::uint32_t zoneIndex = 0; zoneIndex < zoneCount; ++zoneIndex)
    {
        const auto cellIndex = zoneReader.dword();
        Reader common({}), properties({}), body({});
        const auto [zoneTag, zoneProperties] = readBatteryObjectParts(zoneReader, {152, 154}, common, body);
        properties = zoneProperties;
        const size_t expectedSize = zoneTag == 152 ? 76 : 82;
        const size_t sampleOffset = zoneTag == 152 ? 42 : 48;
        if (properties.remaining() != expectedSize)
            throw FormatError("Unsupported Battery sample zone settings version.");
        const auto sampleIndex = static_cast<std::uint32_t>(little(properties.rest(), sampleOffset, 4));
        if (cellIndex >= cells.size() || sampleIndex >= samplePaths.size())
            throw FormatError("Battery sample zone references a missing cell or audio path.");
        magdrum::battery::SampleZone zone;
        zone.zoneIndex = static_cast<int>(zoneIndex);
        zone.sampleIndex = static_cast<int>(sampleIndex);
        zone.minimumNote = static_cast<int>(little(properties.rest(), 16, 2));
        zone.maximumNote = static_cast<int>(little(properties.rest(), 18, 2));
        zone.minimumVelocity = static_cast<int>(little(properties.rest(), 12, 2));
        zone.maximumVelocity = static_cast<int>(little(properties.rest(), 14, 2));
        if (zone.minimumNote < 0 || zone.maximumNote > 127 || zone.minimumNote > zone.maximumNote
            || zone.minimumVelocity < 1 || zone.maximumVelocity > 127
            || zone.minimumVelocity > zone.maximumVelocity)
            throw FormatError("Battery sample zone has an invalid key or velocity range.");
        zone.path = samplePaths[sampleIndex];
        zone.embedded = zone.path.isEmpty();
        cells[cellIndex].zones.push_back(std::move(zone));
        // The versioned settings are inspected through a bounded byte view;
        // the exact size check above makes consuming opaque fields unnecessary.
    }
    zoneReader.finish();

    magdrum::battery::Kit result;
    result.name = kitName;
    result.rows = rows;
    result.columns = columns;
    result.zoneCount = static_cast<int>(zoneCount);
    result.cells = std::move(cells);
    result.samplePaths = std::move(samplePaths);
    result.hasEmbeddedSamples = std::any_of(result.samplePaths.begin(), result.samplePaths.end(),
        [](const juce::String& path) { return path.isEmpty(); });
    return result;
}
}

magdrum::battery::ReadResult magdrum::battery::readKit(const void* data, size_t size)
{
    try
    {
        if (data == nullptr || size < 48 || size > 512u * 1024u * 1024u)
            throw FormatError("Battery kit file is empty, truncated, or exceeds the 512 MB safety limit.");
        return {parseBattery(Bytes(static_cast<const std::uint8_t*>(data), size)), {}};
    }
    catch (const std::exception& error)
    {
        return {{}, juce::String(error.what())};
    }
}

magdrum::battery::ReadResult magdrum::battery::readKit(const juce::File& file)
{
    if (!file.existsAsFile())
        return {{}, "Battery kit file does not exist."};
    const auto fileSize = file.getSize();
    if (fileSize <= 0 || fileSize > 512ll * 1024ll * 1024ll)
        return {{}, "Battery kit file is empty or exceeds the 512 MB safety limit."};
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes))
        return {{}, "Could not read the Battery kit file."};
    if (bytes.getSize() != static_cast<size_t>(fileSize))
        return {{}, "Battery kit file changed while it was being read."};
    return readKit(bytes.getData(), bytes.getSize());
}
