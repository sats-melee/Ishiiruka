# Embedded local funding preview (fake only)

Build with `ENABLE_SATS_ARENA=ON` and an adjacent `sats-arena` checkout (or
set `SATS_ARENA_ENGINE_DIR` in CMake). The `sats-arena-local` Rust shared library
is linked into Dolphin; there is no HTTP listener or separate payment server.

`make app` (and `make arena`) enables the fake funding gate by default. When a
saved ISO is available, the funding dialog opens before that ISO boots. Fund
both players and select **Start funded session** to launch emulation. Closing
the dialog leaves emulation stopped; press Play to reopen the gate.

To prepare funding separately, stop emulation, then select **Local funding demo (FAKE)** in the sidebar.
Each player receives a fake invoice. **Simulate Player 1/2 payment** confirms
that invoice through the fake wallet and reserves their 500-sat game budget.
Start stays disabled until both budgets are reserved. Existing confirmed
balances can be reused instead of paying again. Cancellation returns reserved
funds to player balances, not a Lightning refund.

After closing the funding window, press Play: **Start funded session** consumes
the ready lobby's single-use start transition before Dolphin boots. Direct
binary launches can enable the same gate with `SATS_ARENA_FAKE_LOCAL=1`.
`start.sh` enables it by default, including across Flatpak/Distrobox boundaries.
The dialog is centered on the main application window.

This is a **pre-launch session gate, not a stage-select gate for every match**.
After stopping emulation, use the sidebar's explicitly manual fake result
buttons to settle or record a no-contest, then create a new lobby. These inputs
are not proof of the game result. Live tracker damage is NOT billed by this
preview (damage rate is zero, win bonus 100 sats). For ungated preview play, run
`SATS_ARENA_FAKE_LOCAL=0 make app`. Selecting the sidebar demo still enables the
gate for subsequent launches in that process.

All money operations run on a Rust background thread and use the shared core
engine/SQLite ledger. Fake data lives under `<Dolphin user>/SatsArenaFake/`.
`SATS_ARENA_FAKE_DATA_DIR` overrides that directory for isolated tests. Pending,
ready, and started lobbies survive restarts; started sessions must be explicitly
finished and cannot be started twice. There is no NWC key or real-wallet option
in this host, and its database must never be reused with a real wallet.
