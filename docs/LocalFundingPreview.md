# Match funding in the sidebar (fake wallet)

Launch with `make app` or `make arena`: Melee boots immediately. The sidebar is
part of the game window. There is no launch-time funding dialog or manual
result-entry flow.

## Match loop

1. Select two human characters on P1 and P2 in local, non-team Versus.
2. When Melee displays **PRESS START**, the sidebar displays each unpaid
   player's invoice and QR code. These are **test invoices**, not payable
   Lightning invoices. Use each player's **Simulate payment** button.
3. Start cannot leave character select until both 500-sat budgets are confirmed
   and reserved. A Start pressed while unpaid is queued. The second payment
   releases that queued Start automatically; otherwise press Start normally.
4. Choose a stage and play. The sidebar shows real available/reserved balances
   from the fake ledger and finalized damage-credit activity.
5. A normal game-end event and placements settle the 100-sat win bonus.
   No-contest, ambiguous results, and emulator stop produce no winner.
   Damage billing remains zero.
6. Stay in Melee, return through results to character select, and fund the next
   match at PRESS START. Confirmed balances are reusable; only a shortfall
   receives a new invoice. For example, after P1 wins the first match, P1 has
   600 sats, P2 has 400, and P2 needs 100 more to reserve the next 500-sat budget.

Characters can be changed while funding. Changing selections revokes pending
game authorization. Returning to character select from stage select invalidates
that admission and resolves the abandoned fake match as no contest before
preparing a new one. Admission is single-use and matched to the current CSS
generation; a delayed worker acknowledgement cannot unlock a different selection.

## Build and protocol

Build with `ENABLE_SATS_ARENA=ON` and the adjacent `sats-arena` workspace
(or set `SATS_ARENA_ENGINE_DIR`). Rebuild/install the Arena codeset first:

```sh
bash Tools/arena/build-codeset.sh /path/to/NTSC-U-1.02.iso
bash Tools/arena/build.sh
bash Tools/arena/test.sh
```

The assembly branch adds a verified hook at `0x80263250`, after the CSS prompt
readiness calculation and before Start handling. It sends a private 12-byte
`0xE7` query containing readiness, Start attempt, slot types, and character
selections; Dolphin responds with one byte. This traffic is never recorded or
forwarded. The CSS-load hook sends a reset so stage-select admission cannot be
reused on a new visit. Online/other scenes are untouched. Unsupported local
rosters fail closed; this host currently funds only two humans on P1/P2.

The emulation thread only exchanges bounded state with the GUI. The Rust worker
checks the wallet, reserves funds, consumes admission, and settles results.
An acknowledged Playing phase for the current selection is required before
the GUI grants the hook. The emulator is not paused while payment is pending.

QR encoding uses Project Nayuki's MIT-licensed C++ QR Code generator under
`Source/Core/DolphinWX/QR/`. The source files retain the license notice.

Fake data lives under `<Dolphin user>/SatsArenaFake/`.
`SATS_ARENA_FAKE_DATA_DIR` selects an isolated test ledger. The host accepts no
NWC credentials and never makes real Lightning payments.
`SATS_ARENA_FAKE_LOCAL=0 make app` explicitly disables the payment gate for
ungated diagnostic play.

## Live verification

See `docs/evidence/css-payment-gate/`:

- `01-unpaid-start-blocked.png`: two QR codes, Start queued, no payments.
- `02-one-payment-blocked.png`: P1 reserved, P2 unpaid, CSS still blocked.
- `03-funded-stage-select.png`: second payment released queued Start.
- `04-funded-match-hit.png`: Dr. Mario hit Luigi; 8% damage and credit event.
- `05-match-settled.png`: normal Game end, P1 winner inferred automatically.
- `06-next-match-payment.png`: same running emulator, next CSS gate, P2 shortfall QR.
- `07-stage-back-requires-new-start.png`: backing out of stage select resolves
  the abandoned admission, keeps both budgets funded, and waits for a new Start.

The QR contents were independently decoded from screenshots and matched the
fake invoices (500 sats each initially; 100 sats shortfall for the next match).
The fake ledger recorded exactly one 100-sat winner transfer and a new funding
lobby. No manual result was submitted.
