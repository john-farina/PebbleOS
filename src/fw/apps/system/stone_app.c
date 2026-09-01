/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#include "stone_app.h"

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

#include "applib/app.h"
#include "apps/system/settings/menu.h"
#include "pbl/services/i18n/i18n.h"
#include "resource/resource_ids.auto.h"

static void prv_main(void) {
  // Straight into the list: an app whose only content is one window has no
  // reason to show anything before it.
  settings_menu_push(SettingsMenuItemStone);
  app_event_loop();
}

const PebbleProcessMd *stone_app_get_app_info(void) {
  static const PebbleProcessMdSystem s_info = {
    .common = {
      .main_func = prv_main,
      // UUID: 3b7c1d90-5a24-4e6f-8b03-9d41c7e2f085
      .uuid = {0x3b, 0x7c, 0x1d, 0x90, 0x5a, 0x24, 0x4e, 0x6f,
               0x8b, 0x03, 0x9d, 0x41, 0xc7, 0xe2, 0xf0, 0x85},
    },
    // Not translated: it is the name of this fork, not a word.
    .name = "Stone",
    .icon_resource_id = RESOURCE_ID_SETTINGS_TINY,
  };
  return (const PebbleProcessMd *)&s_info;
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
