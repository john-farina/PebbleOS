// SPDX-FileCopyrightText: 2026 John Farina
// SPDX-License-Identifier: Apache-2.0

// The Stone channel server.
//
// It answers one question the companion app already knows how to ask: "is
// there a newer build for the channel this watch follows, and where do I get
// it?" The response shape is not ours to choose — `EngDashOta.kt` in the app
// speaks it already, so matching it is what makes update cards appear for our
// own branches with no protocol work at all.
//
// Bundles live in the same KV namespace as the metadata. That is not the
// obvious choice, but the obvious ones are worse: GitHub release assets need a
// tag per build, and `git describe` has no --match here, so tag churn would
// make every firmware version relative to the last build instead of to an
// upstream release. A KV value holds 25 MB against a ~3 MB bundle, and 1 GB of
// free storage is around three hundred of them.
//
// The boot-priority re-stamp that makes a rollback actually boot happens in
// CI, not here — see docs/stone/recovery.md.

const JSON_HEADERS = { "content-type": "application/json; charset=utf-8" };

function json(body, status = 200) {
  return new Response(JSON.stringify(body, null, 2) + "\n", {
    status,
    headers: JSON_HEADERS,
  });
}

function error(status, message) {
  return json({ error: message }, status);
}

// Constant-time-ish compare so a token cannot be recovered a byte at a time.
// Not a hot path, and the alternative is a genuine (if slow) leak.
function tokenMatches(given, expected) {
  if (!expected || !given || given.length !== expected.length) return false;
  let diff = 0;
  for (let i = 0; i < given.length; i++) {
    diff |= given.charCodeAt(i) ^ expected.charCodeAt(i);
  }
  return diff === 0;
}

function authorized(request, expected) {
  const header = request.headers.get("authorization") || "";
  const [scheme, value] = header.split(" ");
  return scheme === "Bearer" && tokenMatches(value || "", expected || "");
}

// `git describe` gives v4.36.0-98-g137d1852. The app parses
// v?(\d+)\.(\d+)(?:\.(\d+))?(?:-(.*))? and ignores anything past the patch, so
// two builds off the same upstream base compare equal to it. That is why the
// server decides `is_downgrade` rather than leaving it to the app.
const VERSION_RE = /^v?(\d+)\.(\d+)(?:\.(\d+))?/;

function versionTriple(version) {
  const m = VERSION_RE.exec(version || "");
  if (!m) return null;
  return [Number(m[1]), Number(m[2]), Number(m[3] || 0)];
}

function compareVersions(a, b) {
  const x = versionTriple(a);
  const y = versionTriple(b);
  if (!x || !y) return 0;
  for (let i = 0; i < 3; i++) {
    if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
  }
  return 0;
}

// A downgrade is only a downgrade if the *upstream base* goes backwards. Hops
// between branch builds off the same base are not, and calling them downgrades
// would send the watch through PRF — which invalidates both slots — for what is
// meant to be a routine switch.
function isDowngrade(from, to) {
  return compareVersions(to, from) < 0;
}

const DEFAULT_CHANNEL = "stone";

async function getDevice(env, serial) {
  if (!serial) return null;
  return await env.STONE.get(`device:${serial}`, "json");
}

async function channelFor(env, serial) {
  const device = await getDevice(env, serial);
  return device?.channel || DEFAULT_CHANNEL;
}

// GET /ota/latest — the endpoint the app polls.
//
// 204 means "nothing new", which is the answer most of the time and the one
// that must be cheap. A 404 here would surface as an error in the app.
async function otaLatest(request, env, url) {
  const serial = url.searchParams.get("device_serial") || "";
  const hardware = url.searchParams.get("hardware_version") || "";
  const current = url.searchParams.get("current_version") || "";

  const channel = await channelFor(env, serial);
  const build = await env.STONE.get(`channel:${channel}`, "json");

  // Record what the watch is actually running. This is the only place we ever
  // learn that, and channel cleanup depends on knowing which build a device is
  // on before anything is deleted out from under it.
  if (serial) {
    const device = (await getDevice(env, serial)) || { channel };
    await env.STONE.put(
      `device:${serial}`,
      JSON.stringify({
        ...device,
        channel,
        hardware_version: hardware || device.hardware_version || null,
        last_version: current || null,
        last_seen: new Date().toISOString(),
      }),
    );
  }

  if (!build) return new Response(null, { status: 204 });

  // Hardware is not negotiable: a bundle for another board will not install,
  // and offering it is worse than offering nothing.
  if (hardware && build.hardware_version && hardware !== build.hardware_version) {
    return new Response(null, { status: 204 });
  }

  if (current && build.version === current) {
    return new Response(null, { status: 204 });
  }

  // An explicit url in the manifest wins, for a bundle hosted elsewhere.
  // Otherwise it is this server, at whatever host the request arrived on.
  const href =
    build.url ||
    (build.bundle &&
      new URL(
        `/bundles/${encodeURIComponent(build.channel)}/${encodeURIComponent(build.bundle)}`,
        request.url,
      ).toString());
  if (!href) return new Response(null, { status: 204 });

  return json({
    version: build.version,
    notes: renderNotes(build),
    is_downgrade: isDowngrade(current, build.version),
    artifacts: [{ url: href }],
  });
}

