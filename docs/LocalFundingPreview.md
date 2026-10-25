# Match funding and payouts in the sidebar

Melee boots immediately. The sidebar follows the 2–4 active character-select
slots, in FFA or teams, and scrolls to show every player's card. Team labels use
Melee's actual Red, Blue and Green assignments. Closed slots are hidden. CPUs
show **No address or entry required** and never fund or receive payments.

Each human enters a Lightning Address and uses its adjacent **Verify** button.
The background worker checks the live receiving endpoint and a signed unpaid
invoice, shows receiving limits, and requires refreshing after ten minutes.
This sends no payment and does not prove ownership or routing. Once every human
is verified, each has a fresh entry invoice/QR. A paid QR remains visible with
**PAID** beside it. Start stays blocked with red text until every human is
verified and funded; a queued Start proceeds after the last entry is confirmed.
Addresses lock after the first entry. Changing a funded roster or team returns
entries before issuing fresh invoices, and revokes earlier Start authorization.

## Payments

Defaults: **500 sats per human entry**, **10 sats per 1% credited damage**.
Configure with `SATS_ARENA_ENTRY_SATS` and `SATS_ARENA_SATS_PER_PERCENT`.

Opponent damage steals from that victim's remaining contribution. Whole sats
collect into five-second recipient batches; later hits do not extend the timer.
Friendly fire, self/environmental damage, CPU attacks and hits against CPUs
move no sats. Each victim can lose only their contributed entry.

At match end, settle unbatched earnings, return each winning human's own
unspent contribution, and split losing sides' remaining funds evenly among all
winning humans, including eliminated teammates. CPUs never take a share. In
FFA the sole winner receives all losing players' remaining funds. Leftover
whole sats go in player-port order.

On no contest, interruption, incomplete match data, or a CPU-only winning side,
keep accrued damage earnings and return unspent contributions to their owners.
The log names the winning team and shows confirmed amount, player, destination
and UTC time. Simulated receipts explicitly say **SIMULATED**. Winnings never
replace the next match's entry.

The full rules are in [FourPlayerRules.md](FourPlayerRules.md) and the shared
[payment rules](../../sats-arena/docs/match-payments.md).

## Game integration

The CSS hook at `0x80263250` sends v2 roster queries with all four slot kinds,
characters, team mode and team IDs. Selection changes, a new CSS visit, malformed
packets and expired queries revoke admission. The actual match-start roster must
agree with the funded lobby before any damage or result can pay out.

Finalized credits carry attacker/victim and an ordered sequence. The worker
consumes them in game order; duplicate retries are harmless, and a sequence gap
forces the return path. The bounded 4,096-record history allows delayed UI polls
without guessing attribution. Credits are acknowledged before submitting the
final result. Missing/overflowed capture cannot authorize a winner payout.

The existing match-end hook sends a private **E8** result with Melee's actual
team standings and full winning-side mask, including eliminated teammates.
Individual replay placements alone cannot establish the team winner. Both E6
credits and E8 results are excluded from replay files, spectators and reporting.
These hooks support local **NTSC-U Melee 1.02**; they do not enable online payouts.
No wallet work runs on the emulation thread.

The Rust `arena_pot::MatchPot` state machine owns funding, batching and settlement.
The worker persists entries and payout intent before issuing wallet operations.
Recovered ambiguous outgoing payments are looked up, never blindly resent.
Pending or failed returns block funding a replacement roster. Local state schema
2 migrates to four slots, preserving old funds, IDs and fractional damage carry.

The GUI wallet remains **simulated**; its QR invoices are not payable Lightning
invoices. Address verification is live, and the receiving invoice resolver and
wallet boundary remain independent of NWC. A real funding/payout wallet adapter
still needs connecting. Fees are outside the match pots.

State: `<Dolphin user>/SatsArenaFake/prepaid-match.json`; completed/changed games
archive under `SatsArenaFake/matches/`. `SATS_ARENA_FAKE_DATA_DIR` selects a test
folder. `SATS_ARENA_FAKE_LOCAL=0` disables funding for diagnostic play.

## Build and validation

```sh
bash Tools/arena/build-codeset.sh /path/to/NTSC-U-1.02.iso
bash Tools/arena/build.sh
bash Tools/arena/test.sh
# Sibling Rust workspace:
cargo test --workspace --all-targets
cargo clippy --workspace --all-targets -- -D warnings
```

The host and codeset must match: CSS protocol v2 and private E8 results.
PPC tests execute the actual hooks, including team stock/timeout results and
online exclusion. Native tests cover rosters, result validation and ordered
credits. Rust tests cover conservation, CPU/team exclusions, payout retries,
verification gates, roster refunds and migration. Live launcher evidence and
ledger assertions are in [four-player-payments](evidence/four-player-payments/README.md).
Earlier evidence folders describe earlier versions of this flow.
