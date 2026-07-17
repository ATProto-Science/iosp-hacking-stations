// Real Semble calls for both skeletons' STRETCH functions — plain `fetch()`
// against Semble's REST API (https://api.semble.so/xrpc), no SDK dependency,
// so this file stays as zero-`npm install` as the rest of station-4-bots.
//
// Why REST here and not MCP: Semble's MCP server (~/src/semble-mcp, or
// whatever your coding agent has configured) is the right tool when a *person
// with an AI agent* wants to browse Semble conversationally — just ask your
// agent to search or look up connections, no code needed. But this file is
// for a *standalone script* (no agent watching), and specifically for
// WRITES: this is real, straight from the SDK's generated types
// (@semble.so/api v0.0.5) — `create_connection`/`add_url_to_library` DO
// exist as MCP tools too, but only register when the MCP server itself was
// launched with a SEMBLE_API_KEY; without one it runs "anonymous" (reads
// only). REST sidesteps that: any function below works read *or* write with
// nothing but an API key, no MCP server to stand up at all.
//
// Get a key from your Semble account settings, then either export it:
//   export SEMBLE_API_KEY=sk_...
// or pass { apiKey } explicitly to any function below.

const BASE_URL = "https://api.semble.so/xrpc";

// Semble's real connectionType enum (network.cosmik.connection.create) — NOT
// the same casing/values as either skeleton's own bandit ARMS. If you want
// arm names that map 1:1 to a real write, use these; if you'd rather keep
// friendlier lowercase arm names, translate at the call site instead of
// growing the bandit's own ARMS list to match Semble's vocabulary.
export const CONNECTION_TYPES = [
  "SUPPORTS",
  "OPPOSES",
  "ADDRESSES",
  "HELPFUL",
  "LEADS_TO",
  "RELATED",
  "SUPPLEMENT",
  "EXPLAINER",
];

function apiKeyOf(opts) {
  const key = opts?.apiKey ?? process.env.SEMBLE_API_KEY;
  if (!key) {
    throw new Error(
      "No Semble API key. Set SEMBLE_API_KEY or pass { apiKey } explicitly."
    );
  }
  return key;
}

async function call(method, path, { apiKey, query, body } = {}) {
  const url = new URL(BASE_URL + path);
  if (query) {
    for (const [k, v] of Object.entries(query)) {
      if (v === undefined) continue;
      if (Array.isArray(v)) v.forEach((item) => url.searchParams.append(k, item));
      else url.searchParams.set(k, v);
    }
  }
  const res = await fetch(url, {
    method,
    headers: {
      Authorization: `Bearer ${apiKey}`,
      ...(body ? { "Content-Type": "application/json" } : {}),
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`Semble ${method} ${path} -> ${res.status}: ${text}`);
  }
  return res.json();
}

// --- Read ------------------------------------------------------------------

// Full-text search across saved URLs' titles/descriptions/URLs. Good for
// bot-skeleton.mjs's queryTool() and connections-skeleton.mjs's
// findCandidatePair(): given a post/paper's text, find something already in
// Semble that might be what it's evidence for, or a connection candidate.
export async function searchCards(searchQuery, opts = {}) {
  const apiKey = apiKeyOf(opts);
  const data = await call("GET", "/network.cosmik.card.search", {
    apiKey,
    query: { searchQuery, limit: opts.limit ?? 10 },
  });
  return data.urls; // [{ url, metadata: { title, description, ... }, urlLibraryCount, ... }]
}

// Existing typed connections touching a URL (either direction). Useful once
// you've found a candidate via searchCards() and want to check it isn't
// already connected before proposing a new one.
export async function getConnectionsForUrl(url, opts = {}) {
  const apiKey = apiKeyOf(opts);
  const data = await call("GET", "/network.cosmik.connection.getForUrl", {
    apiKey,
    query: {
      url,
      direction: opts.direction, // "forward" | "backward" | "both"
      connectionTypes: opts.connectionTypes,
    },
  });
  return data.connections;
}

// --- Write -------------------------------------------------------------

// Save a URL to the authenticated account's library — the prerequisite for
// createConnection() below (Semble connects saved cards, not bare URLs, in
// practice a card gets created on first save either way).
export async function saveUrl(url, opts = {}) {
  const apiKey = apiKeyOf(opts);
  return call("POST", "/network.cosmik.card.addUrl", {
    apiKey,
    body: { url, note: opts.note },
  });
  // -> { urlCardId, noteCardId? }
}

// A typed, directional link between two URLs or cards. This is the one
// connections-skeleton.mjs's old STRETCH comment said didn't exist over MCP
// — it does, both over REST (here) and over MCP with a keyed server; REST is
// just the simpler path for a script with no agent attached.
export async function createConnection(
  { sourceType, sourceValue, targetType, targetValue, connectionType, note },
  opts = {}
) {
  const apiKey = apiKeyOf(opts);
  if (connectionType && !CONNECTION_TYPES.includes(connectionType)) {
    throw new Error(
      `Unknown connectionType "${connectionType}", expected one of ${CONNECTION_TYPES.join(", ")}`
    );
  }
  return call("POST", "/network.cosmik.connection.create", {
    apiKey,
    body: { sourceType, sourceValue, targetType, targetValue, connectionType, note },
  });
  // -> { connectionId }
}
