/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "app_glance.h"

#include "process_management/app_menu_data_source.h"

#ifdef CONFIG_STONE

//! The Stone row in the app list, showing which build is on the watch without
//! having to open anything.
//!
//! On a WIP build that is the Stone label -- `navigation.7` -- which orders at
//! a glance in a way a commit hash does not. On a release build there is no
//! label and the version string is the answer, so that is what it shows.
LauncherAppGlance *launcher_app_glance_stone_create(const AppMenuNode *node);

#endif  // CONFIG_STONE
