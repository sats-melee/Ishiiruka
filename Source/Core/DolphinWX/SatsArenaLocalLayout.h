// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#pragma once
#include <cstdint>

namespace SatsArenaLocal
{
// Admission phase3 can precede gameplay; use actual game start and new entry setup.
inline bool UseBottomStrip(bool was_strip, bool game_started, bool next_entry_setup)
{
    return game_started || (was_strip && !next_entry_setup);
}

// Some builds skip results and expose CSS immediately; wait for a fresh intent.
inline bool WantsNextSetup(bool strip, bool clicked, std::uint64_t start_requests,
                           std::uint64_t result_start_requests)
{
    return !strip || clicked || start_requests != result_start_requests;
}
}
