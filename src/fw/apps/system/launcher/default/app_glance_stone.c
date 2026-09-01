/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// The Stone row's subtitle: which build is on the watch, without opening it.
//
// Modelled on the Watchfaces glance, which is the same shape -- a title, an
// icon, and one line of text that is fixed for the life of the glance. The
// build a watch is running cannot change while you are looking at the list, so
// there is nothing here to subscribe to and nothing to invalidate.

#include "app_glance_stone.h"

#ifdef CONFIG_STONE

#include "app_glance_structured.h"

#include "kernel/pbl_malloc.h"
#include "process_management/app_install_manager.h"
#include "system/passert.h"
#include "system/version.h"
#include "pbl/util/attributes.h"
#include "pbl/util/struct.h"

#include "stone_build_info.auto.h"

#include <string.h>

typedef struct LauncherAppGlanceStone {
  char title[APP_NAME_SIZE_BYTES];
  char subtitle[APP_NAME_SIZE_BYTES];
  KinoReel *icon;
} LauncherAppGlanceStone;

static KinoReel *prv_get_icon(LauncherAppGlanceStructured *structured_glance) {
  LauncherAppGlanceStone *stone_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  return NULL_SAFE_FIELD_ACCESS(stone_glance, icon, NULL);
}

static const char *prv_get_title(LauncherAppGlanceStructured *structured_glance) {
  LauncherAppGlanceStone *stone_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  return NULL_SAFE_FIELD_ACCESS(stone_glance, title, NULL);
}

static void prv_subtitle_dynamic_text_node_update(
    PBL_UNUSED GContext *ctx, PBL_UNUSED GTextNode *node, PBL_UNUSED const GRect *box,
    PBL_UNUSED const GTextNodeDrawConfig *config, PBL_UNUSED bool render, char *buffer,
    size_t buffer_size, void *user_data) {
  LauncherAppGlanceStructured *structured_glance = user_data;
  LauncherAppGlanceStone *stone_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  if (stone_glance) {
    strncpy(buffer, stone_glance->subtitle, buffer_size);
    buffer[buffer_size - 1] = '\0';
  }
}

static GTextNode *prv_create_subtitle_node(LauncherAppGlanceStructured *structured_glance) {
  return launcher_app_glance_structured_create_subtitle_text_node(
      structured_glance, prv_subtitle_dynamic_text_node_update);
}

static void prv_destructor(LauncherAppGlanceStructured *structured_glance) {
  LauncherAppGlanceStone *stone_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  if (stone_glance) {
    kino_reel_destroy(stone_glance->icon);
  }
  app_free(stone_glance);
}

//! The Stone label on a WIP build, the version string on a release one.
//!
//! Not both: the row has one line, and showing a version on a WIP build is how
//! the two get confused in the first place.
static const char *prv_build_text(void) {
#if STONE_CUSTOM
  if (STONE_BUILD[0] != '\0') {
    return STONE_BUILD;
  }
#endif
  return (strlen(TINTIN_METADATA.version_tag) >= 2) ? TINTIN_METADATA.version_tag
                                                    : TINTIN_METADATA.version_short;
}

static const LauncherAppGlanceStructuredImpl s_stone_structured_glance_impl = {
  .get_icon = prv_get_icon,
  .get_title = prv_get_title,
  .create_subtitle_node = prv_create_subtitle_node,
  .destructor = prv_destructor,
};

LauncherAppGlance *launcher_app_glance_stone_create(const AppMenuNode *node) {
  PBL_ASSERTN(node);

  LauncherAppGlanceStone *stone_glance = app_zalloc_check(sizeof(*stone_glance));

  strncpy(stone_glance->title, node->name, sizeof(stone_glance->title));
  stone_glance->title[sizeof(stone_glance->title) - 1] = '\0';

  strncpy(stone_glance->subtitle, prv_build_text(), sizeof(stone_glance->subtitle));
  stone_glance->subtitle[sizeof(stone_glance->subtitle) - 1] = '\0';

  stone_glance->icon = kino_reel_create_with_resource_system(node->app_num,
                                                             node->icon_resource_id);
  PBL_ASSERTN(stone_glance->icon);

  const bool should_consider_slices = false;
  LauncherAppGlanceStructured *structured_glance =
      launcher_app_glance_structured_create(&node->uuid, &s_stone_structured_glance_impl,
                                            should_consider_slices, stone_glance);
  PBL_ASSERTN(structured_glance);

  return &structured_glance->glance;
}

#endif  // CONFIG_STONE
