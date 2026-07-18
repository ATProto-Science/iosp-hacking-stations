// Real Bluesky post stream via Jetstream — zero-dependency (Node 20+'s
// built-in WebSocket), matching the exact query-param pattern already
// proven in station-2-live-data/consumer_viewer.py (nebra.jetstream's
// get_jetstream_query_url: wantedCollections + wantedDids, repeated params).
//
// Two ways to scope this, matching the two-tier pattern used everywhere else
// in station-4-bots:
//   (a) bounded/reliable (recommended default): pass `dids`, a small known
//       set of accounts (e.g. memo.dog test accounts) — guaranteed to have
//       real traffic to react to during a live demo, no dependency on
//       organic engagement. Confirmed working: memo.dog posts DO reach
//       Jetstream, but only after an explicit crawl request
//       (POST https://bsky.network/xrpc/com.atproto.sync.requestCrawl,
//       body {"hostname": "https://memo.dog"}) — cocoon's own
//       COCOON_RELAYS=https://bsky.network config didn't self-register
//       reliably. Budget ~1-2 minutes of relay-crawl lag after a fresh post
//       before it's guaranteed to show up.
//   (b) open/harder: omit `dids` entirely -- watches the full public
//       app.bsky.feed.post firehose (real volume, hundreds of events/sec).
//       Jetstream has no topic/keyword filter server-side, so "watch a
//       research topic" means filtering `record.text` yourself after
//       receiving -- pass a `filter` function to do that.

const JETSTREAM_BASE = "wss://jetstream2.us-east.bsky.network/subscribe";

function buildUrl({ collections = ["app.bsky.feed.post"], dids = [] } = {}) {
  const params = new URLSearchParams();
  for (const c of collections) params.append("wantedCollections", c);
  for (const d of dids) params.append("wantedDids", d);
  const qs = params.toString();
  return qs ? `${JETSTREAM_BASE}?${qs}` : JETSTREAM_BASE;
}

// Async generator yielding real posts: { text, did, rkey, uri, url }.
// `filter(record)` is checked before yielding -- return false to skip
// (only useful in the open/harder tier; the bounded tier rarely needs one).
export async function* watchPosts({ dids = [], collections = ["app.bsky.feed.post"], filter } = {}) {
  const url = buildUrl({ collections, dids });
  const ws = new WebSocket(url);

  const queue = [];
  let resolveNext;
  let closed = false;
  let error;

  ws.addEventListener("message", (event) => {
    let msg;
    try {
      msg = JSON.parse(event.data);
    } catch {
      return;
    }
    if (msg.kind !== "commit" || msg.commit?.operation !== "create") return;
    if (msg.commit.collection !== "app.bsky.feed.post") return;

    const record = msg.commit.record;
    if (filter && !filter(record)) return;

    const did = msg.did;
    const rkey = msg.commit.rkey;
    const post = {
      text: record.text,
      did,
      rkey,
      uri: `at://${did}/app.bsky.feed.post/${rkey}`,
      url: `https://bsky.app/profile/${did}/post/${rkey}`,
    };

    if (resolveNext) {
      const r = resolveNext;
      resolveNext = null;
      r(post);
    } else {
      queue.push(post);
    }
  });

  ws.addEventListener("error", (e) => {
    error = e;
  });
  ws.addEventListener("close", () => {
    closed = true;
    if (resolveNext) {
      const r = resolveNext;
      resolveNext = null;
      r(null);
    }
  });

  try {
    while (!closed) {
      if (queue.length) {
        yield queue.shift();
        continue;
      }
      if (error) throw error;
      const post = await new Promise((resolve) => {
        resolveNext = resolve;
      });
      if (post === null) break;
      yield post;
    }
  } finally {
    ws.close();
  }
}
