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

describe("bug reports (the eng-dash routes BugApi.kt speaks)", () => {
  it("creates a report, presigns, stores bytes, completes with a URL or a key", async () => {
    const created = await call("POST", "/bug-reports/create", {
      body: { bugReportDetails: "watch rebooted", username: "john", email: "j@x", timezone: "UTC", summary: "s", latestLogs: "l" },
    });
    assert.equal(created.status, 201);
    const { success, bugReportId } = await created.json();
    assert.equal(success, true);
    assert.ok(bugReportId);

    const presigned = await call("POST", "/upload/presigned", {
      body: { files: [{ fileName: "full logs.txt", fileType: "text/plain", fileSize: 3 }] },
    });
    assert.equal(presigned.status, 200);
    const { uploads } = await presigned.json();
    assert.equal(uploads.length, 1);
    assert.equal(uploads[0].uploadUrl, uploads[0].fileUrl);
    const path = new URL(uploads[0].uploadUrl).pathname;

    const put = await call("PUT", path, { raw: "abc" });
    assert.equal(put.status, 201);

    // The app sends the whole fileUrl back as fileKey when Core's bucket name is absent.
    const done = await call("POST", "/upload/complete", {
      body: { fileKeys: uploads[0].fileUrl, bugReportId },
    });
    assert.equal(done.status, 200);

    const report = await (await call("GET", `/reports/${bugReportId}`)).json();
    assert.equal(report.details, "watch rebooted");
    assert.equal(report.files.length, 1);
    assert.equal(report.files[0], uploads[0].fileUrl);

    const file = await call("GET", path);
    assert.equal(file.status, 200);
    assert.equal(await file.text(), "abc");

    const list = await (await call("GET", "/reports")).json();
    assert.equal(list.reports[0].id, bugReportId);
  });

  it("refuses completion against an unknown report and empty uploads", async () => {
    assert.equal((await call("POST", "/upload/complete", { body: { fileKeys: "k", bugReportId: "nope" } })).status, 404);
    assert.equal((await call("PUT", "/upload/files/k", { raw: "" })).status, 400);
    assert.equal((await call("POST", "/upload/presigned", { body: { files: [] } })).status, 400);
  });
});