// The only description of a build anyone reads is this card, so it carries the
// channel and commit as well as the subjects — "which of my branches is this?"
// is the question you actually have at 1am.
function renderNotes(build) {
  const lines = [];
  if (build.channel) lines.push(`channel: ${build.channel}`);
  if (build.commit_short) lines.push(`commit: ${build.commit_short}`);
  if (build.base) lines.push(`base: ${build.base}`);
  if (Array.isArray(build.notes) && build.notes.length) {
    lines.push("");
    for (const note of build.notes) lines.push(`• ${note}`);
  }
  return lines.join("\n");
}

// POST /builds — CI publishing a manifest. The body is what
// tools/stone/make_manifest.py emits, plus the URL the bundle ended up at.
async function publishBuild(request, env) {
  let manifest;
  try {
    manifest = await request.json();
  } catch {
    return error(400, "body is not JSON");
  }

  for (const field of ["channel", "version"]) {
    if (!manifest[field]) return error(400, `missing ${field}`);
  }
  if (!manifest.url && !manifest.bundle) {
    return error(400, "missing url or bundle");
  }

  // Deliberately no derived URL stored here. CI knows the bundle's filename but
  // not the hostname it will be served from, and baking in whichever host CI
  // happened to reach would orphan every stored build the day a custom domain
  // is added. The URL is derived per request instead, in otaLatest.
  const build = { ...manifest, published_at: new Date().toISOString() };

  // Keyed by commit as well as channel: a rebased feat/* branch orphans its
  // earlier builds' SHAs, and those bundles are still installable. Keeping the
  // history is what makes "go back to the one that worked" possible.
  if (build.commit) {
    await env.STONE.put(
      `build:${build.channel}:${build.commit}`,
      JSON.stringify(build),
    );
  }
  await env.STONE.put(`channel:${build.channel}`, JSON.stringify(build));

  return json({ published: build.channel, version: build.version }, 201);
}

// PUT /bundles/:channel/:name — CI handing over the bytes.
//
// KV caps a value at 25 MB, which a dual-slot obelix bundle is comfortably
// under. Refuse rather than truncate: a partially stored bundle would fail at
// install time, on the watch, with nothing useful to say.
const MAX_BUNDLE_BYTES = 25 * 1024 * 1024;

async function putBundle(request, env, channel, name) {
  const body = await request.arrayBuffer();
  if (body.byteLength === 0) return error(400, "empty body");
  if (body.byteLength > MAX_BUNDLE_BYTES) {
    return error(413, `bundle is ${body.byteLength} bytes, limit is ${MAX_BUNDLE_BYTES}`);
  }

  await env.STONE.put(bundleKey(channel, name), body);
  return json({ stored: name, channel, size: body.byteLength }, 201);
}

function bundleKey(channel, name) {
  return `bundle:${channel}:${name}`;
}

async function getBundle(env, channel, name) {
  const body = await env.STONE.get(bundleKey(channel, name), "arrayBuffer");
  if (!body) return error(404, "no such bundle");
  return new Response(body, {
    headers: {
      "content-type": "application/octet-stream",
      "content-disposition": `attachment; filename="${name}"`,
      // Bundles are immutable: the name carries the version and the commit.
      "cache-control": "public, max-age=31536000, immutable",
    },
  });
}

// GET /channels — what the branch picker lists.
async function listChannels(env) {
  const listed = await env.STONE.list({ prefix: "channel:" });
  const channels = [];
  for (const key of listed.keys) {
    const build = await env.STONE.get(key.name, "json");
    if (!build) continue;
    channels.push({
      channel: build.channel,
      version: build.version,
      commit_short: build.commit_short || null,
      base: build.base || null,
      built_at: build.built_at || null,
      published_at: build.published_at || null,
    });
  }
  channels.sort((a, b) => a.channel.localeCompare(b.channel));
  return json({ channels });
}

// GET /builds/:channel — the history behind one channel, for rolling back.
async function listBuilds(env, channel) {
  const listed = await env.STONE.list({ prefix: `build:${channel}:` });
  const builds = [];
  for (const key of listed.keys) {
    const build = await env.STONE.get(key.name, "json");
    if (build) builds.push(build);
  }
  builds.sort((a, b) => (b.published_at || "").localeCompare(a.published_at || ""));
  return json({ channel, builds });
}

