# Sats Arena native UI preview

This fork puts the arena panel **inside the emulator's main window**, not in an
Electron overlay or a separately docked application. The game renders into its
own native child surface; a wxWidgets sibling paints the sidebar. Resizing and
fullscreen operate on their shared parent. The preview never reads emulated
memory, touches rollback state, or performs network or wallet operations.

All pots are explicitly placeholders. No damage accounting, Lightning, escrow,
or funds are connected. Payment labels are non-interactive drawings.

## SteamOS development

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

Source changes are licensed under GPLv2+, like the surrounding emulator code.
