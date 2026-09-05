# Bug: youandme.at sign-in hangs forever for accounts on non-Bluesky PDSes that strictly enforce RFC 9126

**Summary**: Logging into youandme.at with a handle on a third-party PDS (tested: `cocoon`,
running memo.dog) hangs indefinitely on "Connecting…" — no error is ever shown. Reproduced on
both mobile and desktop Chrome, different networks, ruling out a client-device/network cause.

**Root cause**: youandme.at's OAuth client sends the PAR (Pushed Authorization Request) with
`Content-Type: application/json`. RFC 9126 §2 requires `application/x-www-form-urlencoded` for
this request. A PDS that parses PAR bodies strictly per spec (cocoon does) can't read the JSON
body and returns a generic `{"error":"InvalidRequest"}` — which the client never surfaces to the
user, so the UI just hangs.

**Why this doesn't show up against bsky.social accounts**: Bluesky's own reference PDS
implementation appears to be lenient about PAR content-type (accepting JSON in addition to the
spec-required form-encoding), which masks this bug for the common case. It only surfaces against
a strictly spec-compliant PDS.

**Reproduction** (captured via Chrome DevTools Network tab, 2026-09-05):
1. Log into youandme.at with a handle on `memo.dog` (a cocoon-based PDS).
2. Network tab shows a clean sequence succeeding: `com.atproto.identity.resolveHandle` (200) →
   DID doc fetch (304) → `/.well-known/oauth-protected-resource` (200) →
   `/.well-known/oauth-authorization-server` (200) → then a POST to `/oauth/par` that returns
   **400**, request `Content-Type: application/json`, response body
   `{"error":"InvalidRequest"}`.
3. Confirmed independently: sending a genuinely empty POST directly to `https://memo.dog/oauth/par`
   produces the identical `{"error":"InvalidRequest"}` response — i.e., the JSON body is being
   treated as no body at all.
4. Sending a proper `application/x-www-form-urlencoded` PAR request to the same endpoint (with a
   real, resolvable `client_id` metadata document) gets a correctly-parsed, spec-compliant
   response instead.

**Suggested fix**: send the PAR request body as `application/x-www-form-urlencoded` per RFC 9126,
not JSON.

**Impact**: any PDS that correctly enforces RFC 9126 (not just cocoon) will hit this same issue —
likely affects sign-in for a meaningful fraction of self-hosted/non-Bluesky accounts, not just
memo.dog specifically.

**Status**: reported to youandme.at's developer (`pckt.blog`); this blocks memo.dog participants
from using the youandme.at kiosk-connections feature at the IOSP 2026 station until fixed
upstream — see `tracker-vss7` for the full incident writeup.
