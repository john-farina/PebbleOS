/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Watchface thumbnails: capture, downscale, store, load.
//
// The capture point is the moment a watchface stops being the foreground app. At that instant the
// system framebuffer still holds its last composited frame -- nothing clears it until the
// *incoming* app's first render -- and we are on KernelMain, so reading it cannot race a
// composite. That is a narrow window, and it is the only one: there is one app framebuffer, so a
// face that is not running cannot be drawn.
//
// Pixels are ARGB2222, two bits per channel. They cannot be averaged as bytes -- that would mix
// alpha into red and produce colours that are not in the image -- so the box filter below
// averages each channel separately and repacks.

#include "stone_face_thumb.h"

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

#include "kernel/pbl_malloc.h"
#include "pbl/services/compositor/compositor.h"
#include "pbl/services/system_task.h"
#include "process_management/app_install_manager.h"
#include "pbl/services/settings/settings_file.h"
#include "pbl/services/settings/settings_raw_iter.h"
#include "pbl/logging/logging.h"
#include "debug/stone_trace.h"
#include "system/passert.h"

#include <inttypes.h>
#include <string.h>

//! One file for every face rather than one file each: PFS allocates whole 4KB pages, so a file
//! per thumbnail would waste most of a page apiece.
#define THUMB_FILE_NAME "stone_wf_thumbs"

//! How many faces the cache holds. Beyond this the file is full and further captures are refused
//! -- loudly, see prv_store. Eight covers any realistic collection, and at ~5KB apiece the whole
//! cache is about 1% of the 5MB filesystem.
#define THUMB_MAX_FACES (8)

//! Sized for every chunk of every face, plus each record's header and key, plus headroom for the
//! settings layer to compact into. This is a *ceiling*, not an allocation -- see THUMB_FILE_SEED.
#define THUMB_FILE_SIZE \
  (((STONE_FACE_THUMB_BYTES + (STONE_FACE_THUMB_CHUNKS * 32)) * THUMB_MAX_FACES) * 2)

//! What the file is actually created at, growing by doubling as thumbnails arrive.
//!
//! settings_file_open() allocates its maximum up front, and this file's maximum is ~80KB: opening
//! it would erase twenty NOR sectors before returning. That is a second or more of blocking flash
//! work, and the read side runs on the app task inside the picker's window load -- so the first
//! open of the picker on a fresh watch would hang it. Seeding small costs an occasional grow on
//! the write path, which already runs on the system task where slow flash work belongs.
#define THUMB_FILE_SEED (4096)

#define SCALE (3)

//! A record holds one chunk of one face. The chunk index is part of the key rather than the value
//! so a partially written thumbnail cannot be read back as a whole one: a missing chunk is a
//! missing key, and the load fails.
typedef struct PACKED {
  Uuid uuid;
  uint8_t chunk;
} ThumbKey;

_Static_assert(STONE_FACE_THUMB_CHUNK_BYTES <= SETTINGS_VAL_MAX_LEN,
               "a thumbnail chunk must fit in one settings record");
_Static_assert(sizeof(ThumbKey) <= SETTINGS_KEY_MAX_LEN, "thumbnail key too long");

typedef struct {
  Uuid uuid;
  uint8_t pixels[];
} ThumbWrite;

//! Bytes in chunk @p i. Only the last one is short.
static size_t prv_chunk_len(uint8_t i) {
  const size_t offset = (size_t)i * STONE_FACE_THUMB_CHUNK_BYTES;
  const size_t remaining = STONE_FACE_THUMB_BYTES - offset;
  return (remaining < STONE_FACE_THUMB_CHUNK_BYTES) ? remaining : STONE_FACE_THUMB_CHUNK_BYTES;
}

// Runs on the system task: pfs_write erases and programs NOR flash, which is far too slow to sit
// inside an app switch.
static void prv_store(void *ctx) {
  ThumbWrite *write = ctx;
  SettingsFile file;

  const status_t open_rv =
      settings_file_open_growable(&file, THUMB_FILE_NAME, THUMB_FILE_SIZE, THUMB_FILE_SEED);
  if (open_rv != S_SUCCESS) {
    // Every earlier version of this function discarded its status, which is why a cache that
    // stored nothing at all looked exactly like a cache of faces that had never been worn.
    PBL_LOG_WRN("thumb: could not open the cache (%" PRId32 ")", (int32_t)open_rv);
    kernel_free(write);
    return;
  }

  for (uint8_t i = 0; i < STONE_FACE_THUMB_CHUNKS; i++) {
    const ThumbKey key = { .uuid = write->uuid, .chunk = i };
    const status_t rv =
        settings_file_set(&file, &key, sizeof(key),
                          &write->pixels[(size_t)i * STONE_FACE_THUMB_CHUNK_BYTES],
                          prv_chunk_len(i));
    if (rv != S_SUCCESS) {
      // A partial thumbnail is never served: prv_load requires every chunk. E_OUT_OF_STORAGE here
      // means THUMB_MAX_FACES has been reached, which is a cap rather than a fault.
      PBL_LOG_WRN("thumb: chunk %" PRIu8 " not stored (%" PRId32 ")", i, (int32_t)rv);
      stone_trace(StoneTraceThumb, 3, (int16_t)i, (int16_t)rv);
      break;
    }
  }

  settings_file_close(&file);
  kernel_free(write);
}

