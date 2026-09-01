/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Which installed apps belong in the main app list.
//
// John's list is "Music, Settings, alarms, notifications, weather, health, workout, watchfaces,
// timeline, and all other third party apps automatically". That is very nearly what the launcher
// already showed, so this is a deny-list of the handful left over rather than an allow-list of
// the ones to keep -- an allow-list would have to be edited every time upstream adds an app, and
// would silently swallow third-party apps, which are the ones that must appear automatically.

#include "stone_app_list.h"

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

#include "apps/system_app_ids.h"

bool stone_app_list_is_app(const AppInstallEntry *entry) {
  switch (entry->install_id) {
    case APP_ID_GOLF:
    case APP_ID_SEND_TEXT:
    case APP_ID_REMINDERS:
    case APP_ID_SPORTS:
      return false;
    default:
      return true;
  }
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
