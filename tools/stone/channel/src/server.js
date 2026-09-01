// SPDX-FileCopyrightText: 2026 John Farina
// SPDX-License-Identifier: Apache-2.0

// Node entry point, for running the channel server on Railway.
//
// Everything this does is translate between Node's http module and the Worker's
// fetch handler. The handler itself is unchanged and untouched by which host it
// is running on — see src/index.js.

import { realpathSync } from "node:fs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";

import worker from "./index.js";
import { FileKV } from "./kv_fs.js";

// The Worker refuses a bundle over 25 MB; this is the point at which we stop
// reading one, so a bad or hostile upload cannot exhaust memory before the
// handler ever sees it.
const MAX_BODY = 32 * 1024 * 1024;

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on("data", (chunk) => {
      size += chunk.length;
      if (size > MAX_BODY) {
        reject(Object.assign(new Error("body too large"), { status: 413 }));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

// Node hands back a plain object whose values may be arrays, and a few of the
// hop-by-hop names it includes are rejected by the Headers constructor.
const SKIP = new Set(["connection", "keep-alive", "transfer-encoding", "upgrade"]);

function toHeaders(raw) {
  const headers = new Headers();
  for (const [name, value] of Object.entries(raw)) {
    if (value === undefined || SKIP.has(name.toLowerCase())) continue;
    for (const one of Array.isArray(value) ? value : [value]) {
      try {
        headers.append(name, one);
      } catch {
        // A header we cannot represent is a header the handler does not read.
      }
    }
  }
  return headers;
}

export function createHandler(env) {
  return async (req, res) => {
    // Railway terminates TLS in front of us, so the scheme the app should
    // report is the forwarded one. It matters: POST /builds derives a bundle's
    // download URL from this, and an http:// URL in a firmware manifest would
    // be handed to the phone.
    const proto = req.headers["x-forwarded-proto"]?.split(",")[0]?.trim() || "http";
    const host = req.headers["x-forwarded-host"] || req.headers.host || "localhost";
    const url = new URL(req.url, `${proto}://${host}`);

    try {
      const hasBody = req.method !== "GET" && req.method !== "HEAD";
      const body = hasBody ? await readBody(req) : undefined;

      const request = new Request(url, {
        method: req.method,
        headers: toHeaders(req.headers),
        body: body && body.length ? body : undefined,
      });

      const response = await worker.fetch(request, env);

      res.writeHead(response.status, Object.fromEntries(response.headers));
      if (response.body) {
        res.end(Buffer.from(await response.arrayBuffer()));
      } else {
        res.end();
      }
    } catch (e) {
      const status = e.status || 500;
      // Never echo the message back: it can carry a filesystem path.
      if (status === 500) console.error("request failed:", e);
      res.writeHead(status, { "content-type": "application/json" });
      res.end(JSON.stringify({ error: status === 413 ? "body too large" : "internal" }));
    }
  };
}

function main() {
  const dir = process.env.STONE_DATA_DIR || "/data";
  const port = Number(process.env.PORT || 8080);

  const env = {
    STONE: new FileKV(dir),
    STONE_PUBLISH_TOKEN: process.env.STONE_PUBLISH_TOKEN,
    STONE_CONTROL_TOKEN: process.env.STONE_CONTROL_TOKEN,
  };

  // Refuse to start without them rather than starting an open server: an
  // unauthenticated POST /builds would let anyone hand your watch firmware.
  for (const name of ["STONE_PUBLISH_TOKEN", "STONE_CONTROL_TOKEN"]) {
    if (!env[name]) {
      console.error(`${name} is not set; refusing to start`);
      process.exit(1);
    }
  }

  const server = createServer(createHandler(env));
  server.listen(port, () => {
    console.log(`stone-channel listening on ${port}, data in ${dir}`);
  });

  for (const signal of ["SIGTERM", "SIGINT"]) {
    process.on(signal, () => server.close(() => process.exit(0)));
  }
}

// Only start a server when run directly, so the tests can import the handler.
function isMain() {
  if (!process.argv[1]) return false;
  try {
    return realpathSync(process.argv[1]) === realpathSync(fileURLToPath(import.meta.url));
  } catch {
    return false;
  }
}

if (isMain()) main();
