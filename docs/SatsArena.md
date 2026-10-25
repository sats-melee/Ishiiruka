# Sats Arena native UI preview

This fork puts the arena panel **inside the emulator's main window**, not in an
Electron overlay or a separately docked application. The game renders into its
own native child surface; a wxWidgets sibling paints the sidebar. Resizing and
fullscreen operate on their shared parent. A read-only observer consumes the
existing Slippi replay protocol; it never reads emulated memory, changes rollback
state, or performs network or wallet operations.

The standard Arena build now embeds the fake payment engine in this sidebar.
Melee boots immediately, and the assembly hook gates the local CSS PRESS START
transition on both budgets. Invoice QR codes, available/reserved balances and
automatic match settlement are live against the fake ledger. Real Lightning
payments are not connected. See [the match funding flow](LocalFundingPreview.md).

A build without the adjacent local-engine crate retains the damage-only
diagnostic sidebar with placeholder balances.

## Live local damage log

Start a local, non-team VS game with exactly two human/CPU participants. The
sidebar shows only **finalized game-credit calls**, including frame, attacker,
victim, follower flags, attack ID and nominal credited amount. Percent changes
and stale last-hit hints are retained internally for diagnostics but are no
longer displayed. Online, demos, and teams are deliberately excluded.
Nothing changes the pots. Nominal game credit is not actual applied percent or
financial authorization.

The observer replaces unfinalized rollback frames and publishes each finalized
frame only once. Separate bounded histories keep the latest 200 observations and
100 game credits, with at most 300 unfinalized frames
are retained. A 100 ms GUI timer reads a mutex-protected snapshot; the emulation
thread never calls wxWidgets. New matches clear the log; game end/stop preserves
the completed log but discards speculative frames. No log files are written.

The log always shows the latest **100 game-credit messages**, oldest to newest,
and automatically scrolls to the bottom when a message arrives or the window is
resized. No follow/pause control is needed. Match totals are not reduced when old
messages are evicted. `MAX_CREDIT_MESSAGES` in `SatsArenaTracker.h` sets the limit.

Run the standalone tracker tests (no emulator or ROM needed):

```sh
bash Tools/arena/test.sh
```

Tests cover float/endian decoding, negative frame numbers, finalization,
rollback replacement, duplicates, stock resets, missing frames, followers,
unsupported modes, invalid input, bounded storage, and concurrent snapshots.

## Experimental game-credited damage

The companion fork `https://github.com/landaverdend/slippi-ssbm-asm` adds an
opt-in NTSC-U 1.02 hook at `0x8003EB30`. It captures explicit attacker/victim,
follower flags, attack ID, and nominal damage into a bounded per-frame buffer.
The native log shows **CREDIT** records only. Internally observed percent changes
can differ from these amounts; neither authorizes payment.

The private version-1 `0xE6` packet is exactly 392 bytes (32 records maximum).
EXI consumes it explicitly without forwarding it to replay files, spectators,
or Rust reporting. Bad packets mark capture incomplete; overflowed batches are
discarded. Empty batches replace predictions; credit is published only at the
ordinary frame bookend's finalized watermark. Collection is local VS only.
Do not use this experimental codeset for public online matches or settlement.

To regenerate and validate against your own supported ISO:

```sh
distrobox enter -n slippi-arena-build -- bash ../slippi-ssbm-asm/setup-arena-tools.sh
bash Tools/arena/build-codeset.sh "/path/to/Melee.iso"
bash Tools/arena/build.sh
```

`Tools/arena/codeset-manifest.json` records assembler/source hashes and codeset
provenance. The ISO is inspected locally, never copied or committed. See the
ASM fork's `Arena/README.md` for the protocol and PPC execution test.

## SteamOS build environment

The `slippi-arena-build` Distrobox uses Ubuntu 22.04, matching upstream's Linux
CI. Build dependencies and Rust run inside the container, without installing
packages in the SteamOS root filesystem. Distrobox shares your home and devices;
it is a development environment, **not a security sandbox**.

The initial container setup uses the Linux packages listed in
`.github/workflows/pr-build.yml`, plus `build-essential`, `ninja-build`, `curl`,
`ca-certificates`, and `libgl1-mesa-dri` for local GUI execution.

Initialize only the submodules Linux needs:

```sh
bash Tools/arena/setup.sh
git submodule update --init --recursive Externals/SlippiRustExtensions Externals/corrosion
bash Tools/arena/build.sh
bash Tools/arena/install-shortcut.sh
bash Tools/arena/start.sh
```

Run setup only once. The shortcut installs a **Sats Arena** application-menu
entry, so normal use doesn't need a terminal or any window positioning.

The start script also works from VS Code's Flatpak terminal: it uses
`flatpak-spawn --host` to reach the host's Distrobox installation. If host access
is denied, use Konsole instead. It does not install Distrobox inside Flatpak.

### Audio in the container

In Dolphin's audio settings, select **Pulse** as the backend. SteamOS's PipeWire
server provides the PulseAudio-compatible socket. The launcher routes the Rust
music player's ALSA default device through that same socket using a process-local
`ALSA_CONFIG_PATH`; it does not change host audio settings. The container needs
`libasound2-plugins` (included by setup). Existing containers can add it with:

```sh
distrobox enter -n slippi-arena-build -- sudo apt-get install -y libasound2-plugins
```

The launcher respects an explicit `PULSE_SERVER` and otherwise uses the current
user's host socket when available. Volume and output-device choices remain yours.

The build pins the upstream Rust version (1.88.0); its compiler/cache live under
the ignored `build-tools/` directory. Build output is under `build-arena/`.
Concurrency is intentionally limited for this machine's RAM.

The start script imports stock Slippi's `Config` and `GameSettings` folders once into
`~/.config/SatsArenaDolphin`, preserving both configured controllers. It passes
that separate user folder to Dolphin and boots its last ISO when the file exists.
Later changes to stock Slippi settings are not synchronized. Pass `--help` to
the start script for upstream command-line options. There is no ISO bundled here.

The native build forces render-to-main and disables render auto-size on boot so
the sidebar and game cannot split into separate windows. Window size/fullscreen
preferences otherwise remain Dolphin settings in the isolated user profile.

To build without this fork's sidebar, configure CMake with
`-DENABLE_SATS_ARENA=OFF`.

## Manual verification

- Launch: a single application window contains both the game and sidebar.
- Resize, maximize, Alt+Enter: the sidebar stays inside that same window.
- Verify Player 1 (GameCube adapter) and Player 2 (8BitDo) still work.
- Stop and restart a game: sidebar survives and the game list returns normally.
- Close and relaunch: no manual positioning or controller reconfiguration.
- Confirm labels say preview/no real money, including at short window heights.
- Begin a two-player VS game, hit each player, and compare log percent with HUD.
- Trade enough hits to overflow the visible log: new messages stay visible at the bottom.
- After more than 100 credits, only the latest 100 remain; the match total continues increasing.
- End/start another match: old events clear and new damage begins at a fresh baseline.

Source changes are licensed under GPLv2+, like the surrounding emulator code.
