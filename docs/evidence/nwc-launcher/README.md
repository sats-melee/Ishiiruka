# Launcher-first NWC integration checks

No real connection string or Lightning payment was used for these checks.

- Rust workspace: 92 tests passed, including 4 encrypted local-relay NWC tests,
  configuration precedence/redaction, and 8 retired-invoice lifecycle cases.
  Clippy passed with warnings denied.
- Launcher: saved-secret selection, private stdin framing, explicit development
  overrides, ordinary-child environment isolation, local Play routing, binary
  discovery, and missing-credential errors passed focused tests. Typecheck,
  lint, and production main/renderer builds passed.
- Actual native integration: the launcher's real `SatsArenaNativeGame.start()`
  launched the built Dolphin through the Linux development wrapper using a real
  Melee ISO and isolated profile. The native worker consumed the private fake
  payload, created unpaid simulated state, and did not access the secret store.
  Cleanup targeted only that exact test profile. The opt-in test passed from
  the host Electron runtime, matching normal launcher placement.
- Native Dolphin build and tracker/gate tests passed. Fake/live state roots are
  separate, and an exclusive file lock prevents two processes owning one ledger.

NWC relay tests use actual NIP44/NIP04 encrypted request/response traffic with
signed invoices. They cover startup without payments, invoice creation and
confirmation, outgoing proof validation, lost-response recovery via lookup with
one submission, capability/network rejection and unsupported finite fee caps.

Late-entry tests cover destination binding, an old QR paid after an address or
roster change, expiry/replacement, an unobserved already-paid invoice, an
incorrect amount, unavailable old lookups, and restart recovery without resend.

A mainnet match still requires the user's saved NWC connection, supported wallet
permissions and wallet funds. Configure it through Wallet settings, then use
**Play local Arena**. The separate server-preview Host button is not this path.
