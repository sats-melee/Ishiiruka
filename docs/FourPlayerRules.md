# Four-player funding and settlement rules

Agreed behavior for local Melee, up to four participants:

- FFA: each participant is a side. Teams: use the actual game's team IDs,
  including uneven teams and three-team matches. Colors identify teams;
  controller port colors do not establish membership.
- Each participating human verifies a receiving address and funds a fresh
  entry. CPUs need no address, contribute nothing, and never receive payouts.
- Attributed opponent damage steals from the victim's unspent contribution.
  Keep five-second recipient batches. Friendly fire, self/environmental damage,
  CPU attacks, and attacks against CPUs move no sats. Empty pots cannot pay more.
- Flush pending damage earnings at match end. Return each winning human's own
  unspent funds. Split all losing sides' remaining funds equally among winning
  humans, including eliminated teammates. Allocate indivisible sats in port order.
- Draw, no contest, or CPU-only winning side: completed damage earnings stand;
  return remaining contributions to their owners. Never invent a winner.
- Roster/team changes revoke Start admission. A funded lobby must settle its
  returns before a new roster receives fresh invoices. Verification may carry
  forward only while fresh and bound to the same address.
- The scrollable sidebar must follow all active slots, show team membership,
  verification and entry state, and block Start until every human is ready.
  Log confirmed payments with player, recipient and sats; name the winning team.

## Integration status

The CSS v2 roster bridge, four-slot payment host, scrollable cards, ordered
attacker/victim accounting and team settlement are connected. The match-end
hook reports actual team standings through private E8; standard individual
placements alone cannot establish a team winner.

The match-start roster must agree with the funded CSS roster. Missing/incomplete
credits or an ambiguous result take the return path. Live launcher checks and
accounting assertions are in [the evidence](evidence/four-player-payments/README.md).
Gameplay payments remain simulated; receiving-address verification is live.