// Box-average a SCALE x SCALE block, per channel. Alpha is averaged too and then forced opaque:
// a watchface frame is opaque, and a rounded-down alpha would render the thumbnail see-through.
static uint8_t prv_sample_block(const GBitmap *src, int16_t sx, int16_t sy) {
  uint16_t r = 0, g = 0, b = 0;
  uint8_t taken = 0;

  for (int16_t dy = 0; dy < SCALE; dy++) {
    const int16_t y = sy + dy;
    if (y >= src->bounds.size.h) {
      break;
    }
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(src, (uint16_t)y);
    for (int16_t dx = 0; dx < SCALE; dx++) {
      const int16_t x = sx + dx;
      // Row info carries the valid span for this row, which on a round display is narrower than
      // the bitmap. Respecting it costs nothing on a rectangle and is correct on both.
      if ((x < row.min_x) || (x > row.max_x)) {
        continue;
      }
      const GColor8 c = (GColor8){ .argb = row.data[x] };
      r += c.r;
      g += c.g;
      b += c.b;
      taken++;
    }
  }

  if (taken == 0) {
    return GColorBlack.argb;
  }
  return (GColor8){ .a = 3, .r = r / taken, .g = g / taken, .b = b / taken }.argb;
}

void stone_face_thumb_capture(AppInstallId install_id) {
  PBL_ASSERT_TASK(PebbleTask_KernelMain);

  if (install_id == INSTALL_ID_INVALID) {
    return;
  }
  AppInstallEntry entry;
  if (!app_install_get_entry_for_install_id(install_id, &entry)) {
    return;
  }
  if (!app_install_entry_is_watchface(&entry)) {
    return;
  }
  // 1: a face is being photographed. Absent from a capture means the cache is not filling, which
  // is a different problem from a thumbnail that was stored and could not be read back.
  stone_trace(StoneTraceThumb, 1, (int16_t)install_id, 0);

  ThumbWrite *write = kernel_malloc(sizeof(ThumbWrite) + STONE_FACE_THUMB_BYTES);
  if (!write) {
    return;
  }
  write->uuid = entry.uuid;

  // The system framebuffer, not the app's: on a graceful exit the app has already torn its own
  // state down and its framebuffer memory is about to be cleared, while the system one still
  // holds the composited frame and has no such lifetime hazard.
  GBitmap src = compositor_get_framebuffer_as_bitmap();

  for (int16_t y = 0; y < STONE_FACE_THUMB_H; y++) {
    for (int16_t x = 0; x < STONE_FACE_THUMB_W; x++) {
      write->pixels[(y * STONE_FACE_THUMB_W) + x] =
          prv_sample_block(&src, (int16_t)(x * SCALE), (int16_t)(y * SCALE));
    }
  }

  if (!system_task_add_callback(prv_store, write)) {
    kernel_free(write);
  }
}

bool stone_face_thumb_load(const Uuid *uuid, uint8_t *buffer) {
  SettingsFile file;
  if (settings_file_open_growable(&file, THUMB_FILE_NAME, THUMB_FILE_SIZE, THUMB_FILE_SEED) !=
      S_SUCCESS) {
    return false;
  }

  // All or nothing: a face whose capture was interrupted has some of its chunks, and half a
  // thumbnail drawn over an uninitialised buffer is worse than the icon fallback.
  bool ok = true;
  for (uint8_t i = 0; i < STONE_FACE_THUMB_CHUNKS; i++) {
    const ThumbKey key = { .uuid = *uuid, .chunk = i };
    if (settings_file_get(&file, &key, sizeof(key),
                          &buffer[(size_t)i * STONE_FACE_THUMB_CHUNK_BYTES],
                          prv_chunk_len(i)) != S_SUCCESS) {
      ok = false;
      break;
    }
  }

  settings_file_close(&file);
  // 4/5: whether a face had a stored miniature. A picker showing icons is expected while these
  // are 5 -- the cache only fills as faces are worn -- and a bug once they are 4 and it still
  // draws an icon.
  stone_trace(StoneTraceThumb, ok ? 4 : 5, 0, 0);
  return ok;
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
