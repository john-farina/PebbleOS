/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "process_management/app_install_manager.h"

#include <stdbool.h>

// Not built for the SDK shell: its registry has four apps and none of the ones sorted out
// below, so the APP_ID_* constants this needs do not exist there.
#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

//! Whether an installed app belongs in the main app list rather than in Settings > Apps.
//!
//! The launcher's filter and the Settings > Apps list are the same question asked from opposite
//! sides, so they ask it in one place: whatever this excludes is exactly what Settings shows, and
//! the two can never drift into disagreeing about where an app lives.
//!
//! This says nothing about whether an app is *visible* -- watchfaces and hidden apps are still
//! the launcher's own business. It only sorts the visible ones into the two lists.
//!
//! @param entry the installed app to classify
//! @return true if it belongs in the main list
bool stone_app_list_is_app(const AppInstallEntry *entry);

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
