#include "core/BatteryKitReader.h"
#include "HeadlessProcessSafety.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
using Bytes = std::vector<std::uint8_t>;

void appendLittle(Bytes& target, std::uint64_t value, std::size_t width)
{
    for (std::size_t index = 0; index < width; ++index)
        target.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
}

void appendTag(Bytes& target, std::string_view tag)
{
    target.insert(target.end(), tag.begin(), tag.end());
}

void appendText(Bytes& target, std::string_view text)
{
    appendLittle(target, text.size(), 4);
    for (const auto character : text)
    {
        target.push_back(static_cast<std::uint8_t>(character));
        target.push_back(0);
    }
}

Bytes makeBaseClass()
{
    Bytes result;
    appendTag(result, "DSIN");
    appendLittle(result, 1, 4);
    appendLittle(result, 1, 4);
    result.resize(24, 0);
    return result;
}

Bytes makeSubclass(std::string_view tag, std::uint32_t type, const Bytes& payload)
{
    const auto base = makeBaseClass();
    Bytes result;
    appendTag(result, tag);
    appendLittle(result, type, 4);
    appendLittle(result, 1, 4);
    appendLittle(result, base.size(), 8);
    result.insert(result.end(), base.begin(), base.end());
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

Bytes makeObject(const Bytes& classData, const Bytes& body = {})
{
    Bytes result;
    appendLittle(result, 48 + classData.size() + body.size(), 8);
    appendLittle(result, 1, 4);
    appendTag(result, "hsin");
    appendLittle(result, 1, 4);
    appendLittle(result, 0, 4);
    result.resize(40, 0);
    appendLittle(result, classData.size(), 8);
    result.insert(result.end(), classData.begin(), classData.end());
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

void appendChunk(Bytes& target, std::uint16_t tag, const Bytes& payload)
{
    appendLittle(target, tag, 2);
    appendLittle(target, payload.size(), 4);
    target.insert(target.end(), payload.begin(), payload.end());
}

Bytes makeSyntheticBatteryKit()
{
    Bytes paths;
    appendLittle(paths, 3, 2);
    appendLittle(paths, 0, 4);

    Bytes cells;
    appendLittle(cells, 128, 4);
    for (int index = 0; index < 128; ++index)
    {
        appendLittle(cells, 1, 1);
        appendLittle(cells, 149, 2);
        appendLittle(cells, 0, 4);
        Bytes name;
        std::string cellName = "Cell ";
        cellName += std::to_string(index);
        appendText(name, cellName);
        appendLittle(cells, name.size(), 4);
        cells.insert(cells.end(), name.begin(), name.end());
        appendLittle(cells, 0, 4);
    }

    Bytes zones;
    appendLittle(zones, 0, 4);
    Bytes kitBody;
    appendChunk(kitBody, 51, cells);
    appendChunk(kitBody, 52, zones);

    Bytes kit;
    appendLittle(kit, 1, 1);
    appendLittle(kit, 163, 2);
    Bytes grid(93, 0);
    grid[48] = 1;
    grid[52] = 1;
    appendLittle(kit, grid.size(), 4);
    kit.insert(kit.end(), grid.begin(), grid.end());
    Bytes kitName;
    appendText(kitName, "Synthetic lifetime fixture");
    appendLittle(kit, kitName.size(), 4);
    kit.insert(kit.end(), kitName.begin(), kitName.end());
    appendLittle(kit, kitBody.size(), 4);
    kit.insert(kit.end(), kitBody.begin(), kitBody.end());

    Bytes legacy;
    appendChunk(legacy, 75, paths);
    appendChunk(legacy, 40, kit);

    Bytes documentPayload;
    appendLittle(documentPayload, 1, 4);
    appendLittle(documentPayload, legacy.size(), 8);
    documentPayload.insert(documentPayload.end(), legacy.begin(), legacy.end());
    documentPayload.resize(documentPayload.size() + 16, 0);
    const auto document = makeObject(makeSubclass("DSIN", 109, documentPayload));

    // FastLZ level 1 literal-only stream. Each control byte encodes at most 32 literals.
    Bytes compressed;
    std::size_t offset = 0;
    const auto firstCount = std::min<std::size_t>(32, document.size());
    compressed.push_back(static_cast<std::uint8_t>(firstCount - 1));
    compressed.insert(compressed.end(), document.begin(), document.begin() + static_cast<std::ptrdiff_t>(firstCount));
    offset += firstCount;
    while (offset < document.size())
    {
        const auto count = std::min<std::size_t>(32, document.size() - offset);
        compressed.push_back(static_cast<std::uint8_t>(count - 1));
        compressed.insert(compressed.end(), document.begin() + static_cast<std::ptrdiff_t>(offset),
                          document.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }

    Bytes compressedPayload;
    appendLittle(compressedPayload, 0, 1);
    appendLittle(compressedPayload, compressed.size(), 4);
    compressedPayload.insert(compressedPayload.end(), compressed.begin(), compressed.end());
    compressedPayload.resize(compressedPayload.size() + 8, 0);
    const auto outerClass = makeSubclass("DSIN", 115, compressedPayload);
    const auto markerObject = makeObject(makeBaseClass());
    Bytes outerBody;
    appendLittle(outerBody, 0, 4);
    appendTag(outerBody, "4KIN");
    appendLittle(outerBody, 3, 4);
    outerBody.insert(outerBody.end(), markerObject.begin(), markerObject.end());
    return makeObject(outerClass, outerBody);
}

int verifySyntheticBufferLifetime()
{
    auto bytes = makeSyntheticBatteryKit();
    const auto result = magdrum::battery::readKit(bytes.data(), bytes.size());
    if (!result.kit)
    {
        std::cerr << "Synthetic Battery fixture was rejected: " << result.error << '\n';
        return 10;
    }

    bytes.clear();
    bytes.shrink_to_fit();
    const auto& kit = *result.kit;
    if (kit.name != "Synthetic lifetime fixture" || kit.rows != 1 || kit.columns != 1
        || kit.cells.size() != 128 || kit.cells.front().name != "Cell 0"
        || kit.cells[64].name != "Cell 64" || kit.cells.back().name != "Cell 127")
    {
        std::cerr << "Parsed Battery kit fields did not survive source-buffer release.\n";
        return 11;
    }

    const auto truncated = makeSyntheticBatteryKit();
    const std::vector<std::size_t> truncationPoints{
        0, 1, 47, 48, truncated.size() / 4, truncated.size() / 2, truncated.size() - 1};
    for (const auto length : truncationPoints)
        if (magdrum::battery::readKit(truncated.data(), length).kit)
        {
            std::cerr << "Truncated Battery fixture was accepted at " << length << " bytes.\n";
            return 12;
        }
    return 0;
}
}

int main(int argc, char** argv)
{
    magdrum::tests::suppressWindowsErrorDialogs();
    if (argc == 2 && std::string_view(argv[1]) == "--verify-headless-error-mode")
        return magdrum::tests::windowsErrorDialogsAreSuppressed() ? 0 : 4;
    if (argc == 2 && std::string_view(argv[1]) == "--self-test-buffer-lifetime")
        return verifySyntheticBufferLifetime();

    if (argc != 2)
    {
        std::cerr << "Usage: MagDrumBatteryKitProbe <kit.nbkt>\n";
        return 2;
    }

    const auto result = magdrum::battery::readKit(juce::File(argv[1]));
    if (!result.kit)
    {
        std::cerr << result.error << '\n';
        return 1;
    }

    int mappedCells = 0;
    size_t zones = 0;
    for (const auto& cell : result.kit->cells)
    {
        mappedCells += cell.zones.empty() ? 0 : 1;
        zones += cell.zones.size();
    }
    std::cout << result.kit->name << ": " << result.kit->rows << "x" << result.kit->columns
              << ", " << mappedCells << " mapped cells, " << zones << " zones, "
              << result.kit->samplePaths.size() << " sample paths\n";
    for (size_t index = 0; index < std::min<size_t>(8, result.kit->samplePaths.size()); ++index)
        std::cout << "sample[" << index << "]=" << result.kit->samplePaths[index] << '\n';
    for (size_t cellIndex = 0; cellIndex < std::min<size_t>(4, result.kit->cells.size()); ++cellIndex)
    {
        const auto& cell = result.kit->cells[cellIndex];
        if (cell.zones.empty()) continue;
        if (cellIndex == 0)
        {
            std::cout << "cell[" << cell.cellIndex << "] " << cell.name << " zones=" << cell.zones.size()
                      << '\n';
            for (const auto& zone : cell.zones)
            {
                std::cout << "zone[" << zone.zoneIndex << "] notes="
                          << zone.minimumNote << "-" << zone.maximumNote << " velocity="
                          << zone.minimumVelocity << "-" << zone.maximumVelocity
                          << " sample=" << zone.path << '\n';
            }
        }
        else
            std::cout << "cell[" << cell.cellIndex << "] " << cell.name
                      << " zones=" << cell.zones.size() << '\n';
    }
    for (const auto& cell : result.kit->cells)
        for (const auto& zone : cell.zones)
            if (zone.minimumNote <= 54 && zone.maximumNote >= 54)
                std::cout << "note54 cell[" << cell.cellIndex << "] " << cell.name << " zone["
                          << zone.zoneIndex << "] notes=" << zone.minimumNote << '-'
                          << zone.maximumNote << " velocity=" << zone.minimumVelocity << '-'
                          << zone.maximumVelocity << " sample=" << zone.path << '\n';
    return zones > 0 ? 0 : 3;
}
