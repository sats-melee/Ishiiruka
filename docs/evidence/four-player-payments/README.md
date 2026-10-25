# Live four-player payment checks

Tested in the real launcher on NTSC-U Melee 1.02 with the matching CSS v2,
damage-credit and private E8 result hooks. Controller actions used the local
FIFO controller helper; receiving-address verification and funding used the
actual sidebar controls through GTK accessibility. No emulated state was edited.
The wallet stayed simulated. Addresses were nic@landaverde.io and
moldyshelf891@walletofsatoshi.com, reused across human slots for this test.

| Scenario | Observed result, P1–P4 payouts in sats |
| --- | --- |
| Four-human FFA | Three entries kept Start blocked; fourth released queued Start. P1 stole 232 from P2. P4 won: `[232,0,0,1768]`. |
| Paid team assignment changed | P1's 500 returned before new team roster got fresh unpaid invoices. |
| Red P1/P3 vs Blue P2/P4 | P1 stole 232 from P2. Blue won, with P4 already eliminated: `[232,768,0,1000]`. |
| Three Red humans vs Blue CPU P4 | Only three entries required. 57 credits included damage both to/from CPU, with no money movement. CPU-only Blue victory returned `[500,500,500,0]`. |
| Two-human FFA no contest | P1 had 160 pending sats when the game was aborted through pause/LRAS. Settlement `[660,340,0,0]`; new unpaid invoices followed. |

`ledger-checks.json` was generated from actual archived match state with
`scripts/audit_arena.py` in the controller repository. Assertions check every
contributed sat is remaining, pending, or confirmed exactly once; CPU slots have
no funds; payout IDs/receipts are unique; and invoices stay bound to the actual
receiving player/address. Every scenario settled with zero remaining/pending
funds and the expected exact totals.

Captures include the running game and sidebar, top/bottom scroll positions,
P3/P4 QR codes, live damage receipts, team cards, CPU exemptions, and result logs.
The log ordering was improved after the first team capture so recent funding
and result summaries stay visible at the bottom; capture 07 shows the final log.

Automated validation also passed:

- 75 Rust tests plus workspace Clippy with warnings denied.
- Native tracker/gate tests: 2–4 slots, teams/CPU, stale authorization, complete
  roster validation, finalized ordered credits, result masks and invalid data.
- Actual PPC hooks under QEMU: damage capture, CSS packet/gates, team stock and
  timeout results, eliminated teammates, ties/no contest, malformed data,
  register preservation and online/replay exclusion.
- Controller tests and helper syntax checks; matched codeset/DOL validation and
  native build.

Timeouts, extra team layouts, friendly fire, fractional/capped steals, payment
failures and restart/migration edges have automated coverage. The live scenarios
above distinguish what was exercised in-game from synthetic fixtures.
