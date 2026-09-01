// SPDX-FileCopyrightText: 2026 John Farina
// SPDX-License-Identifier: Apache-2.0

// A KV store on a disk, with the same surface Cloudflare's binding exposes.
//
// The Worker's fetch handler is the single source of truth for what this
// service does, and it only ever touches `env.STONE` through get/put/delete/
// list. Implementing those four against a Railway volume is the whole port —
// no second copy of the routing, and the same 28 tests cover both.

import { createHash } from "node:crypto";
import { mkdir, readdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";

// Keys hold slashes and colons (`bundle:feat/thing:stone.pbz`), so they are
// percent-encoded into a single flat filename. That keeps every key in one
// directory and makes path traversal impossible rather than merely unlikely.
//
// Long channel names can still overrun a 255-byte filename, so anything close
// to the limit is hashed instead. The plain name is kept as a prefix so a
// directory listing stays readable.
const MAX_NAME = 200;

function encodeKey(key) {
  const encoded = encodeURIComponent(key);
  if (encoded.length <= MAX_NAME) return encoded;
  const digest = createHash("sha256").update(key).digest("hex").slice(0, 32);
  return `${encoded.slice(0, MAX_NAME - 33)}~${digest}`;
}

export class FileKV {
  constructor(dir) {
    this.dir = dir;
    // Names are recoverable from the filename except for hashed ones, so the
    // original key travels in a sidecar index kept in memory and rebuilt from
    // disk on demand.
    this.ready = mkdir(dir, { recursive: true });
  }

  #path(key) {
    return join(this.dir, encodeKey(key));
  }

  async get(key, type) {
    await this.ready;
    let body;
    try {
      body = await readFile(this.#path(key));
    } catch (e) {
      if (e.code === "ENOENT") return null;
      throw e;
    }
    if (type === "json") return JSON.parse(body.toString("utf8"));
    if (type === "arrayBuffer") {
      return body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength);
    }
    return body.toString("utf8");
  }

  async put(key, value) {
    await this.ready;
    const body =
      typeof value === "string"
        ? Buffer.from(value, "utf8")
        : Buffer.from(value instanceof ArrayBuffer ? value : value.buffer ?? value);

    // Write then rename: a crash partway through a 3 MB bundle would otherwise
    // leave a truncated file that installs and bricks nothing, but fails a CRC
    // check on the watch with no explanation.
    const target = this.#path(key);
    const temp = `${target}.tmp-${process.pid}-${Date.now()}`;
    await writeFile(temp, body);
    await rename(temp, target);

    if (encodeKey(key) !== encodeURIComponent(key)) {
      await writeFile(`${target}.key`, key, "utf8");
    }
  }

  async delete(key) {
    await this.ready;
    const target = this.#path(key);
    await rm(target, { force: true });
    await rm(`${target}.key`, { force: true });
  }

  async list({ prefix = "" } = {}) {
    await this.ready;
    const names = await readdir(this.dir);
    const keys = [];
    for (const name of names) {
      if (name.endsWith(".key") || name.includes(".tmp-")) continue;
      let key;
      if (name.includes("~")) {
        try {
          key = await readFile(join(this.dir, `${name}.key`), "utf8");
        } catch {
          continue;
        }
      } else {
        key = decodeURIComponent(name);
      }
      if (key.startsWith(prefix)) keys.push({ name: key });
    }
    keys.sort((a, b) => a.name.localeCompare(b.name));
    return { keys, list_complete: true };
  }
}
