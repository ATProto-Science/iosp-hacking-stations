// Real Semble calls via MCP — the protocol itself, not just its outcome.
// Spawns ~/src/semble-mcp (or wherever SEMBLE_MCP_ENTRY points) as a child
// process and talks MCP over stdio, the same way Claude Desktop/Code do.
//
// This is NOT the zero-dependency default — it needs one real npm package
// (see package.json in this folder: `npm install` once). If you just want
// working Semble calls with the least ceremony, use semble-helper.mjs (REST,
// no dependency, no server to run) instead. Reach for *this* file when the
// point is demonstrating the MCP protocol itself, or when you're building a
// standalone script that should keep working even without a live AI agent
// attached.
//
// If you ARE working with an AI coding agent that already has Semble's MCP
// tools configured (Claude Code, Claude Desktop, Cursor, …), you don't need
// this file at all for exploration — just ask your agent in plain English
// ("search Semble for...", "what connects to this URL?"). This file is for
// when that call needs to happen from code that runs on its own.
//
// MCP tools are read-only by default (search_urls, get_url_connections,
// etc.) — matching Semble's own anonymous-mode behavior (no key = read
// only). Write tools (create_connection, add_url_to_library) exist in the
// same server but only REGISTER when the server itself is launched WITH a
// SEMBLE_API_KEY — this file passes your shell's SEMBLE_API_KEY through to
// the spawned server automatically, so exporting it before running your
// script is enough to unlock them.

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const SEMBLE_MCP_ENTRY =
  process.env.SEMBLE_MCP_ENTRY || `${process.env.HOME}/src/semble-mcp/dist/index.js`;

let clientPromise;

function getClient() {
  if (!clientPromise) {
    clientPromise = (async () => {
      const transport = new StdioClientTransport({
        command: "node",
        args: [SEMBLE_MCP_ENTRY],
        env: { ...process.env }, // passes SEMBLE_API_KEY through if set -> unlocks write tools server-side
      });
      const client = new Client({ name: "iosp-workshop-mcp-client", version: "0.1.0" });
      await client.connect(transport);
      return client;
    })();
  }
  return clientPromise;
}

async function callTool(name, args) {
  const client = await getClient();
  const result = await client.callTool({ name, arguments: args });
  if (result.isError) {
    throw new Error(`MCP tool "${name}" failed: ${JSON.stringify(result.content)}`);
  }
  const text = result.content?.[0]?.text;
  return text ? JSON.parse(text) : result;
}

// --- Read (works with no SEMBLE_API_KEY at all — anonymous mode) ---

export async function searchUrls(searchQuery, opts = {}) {
  const data = await callTool("search_urls", { searchQuery, ...opts });
  return data.urls;
}

export async function getUrlConnections(url, opts = {}) {
  return callTool("get_url_connections", { url, ...opts });
}

// --- Write (only registered if the spawned server saw a SEMBLE_API_KEY) ---

export async function createConnectionViaMcp(args) {
  return callTool("create_connection", args);
}

// Call once at the end of a script if you want a clean exit instead of
// waiting on the child process; harmless to skip for a short-lived script.
export async function closeMcpClient() {
  if (clientPromise) {
    const client = await clientPromise;
    await client.close();
  }
}
