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
#include "system/passert.h"

#include <string.h>

//! One file for every face rather than one file each: PFS allocates whole 4KB pages, so a file
//! per thumbnail would waste most of a page apiece.
#define THUMB_FILE_NAME "stone_wf_thumbs"
//! Room for a good number of faces plus the settings file's own overhead and rewrite headroom.
#define THUMB_FILE_SIZE (STONE_FACE_THUMB_BYTES * 24)

#define SCALE (3)

typedef struct {
  Uuid uuid;
  uint8_t pixels[];
} ThumbWrite;

// Runs on the system task: pfs_write erases and programs NOR flash, which is far too slow to sit
// inside an app switch.
static void prv_store(void *ctx) {
  ThumbWrite *write = ctx;
  SettingsFile file;

  if (settings_file_open(&file, THUMB_FILE_NAME, THUMB_FILE_SIZE) == S_SUCCESS) {
    settings_file_set(&file, &write->uuid, sizeof(Uuid), write->pixels, STONE_FACE_THUMB_BYTES);
    settings_file_close(&file);
  }
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
  if (settings_file_open(&file, THUMB_FILE_NAME, THUMB_FILE_SIZE) != S_SUCCESS) {
    return false;
  }

  const status_t rv =
      settings_file_get(&file, uuid, sizeof(Uuid), buffer, STONE_FACE_THUMB_BYTES);
  settings_file_close(&file);
  return rv == S_SUCCESS;
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
