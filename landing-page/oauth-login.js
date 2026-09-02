// SCAFFOLD — written 2026-09-02, NOT wired into player.html yet, NOT
// verified against real hardware/browser. Exists so the "sign in with your
// handle" idea has a concrete starting point next time someone picks it up.
//
// What this is: optional, browser-side ATProto OAuth for player.html, so a
// participant can publish their synth notes under their *own* repo instead
// of the shared relay service account (`ATPROTO_HANDLE`/`ATPROTO_PASSWORD`
// in relay/synth_relay.py). Modeled on the pattern in eurosky-portal
// (~/src/protopack neighbour project, github.com/eurosky-social/eurosky-portal)
// but simplified: that's a full AdonisJS account portal doing server-side
// OAuth with a confidential client and DB-backed session/state stores. This
// only needs a public (no-secret), browser-side client, since the goal is
// just "let a player optionally write one record type to their own PDS" —
// no server, no persisted sessions beyond what IndexedDB already gives us.
//
// Why this doesn't touch synth_relay.py: the relay's Jetstream downlink
// (jetstream_downlink_loop, synth_relay.py:173) already subscribes with
// dids=[] — collection-filtered, not author-filtered — so a note written
// directly to a player's own PDS shows up in the shared live feed and
// re-broadcasts to receive-only instruments exactly like a relay-authored
// note does today. No relay change needed for playback to work.
//
// What "wiring it in" later actually requires:
//   1. Deploy client-metadata.json to the real hacking.tilde.style origin —
//      client_id is a URL and must be live and match exactly, including the
//      redirect_uri. Doesn't work from a local file:// or dev preview URL.
//   2. Load this as a real <script type="module"> in player.html and add a
//      "sign in with your handle" input + button that calls loginWithHandle().
//   3. On page load, call initOAuth() and branch UI on whether a session
//      restored (i.e. the player is already signed in from a previous visit).
//   4. In player.html's playNote(), when a session exists, call
//      publishOwnNote() in addition to (not instead of) the existing
//      POST to the relay's /note — the relay POST still drives the local
//      Web Audio tone and the hardware downlink signal; this just adds the
//      durable, sovereign ATProto record.
//   5. Actually test the OAuth redirect round-trip against a real PDS —
//      nothing below has been run yet.
//
// Unverified assumption: importing @atproto/oauth-client-browser straight
// from esm.sh works with no bundler in a plain <script type="module">. This
// is the standard pattern for browser-only atproto apps but hasn't been
// tried in this repo.

import { BrowserOAuthClient } from "https://esm.sh/@atproto/oauth-client-browser@0.3";
import { Agent } from "https://esm.sh/@atproto/api@0.13";

const NOTE_COLLECTION = "music.atproto.noizetoyz.synth.note";

let clientPromise = null;

function getClient() {
  if (!clientPromise) {
    clientPromise = BrowserOAuthClient.load({
      clientId: "https://hacking.tilde.style/client-metadata.json",
      handleResolver: "https://bsky.social",
    });
  }
  return clientPromise;
}

// Call once on page load. Returns the restored session if the player was
// already signed in from a previous visit, or null if not.
export async function initOAuth() {
  const client = await getClient();
  const result = await client.init();
  return result?.session ?? null;
}

// Redirects the browser to the player's own PDS/authorization server to
// sign in. Never returns — the page reloads at redirect_uri afterwards,
// at which point initOAuth() picks the session back up.
export async function loginWithHandle(handle) {
  const client = await getClient();
  await client.signIn(handle, { scope: "atproto transition:generic" });
}

// Writes one note directly to the signed-in player's own repo. `fields`
// matches the shape player.html already builds for the relay's /note POST
// (note, velocity, deviceId, synthType, fxType?, fxAmount?) — same lexicon,
// same field names, just a different author and transport.
export async function publishOwnNote(session, fields) {
  const agent = new Agent(session);
  return agent.com.atproto.repo.createRecord({
    repo: session.did,
    collection: NOTE_COLLECTION,
    record: {
      $type: NOTE_COLLECTION,
      createdAt: new Date().toISOString(),
      ...fields,
    },
  });
}
