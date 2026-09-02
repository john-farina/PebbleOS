// SPDX-FileCopyrightText: 2026 John Farina
// SPDX-License-Identifier: Apache-2.0

// Run with `node --test test/`. No dependencies and no wrangler: the Worker is
// a plain fetch handler, so a fake KV and a Request are the whole harness.

import assert from "node:assert/strict";
import { beforeEach, describe, it } from "node:test";

import worker from "../src/index.js";

const PUBLISH = "publish-token";
const CONTROL = "control-token";
const SERIAL = "Q402ABCD1234";

class FakeKV {
  constructor() {
    this.store = new Map();
  }
  async get(key, type) {
    const value = this.store.get(key);
    if (value === undefined) return null;
    if (type === "json") return JSON.parse(value);
    if (type === "arrayBuffer") return value;
    return value;
  }
  async put(key, value) {
    this.store.set(key, value);
  }
  async delete(key) {
    this.store.delete(key);
  }
  async list({ prefix }) {
    const keys = [...this.store.keys()]
      .filter((k) => k.startsWith(prefix))
      .sort()
      .map((name) => ({ name }));
    return { keys, list_complete: true };
  }
}

let env;

function call(method, path, { body, raw, token } = {}) {
  const headers = {};
  if (token) headers.authorization = `Bearer ${token}`;
  if (body !== undefined) headers["content-type"] = "application/json";
  return worker.fetch(
    new Request(`https://stone.test${path}`, {
      method,
      headers,
      body: raw !== undefined ? raw : body === undefined ? undefined : JSON.stringify(body),
    }),
    env,
  );
}

function manifest(overrides = {}) {
  return {
    channel: "stone",
    version: "v4.36.0-98-g137d1852",
    hardware_version: "obelix_pvt",
    commit: "137d1852000000000000000000000000000000aa",
    commit_short: "137d185",
    base: "v4.36.0",
    bundle: "stone_obelix_pvt_v4.36.0-98-g137d1852.pbz",
    notes: ["stone: add fork foundations"],
    url: "https://example.invalid/stone.pbz",
    ...overrides,
  };
}

async function publish(overrides) {
  const res = await call("POST", "/builds", {
    body: manifest(overrides),
    token: PUBLISH,
  });
  assert.equal(res.status, 201);
  return res;
}

beforeEach(() => {
  env = {
    STONE: new FakeKV(),
    STONE_PUBLISH_TOKEN: PUBLISH,
    STONE_CONTROL_TOKEN: CONTROL,
  };
});

describe("publishing", () => {
  it("requires the publish token", async () => {
    assert.equal((await call("POST", "/builds", { body: manifest() })).status, 401);
    assert.equal(
      (await call("POST", "/builds", { body: manifest(), token: CONTROL })).status,
      401,
    );
  });

  it("rejects a manifest missing what the app needs", async () => {
    for (const field of ["channel", "version"]) {
      const body = manifest();
      delete body[field];
      const res = await call("POST", "/builds", { body, token: PUBLISH });
      assert.equal(res.status, 400, `${field} should be required`);
      assert.match((await res.json()).error, new RegExp(field));
    }
  });

  it("rejects a manifest that points at no bundle at all", async () => {
    const body = manifest();
    delete body.url;
    delete body.bundle;
    const res = await call("POST", "/builds", { body, token: PUBLISH });
    assert.equal(res.status, 400);
  });

  it("derives the download URL from the request, not from publish time", async () => {
    // CI knows the filename but not the hostname it will be served from, so
    // the URL is derived per request. Storing it at publish time would orphan
    // every build the day a custom domain is added.
    const body = manifest();
    delete body.url;
    await call("POST", "/builds", { body, token: PUBLISH });

    const res = await call("GET", `/ota/latest?device_serial=${SERIAL}`);
    assert.equal(
      (await res.json()).artifacts[0].url,
      "https://stone.test/bundles/stone/stone_obelix_pvt_v4.36.0-98-g137d1852.pbz",
    );
  });

  it("keeps history per commit as well as the channel head", async () => {
    await publish();
    await publish({ version: "v4.36.0-99-gdeadbee", commit: "deadbee", commit_short: "deadbee" });

    const builds = await (await call("GET", "/builds/stone")).json();
    assert.equal(builds.builds.length, 2);

    // A rebased branch orphans its earlier SHAs, and those bundles are still
    // installable — losing them would lose the way back.
    const head = await env.STONE.get("channel:stone", "json");
    assert.equal(head.version, "v4.36.0-99-gdeadbee");
  });
});

