#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Slippi/SatsArenaNetplayRoom.h"

namespace SatsArenaNetplay
{
// The immutable room roster also owns Melee's actual team and slot settings.
inline bool ApplyGameConfig(const Config& config, std::vector<std::uint8_t>& block)
{
    if (block.size() < 0x138 || config.player_count < 2 || config.player_count > 4) return false;
    unsigned sides = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto team = config.team_ids[i];
        if (config.teams && i < config.player_count)
        {
            if (team > 2) return false;
            sides |= 1u << team;
        }
        else if (team != 255) return false;
    }
    if (config.teams && (sides & (sides - 1)) == 0) return false;
    block[0x8] = config.teams;
    for (unsigned i = 0; i < 4; ++i)
    {
        block[0x61 + 0x24 * i] = i < config.player_count ? 0 : 3;
        block[0x69 + 0x24 * i] = static_cast<std::uint8_t>(config.team_ids[i]);
    }
    return true;
}

inline bool SelectionSlots(const Config& config, const std::vector<unsigned>& slots)
{
    if (slots.size() != config.player_count || config.player_count < 2 || config.player_count > 4)
        return false;
    unsigned seen = 0;
    for (auto slot : slots)
    {
        if (slot >= config.player_count || (seen & (1u << slot))) return false;
        seen |= 1u << slot;
    }
    return true;
}
}
