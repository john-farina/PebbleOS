// SPDX-FileCopyrightText: 2026 John Farina
// SPDX-License-Identifier: Apache-2.0

// End-to-end tests for the Railway path: a real HTTP server, a real directory
// on disk, real fetch calls. The Worker tests cover the routing; these cover
// the two things only the Node host can get wrong — persistence and the
// translation between node:http and the fetch handler.

import assert from "node:assert/strict";
import { createServer } from "node:http";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, before, describe, it } from "node:test";

import { FileKV } from "../src/kv_fs.js";
import { createHandler } from "../src/server.js";

const PUBLISH = "publish-token";
const CONTROL = "control-token";
const SERIAL = "Q402ABCD1234";

let dir;
let server;
let base;

before(async () => {
  dir = await mkdtemp(join(tmpdir(), "stone-channel-"));
  const env = {
    STONE: new FileKV(dir),
    STONE_PUBLISH_TOKEN: PUBLISH,
    STONE_CONTROL_TOKEN: CONTROL,
  };
  server = createServer(createHandler(env));
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  base = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await new Promise((resolve) => server.close(resolve));
  await rm(dir, { recursive: true, force: true });
});

const manifest = {
  channel: "feat/thing",
  version: "v4.35.0-140-gabcdef0",
  hardware_version: "obelix_pvt",
  commit: "abcdef0000000000000000000000000000000000",
  commit_short: "abcdef0",
  base: "v4.35.0",
  bundle: "stone_obelix_pvt_v4.35.0-140-gabcdef0.pbz",
  notes: ["stone: something in progress"],
};

describe("the Node host", () => {
  it("answers the root route", async () => {
    const res = await fetch(`${base}/`);
    assert.equal(res.status, 200);
    assert.equal((await res.json()).service, "stone-channel");
  });

  it("carries the Authorization header through to the handler", async () => {
    // The header conversion is the one place a working Worker can become a
    // wide-open server, so assert both sides of it.
    assert.equal(
      (await fetch(`${base}/builds`, { method: "POST", body: "{}" })).status,
      401,
    );
    const res = await fetch(`${base}/builds`, {
      method: "POST",
      headers: { authorization: `Bearer ${PUBLISH}`, "content-type": "application/json" },
      body: JSON.stringify(manifest),
    });
    assert.equal(res.status, 201);
  });

  it("round-trips a bundle through the disk", async () => {
    const bytes = new Uint8Array(64 * 1024);
    for (let i = 0; i < bytes.length; i++) bytes[i] = i & 0xff;

    const put = await fetch(`${base}/bundles/feat%2Fthing/${manifest.bundle}`, {
      method: "PUT",
      headers: { authorization: `Bearer ${PUBLISH}` },
      body: bytes,
    });
    assert.equal(put.status, 201);
    assert.equal((await put.json()).size, bytes.length);

    const got = await fetch(`${base}/bundles/feat%2Fthing/${manifest.bundle}`);
    assert.equal(got.status, 200);
    assert.deepEqual(new Uint8Array(await got.arrayBuffer()), bytes);
  });

  it("derives an https URL from the forwarded scheme", async () => {
    await fetch(`${base}/device/${SERIAL}/channel`, {
      method: "PUT",
      headers: { authorization: `Bearer ${CONTROL}`, "content-type": "application/json" },
      body: JSON.stringify({ channel: "feat/thing" }),
    });

    // Railway terminates TLS in front of us, so without this the manifest
    // would hand the phone an http:// firmware URL.
    const res = await fetch(`${base}/ota/latest?device_serial=${SERIAL}`, {
      headers: { "x-forwarded-proto": "https", "x-forwarded-host": "stone.up.railway.app" },
    });
    assert.equal(res.status, 200);
    assert.equal(
      (await res.json()).artifacts[0].url,
      `https://stone.up.railway.app/bundles/feat%2Fthing/${manifest.bundle}`,
    );
  });

  it("survives a restart with its state intact", async () => {
    // The whole reason for the volume. A restart that forgot which channel the
    // watch follows would silently move it back to stone.
    const fresh = new FileKV(dir);
    assert.equal((await fresh.get(`device:${SERIAL}`, "json")).channel, "feat/thing");
    assert.equal((await fresh.get("channel:feat/thing", "json")).version, manifest.version);
  });

  it("404s an unknown route rather than hanging", async () => {
    assert.equal((await fetch(`${base}/nope`)).status, 404);
  });
});

describe("FileKV", () => {
  it("lists by prefix without matching neighbours", async () => {
    const kv = new FileKV(dir);
    await kv.put("channel:stone", JSON.stringify({ channel: "stone" }));
    const listed = await kv.list({ prefix: "channel:" });
    const names = listed.keys.map((k) => k.name);
    assert.ok(names.includes("channel:stone"));
    assert.ok(names.includes("channel:feat/thing"));
    assert.ok(!names.some((n) => n.startsWith("device:")));
  });

  it("keeps slashes in a key from becoming directories", async () => {
    // encodeURIComponent flattens the key, which is also what makes traversal
    // out of the data directory impossible.
    const kv = new FileKV(dir);
    await kv.put("bundle:../../escape:x", "no");
    assert.equal(await kv.get("bundle:../../escape:x"), "no");
    const listed = await kv.list({ prefix: "bundle:../" });
    assert.equal(listed.keys.length, 1);
  });

  it("returns null for a missing key", async () => {
    assert.equal(await new FileKV(dir).get("channel:nothing", "json"), null);
  });

  it("deletes", async () => {
    const kv = new FileKV(dir);
    await kv.put("scratch:x", "v");
    await kv.delete("scratch:x");
    assert.equal(await kv.get("scratch:x"), null);
  });

  it("handles a key too long for a filename", async () => {
    const kv = new FileKV(dir);
    const key = `channel:feat/${"a".repeat(400)}`;
    await kv.put(key, "long");
    assert.equal(await kv.get(key), "long");
    const listed = await kv.list({ prefix: "channel:feat/" });
    assert.ok(listed.keys.some((k) => k.name === key));
  });
});
