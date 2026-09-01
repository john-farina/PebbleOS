/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Settings > Apps: the apps that were taken out of the main list.
//
// John wanted one list holding only what he thinks of as apps, and "a setting app that will hold
// all the other stuff I don't want in the list". This is that second place. It is deliberately
// not a second copy of the rule about which is which: both this list and the launcher's filter
// call stone_app_list_is_app(), from opposite sides, so an app can never be in both or neither.
//
// Nothing here is hidden in the upstream sense. These apps still launch, still appear in Quick
// Launch, and still work exactly as before; they have only moved out of the way.

#include "stone_apps.h"

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

#include "menu.h"
#include "window.h"

#include "applib/ui/menu_layer.h"
#include "apps/system/launcher/default/stone_app_list.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/i18n/i18n.h"
#include "process_management/app_install_manager.h"
#include "process_management/app_manager.h"
#include "process_management/app_menu_data_source.h"

typedef struct {
  SettingsCallbacks callbacks;
  AppMenuDataSource data_source;
} SettingsStoneAppsData;

// The exact complement of the launcher's filter: same visibility rules, opposite answer to the
// one question that sorts an app into a list.
static bool prv_app_filter_callback(AppMenuDataSource *source, AppInstallEntry *entry) {
  if (app_install_entry_is_watchface(entry) || app_install_entry_is_hidden(entry)) {
    return false;
  }
  return !stone_app_list_is_app(entry);
}

static void prv_data_changed(void *context) {
  settings_menu_reload_data(SettingsMenuItemStoneApps);
}

static void prv_deinit_cb(SettingsCallbacks *context) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;
  app_menu_data_source_deinit(&data->data_source);
  i18n_free_all(data);
  app_free(data);
}

static uint16_t prv_num_rows_cb(SettingsCallbacks *context) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;
  return app_menu_data_source_get_count(&data->data_source);
}

static void prv_draw_row_cb(SettingsCallbacks *context, GContext *ctx, const Layer *cell_layer,
                            uint16_t row, bool selected) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;
  const AppMenuNode *node = app_menu_data_source_get_node_at_index(&data->data_source, row);
  if (!node) {
    return;
  }
  // node->name is the app's own name, already localized where it is localizable at all, so it is
  // drawn as-is rather than passed through i18n_get.
  menu_cell_basic_draw(ctx, cell_layer, node->name, NULL, NULL);
}

static void prv_select_click_cb(SettingsCallbacks *context, uint16_t row) {
  SettingsStoneAppsData *data = (SettingsStoneAppsData *)context;
  const AppMenuNode *node = app_menu_data_source_get_node_at_index(&data->data_source, row);
  if (!node) {
    return;
  }
  app_manager_put_launch_app_event(&(AppLaunchEventConfig) {
    .id = node->install_id,
    .common.reason = APP_LAUNCH_USER,
    .common.button = BUTTON_ID_SELECT,
  });
}

static Window *prv_init(void) {
  SettingsStoneAppsData *data = app_malloc_check(sizeof(SettingsStoneAppsData));
  *data = (SettingsStoneAppsData){};

  data->callbacks = (SettingsCallbacks){
    .deinit = prv_deinit_cb,
    .draw_row = prv_draw_row_cb,
    .select_click = prv_select_click_cb,
    .num_rows = prv_num_rows_cb,
  };

  app_menu_data_source_init(&data->data_source, &(AppMenuDataSourceCallbacks) {
    .changed = prv_data_changed,
    .filter = prv_app_filter_callback,
  }, data);

  return settings_window_create(SettingsMenuItemStoneApps, &data->callbacks);
}

const SettingsModuleMetadata *settings_stone_apps_get_info(void) {
  static const SettingsModuleMetadata s_module_info = {
    /// Settings > Apps: the apps kept out of the main app list.
    .name = i18n_noop("Apps"),
    .init = prv_init,
  };

  return &s_module_info;
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
