# Live v2 CSS roster checks

Tested with the generated Arena codeset on local NTSC-U Melee 1.02 and the
native launcher. Captures show the real game and host sidebar together.

1. Four human slots selected in FFA; all four appear in the roster.
2. P1/P3 assigned Red, P2/P4 Blue; the sidebar follows the game's assignments.
3. P1 changed to CPU and P2 changed Blue → Green. The roster immediately shows
   CPU, Red, Green and Blue correctly. Pressing Start remains on CSS because
   expanded funding is not connected yet.
4. Switch back to FFA; the sidebar drops all team labels and retains CPU status.

The payment wallet stayed simulated, with no entry payments and no payouts.
These captures verify hook-to-sidebar telemetry and blocked admission, not a
four-player paid match. Core funding and team settlement are the next step.

Automated coverage:
- Actual PPC hook executed under QEMU: byte-for-byte v2 packets, all four slots,
  human/CPU/closed slots, all team IDs, live team edits, FFA normalization,
  denied/granted Start, register/CR/CTR/XER preservation, online exclusion.
- Native parser/gate tests: valid/invalid rosters, stale team/mode authorizations,
  old/truncated packets, reserved bytes, CSS-load reset, query expiry.
- Generated codeset validated against the actual target DOL instructions;
  matching host build and native tests passed.
