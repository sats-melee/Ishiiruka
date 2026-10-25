# Automatic receivers and chronological activity

Live launcher checks used four humans and the two supplied Lightning addresses.
Typing an invalid P1 address automatically produced a red player-specific error
and blocked Start. Editing it to a valid address retried automatically. All four
receivers passed without pressing Verify or Retry. Each fresh next-match invoice
also triggered a new automatic check, confirmed by newer checked-at timestamps.

At the new default of 1 sat per 1%, P1 dealt 23.28% and received 23 sats; P2
dealt 6% and received 6 sats. The actual receipts named the correct receiving
players and addresses. After no contest, total payouts were [517,483,500,500],
conserving all 2,000 contributed sats. The timeline keeps invoice confirmations,
damage receipts, the result and settlement receipts in recorded timestamp order.

`live-checks.json` contains the actual persisted activity and ledger assertions.
The first stage-selection attempt was cancelled back to CSS; its chronologically
recorded returns remain in the same history. The successful game was then
selected after visually confirming stage select. All payments were simulated.

Workspace Rust tests (77), Clippy, native tests and the build passed. Unit
regressions cover timeline order across matches/restarts and safe migration of
an unpaid old rate while preserving already-funded rate agreements.
