/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Stone: which build is actually on this watch.
//
// Reached from the Stone app at the top of the app list rather than from
// Settings, but still built as a settings module: it is a list of rows with
// subtitles, which is exactly what this window already draws, and being a
// category is what lets the app push it with settings_menu_push().
//
// With several WIP builds in flight the version string alone is not enough to
// answer "which build is this?". `git describe` records the nearest tag and
// commit but not the branch, and version_tag is 32 bytes with no room to carry
// one, so the branch and the upstream base come from the generated
// stone_build_info header -- as do the two rows that lead the list on a WIP
// build, the orderable Stone label and one sentence on what changed in it.

#include "stone.h"

#include "window.h"

#include "applib/fonts/fonts.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text.h"
#include "applib/ui/menu_layer.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/i18n/i18n.h"
#include "system/version.h"

#include "stone_build_info.auto.h"

#include <stdio.h>
#include <string.h>

typedef enum {
  StoneMenuItemBuild = 0,
  StoneMenuItemSummary,
  StoneMenuItemBranch,
  StoneMenuItemCommit,
  StoneMenuItemUpstream,
  StoneMenuItemFirmware,
  StoneMenuItemSlot,
  StoneMenuItem_Count,
} StoneMenuItem;

//! The first row this build actually has. A release build is named by its
//! version string; a Stone label and a per-build summary would only compete
//! with it, so those two rows are absent rather than empty.
#if STONE_CUSTOM
#define STONE_FIRST_ROW (StoneMenuItemBuild)
#else
#define STONE_FIRST_ROW (StoneMenuItemBranch)
#endif

//! Extra height for the summary row, which is a sentence rather than an
//! identifier and so is the one row here that wraps.
#define SUMMARY_EXTRA_HEIGHT_PX (20)

typedef struct {
  SettingsCallbacks callbacks;
  char branch[48];
  char slot[12];
} SettingsStoneData;

static void prv_deinit_cb(SettingsCallbacks *context) {
  i18n_free_all(context);
  app_free(context);
}

static uint16_t prv_num_rows_cb(SettingsCallbacks *context) {
  return StoneMenuItem_Count - STONE_FIRST_ROW;
}

static int16_t prv_row_height_cb(SettingsCallbacks *context, uint16_t row, bool is_selected) {
  const bool is_summary = ((StoneMenuItem)(row + STONE_FIRST_ROW) == StoneMenuItemSummary);
  return menu_cell_basic_cell_height() + (is_summary ? SUMMARY_EXTRA_HEIGHT_PX : 0);
}

//! Draw the summary as a wrapped block under its own title, because a sentence
//! ellipsized to one line of a menu cell says nothing.
//!
//! The cell is temporarily shortened so menu_cell_basic_draw centres the title
//! in the top part of the row rather than in the whole of it -- the same idiom
//! the Bluetooth page uses for its heart-rate line.
static void prv_draw_summary_row(SettingsCallbacks *context, GContext *ctx,
                                 const Layer *cell_layer) {
  SettingsStoneData *data = (SettingsStoneData *)context;
  const int horizontal_margin = menu_cell_basic_horizontal_inset();
  GRect box = grect_inset(cell_layer->bounds, GEdgeInsets(0, horizontal_margin));
  box.origin.y += menu_cell_basic_cell_height() - SUMMARY_EXTRA_HEIGHT_PX;
  box.size.h = SUMMARY_EXTRA_HEIGHT_PX + menu_cell_basic_cell_height();

  graphics_draw_text(ctx, STONE_SUMMARY, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     box, GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  ((Layer *)cell_layer)->bounds.size.h -= SUMMARY_EXTRA_HEIGHT_PX;
  /// Stone: one sentence on what changed in this build.
  menu_cell_basic_draw(ctx, cell_layer, i18n_get(i18n_noop("Summary"), data), NULL, NULL);
  ((Layer *)cell_layer)->bounds.size.h += SUMMARY_EXTRA_HEIGHT_PX;
}

static void prv_draw_row_cb(SettingsCallbacks *context, GContext *ctx, const Layer *cell_layer,
                            uint16_t row, bool selected) {
  SettingsStoneData *data = (SettingsStoneData *)context;
  const char *title = NULL;
  // Subtitles here are build identifiers, never translated.
  const char *subtitle = NULL;

  switch ((StoneMenuItem)(row + STONE_FIRST_ROW)) {
    case StoneMenuItemBuild:
      /// Stone: the orderable name of this build, e.g. "navigation.7".
      title = i18n_noop("Build");
      subtitle = STONE_BUILD;
      break;
    case StoneMenuItemSummary:
      prv_draw_summary_row(context, ctx, cell_layer);
      return;
    case StoneMenuItemBranch:
      /// Settings > Stone: the branch this firmware was built from.
      title = i18n_noop("Branch");
      subtitle = data->branch;
      break;
    case StoneMenuItemCommit:
      /// Settings > Stone: the commit this firmware was built from.
      title = i18n_noop("Commit");
      subtitle = STONE_COMMIT;
      break;
    case StoneMenuItemUpstream:
      /// Settings > Stone: the upstream release this fork is based on.
      title = i18n_noop("Upstream");
      subtitle = STONE_BASE;
      break;
    case StoneMenuItemFirmware:
      /// Settings > Stone: the full firmware version string.
      title = i18n_noop("Firmware");
      subtitle = (strlen(TINTIN_METADATA.version_tag) >= 2) ? TINTIN_METADATA.version_tag
                                                            : TINTIN_METADATA.version_short;
      break;
    case StoneMenuItemSlot:
      /// Settings > Stone: which firmware slot is running.
      title = i18n_noop("Slot");
      subtitle = data->slot;
      break;
    case StoneMenuItem_Count:
      return;
  }

  menu_cell_basic_draw(ctx, cell_layer, i18n_get(title, data), subtitle, NULL);
}

static Window *prv_init(void) {
  SettingsStoneData *data = app_malloc_check(sizeof(SettingsStoneData));
  *data = (SettingsStoneData){};

  sniprintf(data->branch, sizeof(data->branch), "%s%s", STONE_BRANCH,
            STONE_DIRTY ? " (dirty)" : "");

  // Which slot is running decides where the next update lands, so it is worth
  // showing: an install writes into the *other* one.
  if (TINTIN_METADATA.is_dual_slot) {
    sniprintf(data->slot, sizeof(data->slot), "%d", TINTIN_METADATA.is_slot_0 ? 0 : 1);
  } else {
    sniprintf(data->slot, sizeof(data->slot), "single");
  }

  data->callbacks = (SettingsCallbacks){
    .deinit = prv_deinit_cb,
    .draw_row = prv_draw_row_cb,
    .num_rows = prv_num_rows_cb,
    .row_height = prv_row_height_cb,
  };

  return settings_window_create(SettingsMenuItemStone, &data->callbacks);
}

const SettingsModuleMetadata *settings_stone_get_info(void) {
  static const SettingsModuleMetadata s_module_info = {
    .name = i18n_noop("Stone"),
    .init = prv_init,
  };

  return &s_module_info;
}
