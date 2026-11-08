#include "DolphinWX/SatsArenaLocalLayout.h"

#include <cassert>
#include <iostream>

int main()
{
    bool strip = false;
    const auto update = [&](bool game_started, bool next_entry_setup) {
        strip = SatsArenaLocal::UseBottomStrip(strip, game_started, next_entry_setup);
        return strip;
    };

    assert(!update(false, true));   // New entries: show the grid.
    assert(!update(false, false));  // Funding / receiving checks.
    assert(!update(false, false));  // Every entry paid: stay in the grid.
    assert(!update(false, false));  // Admitted (phase3), choosing a stage.
    assert(!update(false, false));  // An entry error does not change the layout.

    assert(update(true, false));   // Actual game start.
    assert(update(true, false));   // Repeated observations during play.
    assert(update(false, false));  // Final result / settling payouts.
    assert(update(false, false));  // A payout error does not reopen setup.
    assert(update(false, false));  // Settled result remains in the strip.

    assert(!update(false, true));  // Only the next entry setup restores the grid.
    assert(!update(false, true));  // Repeated setup observations are stable.
    assert(!update(false, false)); // The finished game does not reopen its strip.
    assert(update(true, true));    // Actual gameplay wins if signals overlap.
    assert(update(false, false));  // Keep the second match's result visible.
    assert(!update(false, true));  // A third entry setup can restore the grid again.

    const std::uint64_t starts_at_result = 12;
    // CSS may appear immediately after End; earlier Start presses are in the baseline.
    assert(!SatsArenaLocal::WantsNextSetup(true, false, starts_at_result, starts_at_result));
    assert(SatsArenaLocal::WantsNextSetup(true, true, 12, starts_at_result)); // Next setup click.
    assert(SatsArenaLocal::WantsNextSetup(true, false, 13, starts_at_result)); // Fresh controller Start.
    assert(SatsArenaLocal::WantsNextSetup(false, false, 12, starts_at_result)); // Startup recovery.

    std::cout << "PASS: local setup/paid/stage selection stay grid; actual start, settlement and results stay strip; errors preserve layout; results wait for explicit next setup or fresh Start\n";
}
