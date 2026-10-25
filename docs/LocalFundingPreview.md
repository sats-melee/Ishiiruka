# Prepaid match payments in the sidebar (fake wallet)

Melee boots immediately with `make app`. At character select **PRESS START**,
the sidebar shows **two new entry invoices and QR codes**, one per player.
There is no launch-time funding dialog, reusable balance or Reserved label.

Enter and save both players’ Lightning Addresses before funding. These addresses
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
and outgoing payments go to explicitly simulated player destinations. Real
micropayments still require a real wallet adapter and receiving destination
for each player. Routing-fee handling is not part of the simulated pot.

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

The assembly codeset is unchanged by this payment-model update. A fresh install
still needs `Tools/arena/build-codeset.sh /path/to/NTSC-U-1.02.iso`.

Live verification is recorded under `docs/evidence/prepaid-match/`.
Older `css-payment-gate/` evidence documents the superseded reservation preview.

The shared scheduler and wallet/provider contracts are documented in
[`batched-payouts.md`](../../sats-arena/docs/batched-payouts.md).
Live batching evidence is in `docs/evidence/batching/`; earlier
`prepaid-match/` evidence predates the five-second timer.
