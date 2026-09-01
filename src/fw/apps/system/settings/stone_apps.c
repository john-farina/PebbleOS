/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Settings > Apps: the order of the main app list, and the apps kept out of it.
//
// Two things John asked for, on one page because they are the same subject: "I wanna be able to
// control the order of the list in a new settings page called apps or something", and "a setting
// app that will hold all the other stuff I don't want in the list".
//
// The page is one flat list -- the settings window has no section headers -- laid out as the main
// apps, an unselectable heading, then the rest. The heading is skipped by
// prv_selection_will_change_cb, so it reads as a divider rather than behaving like a row.
//
// Which apps go where is not decided here. Both halves call stone_app_list_is_app(), the same
// predicate the launcher's filter uses, so this page and the launcher cannot disagree.

#include "stone_apps.h"

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

#include "menu.h"
#include "window.h"

#include "applib/ui/menu_layer.h"
#include "applib/ui/stone_haptics.h"
#include "apps/system/launcher/default/stone_app_list.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/i18n/i18n.h"
#include "pbl/services/process_management/app_order_storage.h"
#include "pbl/services/system_task.h"
#include "process_management/app_install_manager.h"
#include "process_management/app_manager.h"
#include "process_management/app_menu_data_source.h"

#include <stdint.h>

#define GRAB_NONE (-1)

typedef struct {
  SettingsCallbacks callbacks;
  AppMenuDataSource data_source;
  //! The main list in the order being edited. Held separately from the data source because the
  //! source is only re-sorted when it reloads, and a reorder has to be visible immediately.
  AppInstallId *order;
  uint16_t main_count;
  uint16_t other_count;
  //! Row of the picked-up app, or GRAB_NONE. While a row is grabbed the selection still moves,
  //! and the app moves with it -- that is what makes it read as dragging.
  int16_t grabbed;
} SettingsStoneAppsData;

// The order file is written from KernelBackground (write_uuid_list_to_file asserts it) and this
// runs on the app task, so the payload crosses tasks and cannot live on the app heap.
typedef struct {
  uint8_t count;
  Uuid uuids[];
} StoneOrderWrite;

static void prv_write_order_cb(void *ctx) {
  StoneOrderWrite *write = ctx;
  write_uuid_list_to_file(write->uuids, write->count);
  kernel_free(write);
}

// Every app in the main list is written, Settings included. Left out of the file, Settings is
// special-cased to AppMenuStorageOrder_SettingsDefaultOrder and floats to the top of the launcher
// no matter where it was put here, which would make the saved order quietly wrong.
static void prv_persist_order(SettingsStoneAppsData *data) {
  if (data->main_count == 0) {
    return;
  }

  // AppMenuOrderStorage.list_length is a uint8_t, so the file cannot describe more than 255
  // apps whatever we do here. Clamping keeps a hypothetical overflow from writing a length that
  // disagrees with the payload, which app_order_read_order() would reject as corrupt.
  const uint16_t count = (data->main_count > UINT8_MAX) ? UINT8_MAX : data->main_count;

  const size_t size = sizeof(StoneOrderWrite) + (count * sizeof(Uuid));
  StoneOrderWrite *write = kernel_malloc(size);
  if (!write) {
    return;
  }

  write->count = (uint8_t)count;
  for (uint16_t i = 0; i < count; i++) {
    app_install_get_uuid_for_install_id(data->order[i], &write->uuids[i]);
  }

  if (!system_task_add_callback(prv_write_order_cb, write)) {
    kernel_free(write);
  }
}

//////////////////
// Row addressing

static bool prv_entry_is_main(AppInstallId install_id) {
  AppInstallEntry entry;
  if (!app_install_get_entry_for_install_id(install_id, &entry)) {
    return false;
  }
  return stone_app_list_is_app(&entry);
}

// Rows are [main apps][heading][other apps]. The heading only exists when there is something
// under it, so an empty second half leaves a plain reorderable list rather than a dangling title.
static bool prv_has_heading(const SettingsStoneAppsData *data) {
  return data->other_count > 0;
}

static uint16_t prv_heading_row(const SettingsStoneAppsData *data) {
  return data->main_count;
}

static bool prv_is_main_row(const SettingsStoneAppsData *data, uint16_t row) {
  return row < data->main_count;
}

//! @return the node for a row in the "other" half, or NULL if the row is not one
static const AppMenuNode *prv_other_node(SettingsStoneAppsData *data, uint16_t row) {
  if (!prv_has_heading(data) || row <= prv_heading_row(data)) {
    return NULL;
  }
  const uint16_t nth = row - prv_heading_row(data) - 1;

  uint16_t seen = 0;
  const uint16_t count = app_menu_data_source_get_count(&data->data_source);
  for (uint16_t i = 0; i < count; i++) {
    AppMenuNode *node = app_menu_data_source_get_node_at_index(&data->data_source, i);
    if (!node || prv_entry_is_main(node->install_id)) {
      continue;
    }
    if (seen++ == nth) {
      return node;
    }
  }
  return NULL;
}

/////////////////////////////
// AppMenuDataSource plumbing

static bool prv_app_filter_callback(AppMenuDataSource *source, AppInstallEntry *entry) {
  // Same visibility rules as the launcher; the split between the two halves happens per row.
  return !app_install_entry_is_watchface(entry) && !app_install_entry_is_hidden(entry);
}

