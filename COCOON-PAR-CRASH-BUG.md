# Bug: cocoon panics (nil pointer dereference) handling a real client's PAR request

**Summary**: A Pushed Authorization Request (PAR) to `/oauth/par`, using a real, spec-complete
OAuth client metadata document (youandme.at's actual `client-metadata.json`, including an
explicit `token_endpoint_auth_method: "none"`), crashes the request with an empty reply
(`curl` error 52) instead of returning any HTTP response. Reproduced twice, consistently.

**Root cause** (from cocoon's own source, `server/handle_oauth_par.go`): the server-side Go
process panics with `invalid memory address or nil pointer dereference` at line 79:

```go
if parRequest.DpopJkt == nil {
    if client.Metadata.DpopBoundAccessTokens {   // <-- line 79, client.Metadata is nil here
```

Full panic trace (from `docker logs cocoon-pds`):
```
http: panic serving 172.19.0.1:51134: runtime error: invalid memory address or nil pointer dereference
goroutine 86 [running]:
...
github.com/haileyok/cocoon/server.(*Server).handleOauthPar(...)
	/dockerbuild/server/handle_oauth_par.go:79 +0xb58
```

Client authentication itself succeeds cleanly before this point (no `"error authenticating
client"` log line for this request, unlike the case below) — `client` is non-nil (methods on it,
`IsRedirectURIAllowed` at line 64 and the scope check at line 69, both execute without issue) —
but `client.Metadata` is apparently nil by the time line 78/79 dereferences it directly.

**A related, correctly-handled case for contrast**: sending the *same* PAR request shape but with
a client-metadata document missing `token_endpoint_auth_method` (rather than declaring it `none`
explicitly) fails gracefully with a proper error instead of crashing:
```json
{"error":"failed to get client: unsupported client authentication method ``"}
```
HTTP 400. So the crash specifically needs a client that *passes* authentication — it isn't
triggered by just any malformed client metadata.

**Reproduction**:
```bash
curl -X POST https://<cocoon-host>/oauth/par \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode 'client_id=https://youandme.at/client-metadata.json' \
  --data-urlencode 'response_type=code' \
  --data-urlencode 'redirect_uri=https://youandme.at/oauth/callback' \
  --data-urlencode 'scope=atproto repo:at.youandme.connection' \
  --data-urlencode 'code_challenge=abcxyzabcxyzabcxyzabcxyzabcxyzabcxyzabcxyz' \
  --data-urlencode 'code_challenge_method=S256' \
  --data-urlencode 'state=test123'
```
(`https://youandme.at/client-metadata.json` is real, public, and was HTTP 200 at time of
writing — this isn't a bad-URL issue.)

**Impact**: any real OAuth client whose metadata happens to hit this nil-Metadata path will crash
cocoon's PAR handler outright rather than getting a clean error — confirmed live via a real
browser login attempt through youandme.at against a cocoon-hosted test account, which failed
consistent with this crash.

**Not yet isolated**: which specific field(s) in youandme.at's metadata trigger the nil
`client.Metadata` (candidates: the two-entry `redirect_uris` array, or the custom
`repo:at.youandme.connection` scope value) — left to cocoon's maintainer to narrow down with the
stack trace above as a starting point.

**Status**: found while investigating the separate youandme.at OAuth PAR content-type bug (see
`YOUANDME-OAUTH-BUG.md`) — that bug's fix (if it changes youandme.at's PAR `Content-Type` to
`application/x-www-form-urlencoded`) will still hit *this* crash against cocoon specifically,
independent of anything on youandme.at's side.