describe("GET /ota/latest", () => {
  it("is 204 when the channel has nothing", async () => {
    const res = await call("GET", `/ota/latest?device_serial=${SERIAL}`);
    assert.equal(res.status, 204);
  });

  it("is 204 when the watch already runs the latest", async () => {
    await publish();
    const res = await call(
      "GET",
      `/ota/latest?device_serial=${SERIAL}&current_version=v4.36.0-98-g137d1852`,
    );
    assert.equal(res.status, 204);
  });

  it("offers a build in the shape the app parses", async () => {
    await publish();
    const res = await call(
      "GET",
      `/ota/latest?device_serial=${SERIAL}&hardware_version=obelix_pvt&current_version=v4.36.0-90-gaaaaaaa`,
    );
    assert.equal(res.status, 200);

    const body = await res.json();
    assert.equal(body.version, "v4.36.0-98-g137d1852");
    assert.equal(body.is_downgrade, false);
    assert.deepEqual(body.artifacts, [{ url: "https://example.invalid/stone.pbz" }]);
    assert.match(body.notes, /channel: stone/);
    assert.match(body.notes, /stone: add fork foundations/);
  });

  it("never offers a bundle built for another board", async () => {
    await publish();
    const res = await call(
      "GET",
      `/ota/latest?device_serial=${SERIAL}&hardware_version=snowy_dvt`,
    );
    assert.equal(res.status, 204);
  });

  it("records what the watch is running", async () => {
    await publish();
    await call(
      "GET",
      `/ota/latest?device_serial=${SERIAL}&hardware_version=obelix_pvt&current_version=v4.36.0-90-gaaaaaaa`,
    );

    const device = await (await call("GET", `/device/${SERIAL}`)).json();
    assert.equal(device.last_version, "v4.36.0-90-gaaaaaaa");
    assert.equal(device.hardware_version, "obelix_pvt");
    assert.equal(device.channel, "stone");
    assert.ok(device.last_seen);
  });

  it("serves an unknown watch the default channel", async () => {
    await publish();
    const res = await call("GET", "/ota/latest");
    assert.equal(res.status, 200);
    assert.equal((await res.json()).version, "v4.36.0-98-g137d1852");
  });
});

describe("is_downgrade", () => {
  it("is false between branches off the same upstream base", async () => {
    // The case that matters: hopping branches must not send the watch through
    // PRF, which invalidates both slots.
    await publish({ channel: "feat/thing", version: "v4.36.0-120-gfeed000" });
    await call("PUT", `/device/${SERIAL}/channel`, {
      body: { channel: "feat/thing" },
      token: CONTROL,
    });

    const res = await call(
      "GET",
      `/ota/latest?device_serial=${SERIAL}&current_version=v4.36.0-98-g137d1852`,
    );
    assert.equal((await res.json()).is_downgrade, false);
  });

  it("is true when the upstream base goes backwards", async () => {
    await publish({ version: "v4.35.0-10-gaaaaaaa" });
    const res = await call(
      "GET",
      `/ota/latest?device_serial=${SERIAL}&current_version=v4.36.0-98-g137d1852`,
    );
    assert.equal((await res.json()).is_downgrade, true);
  });
});

