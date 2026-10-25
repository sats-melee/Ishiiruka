# Prepaid match payments in the sidebar (fake wallet)

Melee boots immediately with `make app`. At character select **PRESS START**,
the sidebar shows **two new entry invoices and QR codes**, one per player.
There is no launch-time funding dialog, reusable balance or Reserved label.

Enter each player’s Lightning Address and click its adjacent **Verify** button
before funding. The panel checks the live receiver and a signed unpaid test
invoice, shows its limits, and requires rechecking after ten minutes. This
sends no payment and does not prove address ownership or successful routing. These addresses
are locked when the first entry payment is confirmed and retained for the next
match. Both players pay the full entry amount every match. A paid QR stays visible
with **PAID** beside it. Start is blocked until both current-match entries are
confirmed; a queued Start proceeds automatically after the second payment.

## Where the sats go

Defaults are **500 sats per entry** and **10 sats per 1% credited damage**.
They can be configured with `SATS_ARENA_ENTRY_SATS` and
`SATS_ARENA_SATS_PER_PERCENT`.

- During play, finalized attributed hits accrue earnings from the victim's
  remaining match funds. The first whole sat starts a five-second batch window;
  subsequent hits join without extending it. Each due batch is submitted once. A victim cannot spend more than their entry amount.
- **Paid out** shows confirmed outgoing simulated payments. **Pending** shows
  earnings still in the batch or owed/in flight; the countdown shows time to
  batch close. The payment log records amount, receiving address and UTC time. Earnings do not refill either match contribution.
- A normal winner receives **all remaining funds from both players**. There is
  no fixed win bonus.
- On no contest, cancellation, interrupted play or incomplete credit capture,
  already-earned payouts stay paid and each unspent contribution is returned.
- Returning to the next PRESS START creates two new full-price invoices.
  Winnings from the previous game never substitute for either entry payment.

Example: both players pay 500. P1 deals 8%, receiving 80 during play.
The remaining pot is 920. If P2 wins, P2 receives 920; total payouts are
P1=80 and P2=920. The next game still requires 500 from each player.

The source of truth for these rules is
[`sats-arena/docs/match-payments.md`](../../sats-arena/docs/match-payments.md).

## Implementation

The existing NTSC-U 1.02 CSS hook at `0x80263250` remains the payment gate.
Changing selections or returning from stage select revokes stale admission.
No wallet work runs on the emulation thread.

The tracker publishes cumulative finalized credited-damage counters independent
of its bounded visible history. The GUI sends those counters to the Rust worker.
Cumulative processing prevents duplicate or missed UI polls from paying twice.
Latest credits are acknowledged before a game-end payout is requested.

The shared `sats-arena-core::pot::MatchPot` state machine owns the contribution,
payout and winner-pot rules. The local worker persists outgoing intent before
calling its wallet; recovered ambiguous payments are looked up rather than
blindly resent. Completed games are archived before creating the next game.
A pending/failed payout prevents silently starting a new pot.

The host is **fake only**: QR invoices are not payable Lightning invoices,
and outgoing payments go to explicitly simulated player destinations. Real micropayments still require a real wallet adapter. Receiving destinations
now receive live network verification even while gameplay uses the fake wallet. Routing-fee handling is not part of the simulated pot.

State is separate from the obsolete balance preview, at
`<Dolphin user>/SatsArenaFake/prepaid-match.json`; settled games are in
`SatsArenaFake/matches/`. Old balances are not imported or reused.
`SATS_ARENA_FAKE_DATA_DIR` selects a test directory.
`SATS_ARENA_FAKE_LOCAL=0 make app` disables funding for diagnostic play.

Build and checks:

```sh
bash Tools/arena/build.sh
bash Tools/arena/test.sh
# In the sibling Rust workspace:
cargo test --workspace --all-targets
cargo clippy --workspace --all-targets -- -D warnings
```

The roster bridge now requires the v2 assembly codeset and matching host. A fresh install
needs `Tools/arena/build-codeset.sh /path/to/NTSC-U-1.02.iso`.

Live verification is recorded under `docs/evidence/prepaid-match/`.
Older `css-payment-gate/` evidence documents the superseded reservation preview.

The shared scheduler and wallet/provider contracts are documented in
[`batched-payouts.md`](../../sats-arena/docs/batched-payouts.md).
Live batching evidence is in `docs/evidence/batching/`; earlier
`prepaid-match/` evidence predates the five-second timer.

Live receiver checks and GUI evidence for the two user-provided addresses are in
`docs/evidence/address-verification/`. The protocol details are in
[`address-verification.md`](../../sats-arena/docs/address-verification.md).


### Four-slot team roster bridge (first integration step)

The CSS funding hook now sends protocol v2: all four slot kinds and characters,
team mode, and the per-slot Red/Blue/Green team IDs. The sidebar displays the
live roster, including CPUs, before admission. Closed slots and FFA team IDs
are normalized to 255. Changing teams, mode, characters, or participants
invalidates previous Start authorization. Old, malformed, and truncated packets
fail closed. Stopping CSS queries expires the roster and authorization.

This step supplies roster telemetry; the payment host still admits only two
humans in P1/P2 FFA. Expanded rosters stay visibly blocked until their funding,
damage attribution, and settlement changes are connected. Do not interpret a
four-player roster display as four-player payment support.

The intended remaining rules are in [Four-player rules](FourPlayerRules.md).
Hook tests execute the actual PPC injection under QEMU, and host tests cover
team/CPU/FFA parsing and admission invalidation. Live captures are under
`docs/evidence/team-roster-hook/`.
