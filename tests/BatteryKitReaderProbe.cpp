#include "core/BatteryKitReader.h"

#include <algorithm>
#include <iostream>

int main(int argc, char** argv)
{
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