describe("switching channels", () => {
  it("requires the control token", async () => {
    await publish({ channel: "feat/thing" });
    const res = await call("PUT", `/device/${SERIAL}/channel`, {
      body: { channel: "feat/thing" },
    });
    assert.equal(res.status, 401);
  });

  it("refuses a channel with no builds", async () => {
    // A typo would otherwise park the watch somewhere that never updates, and
    // the symptom shows up days later as "updates stopped working".
    const res = await call("PUT", `/device/${SERIAL}/channel`, {
      body: { channel: "feat/typo" },
      token: CONTROL,
    });
    assert.equal(res.status, 404);
  });

  it("changes which build the watch is offered", async () => {
    await publish();
    await publish({ channel: "feat/thing", version: "v4.36.0-120-gfeed000" });

    await call("PUT", `/device/${SERIAL}/channel`, {
      body: { channel: "feat/thing" },
      token: CONTROL,
    });

    const res = await call("GET", `/ota/latest?device_serial=${SERIAL}`);
    assert.equal((await res.json()).version, "v4.36.0-120-gfeed000");
  });
});

describe("retiring a channel", () => {
  it("refuses while a watch still follows it", async () => {
    await publish();
    await publish({ channel: "feat/thing", version: "v4.36.0-120-gfeed000" });
    await call("PUT", `/device/${SERIAL}/channel`, {
      body: { channel: "feat/thing" },
      token: CONTROL,
    });

    const res = await call("DELETE", "/channels/feat%2Fthing", { token: PUBLISH });
    assert.equal(res.status, 409);
    assert.deepEqual((await res.json()).devices, [SERIAL]);
    assert.ok(await env.STONE.get("channel:feat/thing"));
  });

  it("migrates devices to the default channel before deleting", async () => {
    await publish();
    await publish({ channel: "feat/thing", version: "v4.36.0-120-gfeed000" });
    await call("PUT", `/device/${SERIAL}/channel`, {
      body: { channel: "feat/thing" },
      token: CONTROL,
    });

    const res = await call("DELETE", "/channels/feat%2Fthing?migrate=true", {
      token: PUBLISH,
    });
    assert.equal(res.status, 200);
    assert.deepEqual((await res.json()).migrated, [SERIAL]);

    // The watch has to land somewhere that still gets builds.
    const device = await (await call("GET", `/device/${SERIAL}`)).json();
    assert.equal(device.channel, "stone");
    assert.equal(device.migrated_from, "feat/thing");

    const res2 = await call("GET", `/ota/latest?device_serial=${SERIAL}`);
    assert.equal((await res2.json()).version, "v4.36.0-98-g137d1852");
  });

  it("refuses to retire the default channel", async () => {
    await publish();
    const res = await call("DELETE", "/channels/stone?migrate=true", { token: PUBLISH });
    assert.equal(res.status, 400);
    assert.ok(await env.STONE.get("channel:stone"));
  });
});

describe("bundles", () => {
  const NAME = "stone_obelix_pvt_v4.36.0-98-g137d1852.pbz";
  const BYTES = new Uint8Array(1024).fill(0x42);

  it("requires the publish token to upload", async () => {
    const res = await call("PUT", `/bundles/stone/${NAME}`, { raw: BYTES });
    assert.equal(res.status, 401);
  });

  it("round-trips the bytes", async () => {
    const put = await call("PUT", `/bundles/stone/${NAME}`, {
      raw: BYTES,
      token: PUBLISH,
    });
    assert.equal(put.status, 201);
    assert.equal((await put.json()).size, 1024);

    const get = await call("GET", `/bundles/stone/${NAME}`);
    assert.equal(get.status, 200);
    assert.equal(get.headers.get("content-type"), "application/octet-stream");
    assert.deepEqual(new Uint8Array(await get.arrayBuffer()), BYTES);
  });

  it("serves a bundle without a token, because the app has none", async () => {
    await call("PUT", `/bundles/stone/${NAME}`, { raw: BYTES, token: PUBLISH });
    assert.equal((await call("GET", `/bundles/stone/${NAME}`)).status, 200);
  });

  it("refuses an empty body rather than storing a broken bundle", async () => {
    const res = await call("PUT", `/bundles/stone/${NAME}`, {
      raw: new Uint8Array(0),
      token: PUBLISH,
    });
    assert.equal(res.status, 400);
  });

  it("404s a bundle that was never uploaded", async () => {
    assert.equal((await call("GET", "/bundles/stone/absent.pbz")).status, 404);
  });

  it("deletes a retired channel's bundles", async () => {
    // Bundles are the only large values in KV, so a retired channel that
    // leaves its behind is the one way this fills up.
    await publish({ channel: "feat/thing", version: "v4.36.0-120-gfeed000" });
    await call("PUT", `/bundles/feat%2Fthing/${NAME}`, {
      raw: BYTES,
      token: PUBLISH,
    });

    const res = await call("DELETE", "/channels/feat%2Fthing", { token: PUBLISH });
    assert.equal(res.status, 200);
    assert.equal((await res.json()).deleted_bundles, 1);
    assert.equal((await call("GET", `/bundles/feat%2Fthing/${NAME}`)).status, 404);
  });
});

