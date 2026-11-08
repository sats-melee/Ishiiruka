#include "Core/Slippi/SatsArenaGameplay.h"

#include <cassert>
#include <iostream>

int main()
{
    SatsArenaNetplay::Config config;
    std::vector<std::uint8_t> block(0x138, 0x5a);
    // PlayerInitData +7 is duplicate-fighter shade; the real flags start at +0xC.
    // Some slippi-js versions also label shade's low bit as staminaMode.
    const std::array<std::uint8_t, 4> shades = {{0, 0, 1, 1}};
    for (unsigned i = 0; i < 4; ++i)
    {
        block[0x67 + i * 0x24] = shades[i];
        block[0x6c + i * 0x24] = 0xc0;
    }
    for (unsigned count : {2u, 3u, 4u})
    {
        config.player_count = count;
        assert(SatsArenaNetplay::ApplyGameConfig(config, block));
        assert(block[8] == 0 && block[0x60] == 0x5a && block[0xE] == 0x5a);
        for (unsigned i = 0; i < 4; ++i)
        {
            assert(block[0x61 + i * 0x24] == (i < count ? 0 : 3));
            assert(block[0x69 + i * 0x24] == 255);
            assert(block[0x67 + i * 0x24] == shades[i]);
            assert(block[0x6c + i * 0x24] == 0xc0);
        }
    }
    config.teams = true;
    // Uneven teams and all three team colors are explicit, never randomized.
    for (const auto& teams : {std::array<unsigned, 4>{{0, 1, 0, 1}},
                              std::array<unsigned, 4>{{2, 0, 0, 0}},
                              std::array<unsigned, 4>{{0, 1, 2, 2}}})
    {
        config.team_ids = teams;
        assert(SatsArenaNetplay::ApplyGameConfig(config, block) && block[8] == 1);
        for (unsigned i = 0; i < 4; ++i)
        {
            assert(block[0x69 + i * 0x24] == teams[i]);
            assert(block[0x67 + i * 0x24] == shades[i]);
            assert(block[0x6c + i * 0x24] == 0xc0);
        }
    }
    assert(SatsArenaNetplay::SelectionSlots(config, {3, 1, 0, 2}));
    for (const auto& slots : {std::vector<unsigned>{0, 0, 1, 2}, {0, 1, 2, 4}, {0, 1, 2}})
        assert(!SatsArenaNetplay::SelectionSlots(config, slots));
    const auto previous = block;
    config.team_ids.fill(0);
    assert(!SatsArenaNetplay::ApplyGameConfig(config, block) && block == previous);
    config.team_ids = {{0, 1, 2, 3}};
    assert(!SatsArenaNetplay::ApplyGameConfig(config, block) && block == previous);
    config.player_count = 3; config.team_ids = {{0, 1, 1, 255}};
    assert(SatsArenaNetplay::ApplyGameConfig(config, block));
    assert(block[0x61 + 3 * 0x24] == 3 && block[0x69 + 3 * 0x24] == 255);
    config.player_count = 2; config.teams = false; config.team_ids.fill(255);
    assert(SatsArenaNetplay::ApplyGameConfig(config, block) && block[8] == 0);
    assert(block[0x61 + 2 * 0x24] == 3 && block[0x61 + 3 * 0x24] == 3);
    assert(SatsArenaNetplay::SelectionSlots(config, {1, 0}));
    config.player_count = 5;
    assert(!SatsArenaNetplay::ApplyGameConfig(config, block));
    assert(!SatsArenaNetplay::SelectionSlots(config, {0, 1, 2, 3, 4}));
    config.player_count = 2;
    block.resize(0x137);
    assert(!SatsArenaNetplay::ApplyGameConfig(config, block));
    std::cout << "PASS: canonical 2–4 player FFA/teams, immutable team IDs, stale-slot reset, peer selection roster\n";
}
