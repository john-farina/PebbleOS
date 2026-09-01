/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Settings > Stone: which build is actually on this watch.
//
// With several WIP builds in flight the version string alone is not enough to
// answer that. `git describe` records the nearest tag and commit but not the
// branch, and version_tag is 32 bytes with no room to carry one, so the branch
// and the upstream base come from the generated stone_build_info header.

#include "stone.h"

#include "window.h"

#include "applib/ui/menu_layer.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/i18n/i18n.h"
#include "system/version.h"

#include "stone_build_info.auto.h"

#include <stdio.h>
#include <string.h>

typedef enum {
  StoneMenuItemVersion = 0,
  StoneMenuItemPebbleOS,
  StoneMenuItemBranch,
  StoneMenuItemCommit,
  StoneMenuItemFirmware,
  StoneMenuItemSlot,
  StoneMenuItem_Count,
} StoneMenuItem;

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
  return StoneMenuItem_Count;
}

static void prv_draw_row_cb(SettingsCallbacks *context, GContext *ctx, const Layer *cell_layer,
                            uint16_t row, bool selected) {
  SettingsStoneData *data = (SettingsStoneData *)context;
  const char *title = NULL;
  // Subtitles here are build identifiers, never translated.
  const char *subtitle = NULL;

  switch ((StoneMenuItem)row) {
    case StoneMenuItemVersion:
      /// Settings > Stone: Stone's own version, independent of the PebbleOS release underneath.
      title = i18n_noop("Stone");
      subtitle = STONE_VERSION;
      break;
    case StoneMenuItemPebbleOS:
      /// Settings > Stone: the PebbleOS release this Stone build runs on top of.
      title = i18n_noop("PebbleOS");
      subtitle = STONE_BASE;
      break;
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