describe("trace captures", () => {
  const CAPTURE = "STONE-TRACE BEGIN entries=2 dropped=0\nST 0 touch 0 12 100\nST 30 swipe 8 0 0\nSTONE-TRACE END\n";

  it("requires the publish token to upload", async () => {
    assert.equal((await call("POST", "/traces", { raw: CAPTURE })).status, 401);
  });

  it("round-trips a capture as readable text", async () => {
    // Plain text on the way out matters: the point of a capture is that someone
    // opens it and reads it, not that a tool parses it.
    const post = await call("POST", "/traces", { raw: CAPTURE, token: PUBLISH });
    assert.equal(post.status, 200);
    const { stored } = await post.json();

    const got = await call("GET", `/traces/${stored}`);
    assert.equal(got.status, 200);
    assert.match(got.headers.get("content-type"), /text\/plain/);
    const text = await got.text();
    assert.ok(text.startsWith("# stone-trace "));
    assert.ok(text.endsWith(CAPTURE));
  });

  it("serves a capture without a token, so it can be shared with whoever is helping", async () => {
    const { stored } = await (await call("POST", "/traces", { raw: CAPTURE, token: PUBLISH })).json();
    assert.equal((await call("GET", `/traces/${stored}`)).status, 200);
  });

  it("refuses an empty capture", async () => {
    assert.equal((await call("POST", "/traces", { raw: "   \n", token: PUBLISH })).status, 400);
  });

  it("lists captures newest first", async () => {
    // Ordered by the index rather than by id: two captures uploaded in the same
    // millisecond share a timestamp prefix, so ids alone do not order them.
    await call("POST", "/traces?note=older", { raw: "first\n", token: PUBLISH });
    await call("POST", "/traces?note=newer", { raw: "second\n", token: PUBLISH });
    const { traces } = await (await call("GET", "/traces")).json();
    assert.equal(traces[0].note, "newer");
    assert.equal(traces[1].note, "older");
  });

  it("keeps only the recent captures, deleting what it drops", async () => {
    // An unbounded list would make every listing a slow page nobody reads, and
    // an entry dropped from the index with its capture left behind would leak.
    for (let i = 0; i < 45; i++) {
      await call("POST", `/traces?note=n${i}`, { raw: `capture ${i}\n`, token: PUBLISH });
    }
    const { traces } = await (await call("GET", "/traces")).json();
    assert.equal(traces.length, 40);
    assert.equal(traces[0].note, "n44");
  });

  it("keeps the note that says what the wearer was doing", async () => {
    // Without it a capture is a wall of coordinates with no idea what was being attempted.
    await call("POST", "/traces?note=swiping%20back", { raw: CAPTURE, token: PUBLISH });
    const { traces } = await (await call("GET", "/traces")).json();
    assert.equal(traces[0].note, "swiping back");
  });

  it("404s a capture that does not exist", async () => {
    assert.equal((await call("GET", "/traces/nope")).status, 404);
  });
});

describe("routing", () => {
  it("lists channels for the branch picker", async () => {
    await publish();
    await publish({ channel: "feat/thing", version: "v4.36.0-120-gfeed000" });

    const body = await (await call("GET", "/channels")).json();
    assert.deepEqual(
      body.channels.map((c) => c.channel),
      ["feat/thing", "stone"],
    );
  });

  it("404s an unknown route", async () => {
    assert.equal((await call("GET", "/nope")).status, 404);
  });

  it("404s an unknown device", async () => {
    assert.equal((await call("GET", "/device/nobody")).status, 404);
  });
});