static void prv_rebuild(SettingsStoneAppsData *data) {
  const uint16_t count = app_menu_data_source_get_count(&data->data_source);

  if (data->order) {
    app_free(data->order);
    data->order = NULL;
  }
  data->main_count = 0;
  data->other_count = 0;
  data->grabbed = GRAB_NONE;

  if (count == 0) {
    return;
  }

  // Sized for the worst case rather than counted twice: the list is a few dozen entries at most,
  // and a second pass would have to agree exactly with the first about what is main.
  data->order = app_malloc(count * sizeof(AppInstallId));
  if (!data->order) {
    return;
  }

  for (uint16_t i = 0; i < count; i++) {
    const AppMenuNode *node = app_menu_data_source_get_node_at_index(&data->data_source, i);
    if (!node) {
      continue;
    }
    if (prv_entry_is_main(node->install_id)) {
      data->order[data->main_count++] = node->install_id;
    } else {
      data->other_count++;
    }
  }
}

static void prv_data_changed(void *context) {
  SettingsStoneAppsData *data = context;
  prv_rebuild(data);
  settings_menu_reload_data(SettingsMenuItemStoneApps);
}

/////////////////////
// Settings callbacks

static void prv_deinit_cb(SettingsCallbacks *context) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;
  app_menu_data_source_deinit(&data->data_source);
  if (data->order) {
    app_free(data->order);
  }
  i18n_free_all(data);
  app_free(data);
}

static uint16_t prv_num_rows_cb(SettingsCallbacks *context) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;
  return data->main_count + (prv_has_heading(data) ? 1 + data->other_count : 0);
}

static const char *prv_name_for_install_id(AppInstallId install_id) {
  static AppInstallEntry s_entry;
  if (!app_install_get_entry_for_install_id(install_id, &s_entry)) {
    return "";
  }
  return s_entry.name;
}

static void prv_draw_row_cb(SettingsCallbacks *context, GContext *ctx, const Layer *cell_layer,
                            uint16_t row, bool selected) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;

  if (prv_is_main_row(data, row)) {
    /// Settings > Apps: shown under an app that has been picked up to be moved.
    const char *subtitle = (data->grabbed == (int16_t)row) ? i18n_get("Moving", data) : NULL;
    menu_cell_basic_draw(ctx, cell_layer, prv_name_for_install_id(data->order[row]), subtitle,
                         NULL);
    return;
  }

  if (prv_has_heading(data) && (row == prv_heading_row(data))) {
    /// Settings > Apps: heading over the apps that are not in the main app list.
    menu_cell_basic_draw(ctx, cell_layer, i18n_get("Not in the list", data), NULL, NULL);
    return;
  }

  const AppMenuNode *node = prv_other_node(data, row);
  if (node) {
    menu_cell_basic_draw(ctx, cell_layer, node->name, NULL, NULL);
  }
}

// The hook that makes dragging possible: the selection is about to move, so move the grabbed app
// with it instead of leaving it behind. Also the only way to make the heading unselectable.
static void prv_selection_will_change_cb(SettingsCallbacks *context, uint16_t *new_row,
                                         uint16_t old_row) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;
  const uint16_t target = *new_row;

  if (data->grabbed != GRAB_NONE) {
    // A grabbed app stays in its own half of the list; there is nowhere meaningful for it to go
    // in the other one, and letting it leave would make the heading a place you could drop things.
    if (!prv_is_main_row(data, target) || (target == old_row)) {
      // Refused: the grabbed app stays in its own half of the list.
      stone_haptics_play(StoneHaptic_Bump);
      *new_row = old_row;
      return;
    }
    const AppInstallId moved = data->order[old_row];
    data->order[old_row] = data->order[target];
    data->order[target] = moved;
    data->grabbed = (int16_t)target;
    // Each row the app travels past is a discrete event worth feeling.
    stone_haptics_play(StoneHaptic_Tick);
    return;
  }

  if (prv_has_heading(data) && (target == prv_heading_row(data))) {
    // Step over the heading in whichever direction the selection was already travelling.
    *new_row = (target > old_row) ? (target + 1) : (target - 1);
  }
}

static void prv_select_click_cb(SettingsCallbacks *context, uint16_t row) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;

  if (prv_is_main_row(data, row)) {
    if (data->grabbed == (int16_t)row) {
      data->grabbed = GRAB_NONE;
      prv_persist_order(data);
      stone_haptics_play(StoneHaptic_Select);
    } else {
      data->grabbed = (int16_t)row;
      stone_haptics_play(StoneHaptic_Enter);
    }
    settings_menu_reload_data(SettingsMenuItemStoneApps);
    return;
  }

  const AppMenuNode *node = prv_other_node(data, row);
  if (node) {
    app_manager_put_launch_app_event(&(AppLaunchEventConfig) {
      .id = node->install_id,
      .common.reason = APP_LAUNCH_USER,
      .common.button = BUTTON_ID_SELECT,
    });
  }
}

static Window *prv_init(void) {
  SettingsStoneAppsData *data = app_malloc_check(sizeof(SettingsStoneAppsData));
  *data = (SettingsStoneAppsData){
    .grabbed = GRAB_NONE,
  };

  data->callbacks = (SettingsCallbacks){
    .deinit = prv_deinit_cb,
    .draw_row = prv_draw_row_cb,
    .select_click = prv_select_click_cb,
    .selection_will_change = prv_selection_will_change_cb,
    .num_rows = prv_num_rows_cb,
  };

  app_menu_data_source_init(&data->data_source, &(AppMenuDataSourceCallbacks) {
    .changed = prv_data_changed,
    .filter = prv_app_filter_callback,
  }, data);

  // The source loads lazily on first use, so ask now: the row count has to be right before the
  // window draws.
  prv_rebuild(data);

  return settings_window_create(SettingsMenuItemStoneApps, &data->callbacks);
}

const SettingsModuleMetadata *settings_stone_apps_get_info(void) {
  static const SettingsModuleMetadata s_module_info = {
    /// Settings > Apps: reorder the app list and reach the apps kept out of it.
    .name = i18n_noop("Apps"),
    .init = prv_init,
  };

  return &s_module_info;
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