// PUT /device/:serial/channel — switch which channel a watch follows.
//
// Deliberately refuses an unknown channel. A typo would otherwise silently
// park the watch on a channel that never gets a build, and the symptom would
// be "updates stopped working" days later.
async function setDeviceChannel(request, env, serial) {
  let body;
  try {
    body = await request.json();
  } catch {
    return error(400, "body is not JSON");
  }

  const channel = body?.channel;
  if (!channel) return error(400, "missing channel");

  if (!(await env.STONE.get(`channel:${channel}`))) {
    return error(404, `no builds published for channel '${channel}'`);
  }

  const device = (await getDevice(env, serial)) || {};
  const updated = {
    ...device,
    channel,
    channel_set_at: new Date().toISOString(),
  };
  await env.STONE.put(`device:${serial}`, JSON.stringify(updated));
  return json(updated);
}

// DELETE /channels/:channel — retire a merged or abandoned branch.
//
// Refuses while a watch still follows it. Deleting a channel out from under a
// device strands it on firmware that will never update again, and it fails
// silently: the watch just stops being offered anything.
async function retireChannel(request, env, channel, url) {
  if (channel === DEFAULT_CHANNEL) {
    return error(400, `refusing to retire '${DEFAULT_CHANNEL}'`);
  }

  const devices = await env.STONE.list({ prefix: "device:" });
  const following = [];
  for (const key of devices.keys) {
    const device = await env.STONE.get(key.name, "json");
    if (device?.channel === channel) following.push(key.name.slice("device:".length));
  }

  if (following.length && url.searchParams.get("migrate") !== "true") {
    return json(
      {
        error: `${following.length} device(s) still follow '${channel}'`,
        devices: following,
        hint: `retry with ?migrate=true to move them to '${DEFAULT_CHANNEL}' first`,
      },
      409,
    );
  }

  for (const serial of following) {
    const device = await env.STONE.get(`device:${serial}`, "json");
    await env.STONE.put(
      `device:${serial}`,
      JSON.stringify({
        ...device,
        channel: DEFAULT_CHANNEL,
        channel_set_at: new Date().toISOString(),
        migrated_from: channel,
      }),
    );
  }

  // Take the bytes with it. Bundles are the only large values in KV, so a
  // channel that leaves its behind is the one way this fills up 1 GB.
  const bundles = await env.STONE.list({ prefix: `bundle:${channel}:` });
  for (const key of bundles.keys) await env.STONE.delete(key.name);

  const builds = await env.STONE.list({ prefix: `build:${channel}:` });
  for (const key of builds.keys) await env.STONE.delete(key.name);

  await env.STONE.delete(`channel:${channel}`);
  return json({
    retired: channel,
    migrated: following,
    deleted_bundles: bundles.keys.length,
  });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";
    const method = request.method;

    if (method === "GET" && path === "/") {
      return json({ service: "stone-channel", default_channel: DEFAULT_CHANNEL });
    }

    if (method === "GET" && path === "/ota/latest") {
      return otaLatest(request, env, url);
    }

    if (method === "GET" && path === "/channels") {
      return listChannels(env);
    }

    let m;
    if (method === "GET" && (m = /^\/builds\/(.+)$/.exec(path))) {
      return listBuilds(env, decodeURIComponent(m[1]));
    }

    if (method === "GET" && (m = /^\/device\/([^/]+)$/.exec(path))) {
      const device = await getDevice(env, decodeURIComponent(m[1]));
      return device ? json(device) : error(404, "unknown device");
    }

    // Unauthenticated on purpose: the app downloads this, and putting a token
    // in the firmware URL would leak it into logs on both sides. The names are
    // unguessable enough and the contents are our own open-source build.
    if (method === "GET" && (m = /^\/bundles\/(.+)\/([^/]+)$/.exec(path))) {
      return getBundle(env, decodeURIComponent(m[1]), decodeURIComponent(m[2]));
    }

    // Everything below writes, and everything below needs the token.
    if (method === "POST" && path === "/builds") {
      if (!authorized(request, env.STONE_PUBLISH_TOKEN)) return error(401, "unauthorized");
      return publishBuild(request, env);
    }

    if (method === "PUT" && (m = /^\/bundles\/(.+)\/([^/]+)$/.exec(path))) {
      if (!authorized(request, env.STONE_PUBLISH_TOKEN)) return error(401, "unauthorized");
      return putBundle(request, env, decodeURIComponent(m[1]), decodeURIComponent(m[2]));
    }

    if (method === "PUT" && (m = /^\/device\/([^/]+)\/channel$/.exec(path))) {
      if (!authorized(request, env.STONE_CONTROL_TOKEN)) return error(401, "unauthorized");
      return setDeviceChannel(request, env, decodeURIComponent(m[1]));
    }

    if (method === "DELETE" && (m = /^\/channels\/(.+)$/.exec(path))) {
      if (!authorized(request, env.STONE_PUBLISH_TOKEN)) return error(401, "unauthorized");
      return retireChannel(request, env, decodeURIComponent(m[1]), url);
    }

    return error(404, `no route for ${method} ${path}`);
  },
};
