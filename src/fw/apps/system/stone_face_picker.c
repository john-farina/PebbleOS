/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// The watchface picker.
//
// John: "when I long press on the main watch face it shrinks each watch face and gives me a
// visible list of each watch face I have and I can scroll right or left in a sticky list to see
// each one and click on one when I wanna keep it there (just like Apple Watch)". Then, on the
// entry gesture: "lets make it hold on the watchface and then u can swipe and then u can press a
// watchface and then it goes back to normal".
//
// So: hold to enter, swipe (or Up/Down) to page, tap (or Select) to keep, Back to leave the
// running face alone. Paging is one face per step with a snap, not free scroll -- which is what
// watchOS does and what John asked for by calling it sticky.
//
// What this deliberately does NOT do is show a live miniature of each face. There is one app
// task and one app framebuffer, so only the *running* face can be captured; applib has no bitmap
// scaler (graphics_draw_bitmap_in_rect clips and tiles, it never scales); and a single
// full-screen 8-bit bitmap is about a third of a system app's whole heap. Thumbnails of every
// installed face would mean launching each one in turn. Each face is shown by its icon and name
// instead. That is a hardware limit, not a shortcut -- see docs/stone/features/.

#include "stone_face_picker.h"

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

#include "applib/app.h"
#include "applib/graphics/graphics.h"
#include "applib/fonts/fonts.h"
#include "applib/graphics/text.h"
#include "applib/ui/animation.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/property_animation.h"
#include "applib/ui/stone_haptics.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"
#include "process_management/app_install_manager.h"
#include "process_management/app_manager.h"
#include "pbl/services/i18n/i18n.h"
#include "process_management/app_menu_data_source.h"
#include "process_state/app_state/app_state.h"
#include "resource/resource_ids.auto.h"
#include "shell/normal/watchface.h"
#include "stone_face_thumb.h"
#include "shell/prefs.h"
#include "system/passert.h"

#ifdef CONFIG_TOUCH
#include "applib/touch_service.h"
#include "applib/ui/recognizer/recognizer.h"
#include "applib/ui/recognizer/swipe.h"
#include "applib/ui/recognizer/tap.h"
#endif

//! One face per step, snapped -- the "sticky" John asked for. Matched to the weather app's
//! horizontal slide so a page change feels like the rest of the firmware.
#define PAGE_SLIDE_MS (110)

#define MARKER_DIAMETER (6)
#define MARKER_GAP (4)
#define MARKER_TOP (14)
//! Past this many faces the markers stop meaning anything and just become a grey smear, so they
//! are dropped and the position is carried by the name alone.
#define MARKER_MAX (12)

typedef struct {
  Window window;
  Layer content_layer;
  AppMenuDataSource data_source;

  uint16_t index;
  AppInstallId active_id;

  //! Horizontal offset of the page content, animated to zero on each step so a page arrives
  //! rather than jumping.
  int16_t slide_x;
  Animation *slide_animation;

  //! The miniature of the face on the current page, if one has ever been captured. Loaded on
  //! demand for the visible page only -- holding every face's thumbnail would be pointless when
  //! exactly one is on screen.
  GBitmap *thumb;
  AppInstallId thumb_id;
} StoneFacePickerData;

static StoneFacePickerData *prv_data(void) {
  return app_state_get_user_data();
}

static uint16_t prv_count(StoneFacePickerData *data) {
  return app_menu_data_source_get_count(&data->data_source);
}

static AppMenuNode *prv_node(StoneFacePickerData *data) {
  if (prv_count(data) == 0) {
    return NULL;
  }
  return app_menu_data_source_get_node_at_index(&data->data_source, data->index);
}

//////////////
// Thumbnails

// Load the miniature for the current page, if there is one. A face that has never been worn has
// never rendered, so it has no thumbnail and never will until it is worn once; that is the case
// the icon fallback exists for, and it is not an error.
static void prv_load_thumb(StoneFacePickerData *data) {
  AppMenuNode *node = prv_node(data);
  if (!node || (node->install_id == data->thumb_id)) {
    return;
  }

  if (data->thumb) {
    gbitmap_destroy(data->thumb);
    data->thumb = NULL;
  }
  data->thumb_id = node->install_id;

  GBitmap *thumb = gbitmap_create_blank(GSize(STONE_FACE_THUMB_W, STONE_FACE_THUMB_H),
                                        GBITMAP_NATIVE_FORMAT);
  if (!thumb) {
    return;
  }

  // Rows are written contiguously by the capture, so copy row by row rather than assuming the
  // blank bitmap's stride matches the thumbnail's width.
  bool ok = false;
  uint8_t *raw = app_malloc(STONE_FACE_THUMB_BYTES);
  if (raw && stone_face_thumb_load(&node->uuid, raw)) {
    for (int16_t y = 0; y < STONE_FACE_THUMB_H; y++) {
      const GBitmapDataRowInfo row = gbitmap_get_data_row_info(thumb, (uint16_t)y);
      memcpy(&row.data[row.min_x], &raw[y * STONE_FACE_THUMB_W], STONE_FACE_THUMB_W);
    }
    ok = true;
  }
  if (raw) {
    app_free(raw);
  }

  if (ok) {
    data->thumb = thumb;
  } else {
    gbitmap_destroy(thumb);
  }
}

/////////////
// Rendering

static void prv_draw_markers(GContext *ctx, const GRect *bounds, uint16_t count, uint16_t index) {
  if ((count < 2) || (count > MARKER_MAX)) {
    return;
  }
  const int16_t step = MARKER_DIAMETER + MARKER_GAP;
  const int16_t total = (int16_t)(count * step) - MARKER_GAP;
  int16_t x = bounds->origin.x + ((bounds->size.w - total) / 2);

  for (uint16_t i = 0; i < count; i++) {
    const GRect dot = GRect(x, MARKER_TOP, MARKER_DIAMETER, MARKER_DIAMETER);
    // Filled for where you are, hollow for where you are not: readable at this size in a way
    // that a colour difference alone is not.
    if (i == index) {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, &dot);
    } else {
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_draw_rect(ctx, &dot);
    }
    x += step;
  }
}

static void prv_content_update_proc(Layer *layer, GContext *ctx) {
  StoneFacePickerData *data = prv_data();
  const GRect bounds = layer->bounds;

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, &bounds);

  const uint16_t count = prv_count(data);
  prv_draw_markers(ctx, &bounds, count, data->index);

  AppMenuNode *node = prv_node(data);
  if (!node) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "No watchfaces", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(0, bounds.size.h / 2 - 16, bounds.size.w, 32),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }

  // The page slides as a whole -- icon and name together -- so a step reads as one object
  // moving rather than two things animating side by side.
  const int16_t dx = data->slide_x;

  // The real miniature when the face has been worn, its icon when it has not.
  GBitmap *art = data->thumb ?: app_menu_data_source_get_node_icon(&data->data_source, node);
  if (art) {
    const GSize size = art->bounds.size;
    const GRect art_frame = GRect(dx + ((bounds.size.w - size.w) / 2),
                                  (bounds.size.h / 2) - size.h, size.w, size.h);
    graphics_context_set_compositing_mode(ctx, data->thumb ? GCompOpAssign : GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, art, &art_frame);
  }

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, node->name, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(dx, (bounds.size.h / 2) + 6, bounds.size.w, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  if (node->install_id == data->active_id) {
    /// Watchface picker: marks the face currently being worn.
    graphics_draw_text(ctx, i18n_get("Current", data),
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(dx, (bounds.size.h / 2) + 36, bounds.size.w, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

////////////
// Paging

static void prv_slide_setter(void *subject, int16_t value) {
  StoneFacePickerData *data = subject;
  data->slide_x = value;
  layer_mark_dirty(&data->content_layer);
}

static int16_t prv_slide_getter(void *subject) {
  return ((StoneFacePickerData *)subject)->slide_x;
}

static const PropertyAnimationImplementation s_slide_impl = {
  .base = {
    .update = (AnimationUpdateImplementation)property_animation_update_int16,
  },
  .accessors = {
    .setter = { .int16 = prv_slide_setter },
    .getter = { .int16 = prv_slide_getter },
  },
};

static void prv_step(int delta) {
  StoneFacePickerData *data = prv_data();
  const uint16_t count = prv_count(data);
  if (count < 2) {
    return;
  }

  stone_haptics_play(StoneHaptic_Tick);

  // Wrap, so paging never dead-ends on a list you are cycling through.
  if (delta < 0) {
    data->index = (data->index == 0) ? (count - 1) : (data->index - 1);
  } else {
    data->index = (data->index + 1) % count;
  }

  // The incoming page starts off-screen on the side it is arriving from and is animated home.
  if (data->slide_animation) {
    animation_unschedule(data->slide_animation);
    data->slide_animation = NULL;
  }
  int16_t from = (delta < 0) ? -data->content_layer.bounds.size.w
                             : data->content_layer.bounds.size.w;
  int16_t to = 0;
  PropertyAnimation *prop = property_animation_create(&s_slide_impl, data, &from, &to);
  if (prop) {
    Animation *animation = property_animation_get_animation(prop);
    animation_set_duration(animation, PAGE_SLIDE_MS);
    data->slide_animation = animation;
    animation_schedule(animation);
  } else {
    // No animation is a worse page change, not a broken one.
    data->slide_x = 0;
  }

  prv_load_thumb(data);
  layer_mark_dirty(&data->content_layer);
}

// Committing goes through a launch rather than writing the pref directly. app_manager sets the
// default as a side effect of a *successful* launch, and that indirection is deliberate
// upstream: writing the pref for a face whose fetch then fails would strand the wearer on a
// watchface that is not there.
static void prv_choose(void) {
  StoneFacePickerData *data = prv_data();
  AppMenuNode *node = prv_node(data);
  if (!node) {
    return;
  }
  stone_haptics_play(StoneHaptic_Select);
  app_manager_put_launch_app_event(&(AppLaunchEventConfig) {
    .id = node->install_id,
    .common.reason = APP_LAUNCH_USER,
    .common.button = BUTTON_ID_SELECT,
  });
}

//////////////////
// Button handling

static void prv_up_click(ClickRecognizerRef recognizer, void *context) {
  prv_step(-1);
}

static void prv_down_click(ClickRecognizerRef recognizer, void *context) {
  prv_step(1);
}

static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  prv_choose();
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  // Back is left alone: with no handler the window stack pops it, which exits the picker and
  // leaves the running face exactly as it was. That is the "leave without changing anything"
  // half of the interaction, and it costs nothing.
}

/////////////////
// Touch handling

#ifdef CONFIG_TOUCH
static void prv_swipe_event(const Recognizer *recognizer, RecognizerEvent event) {
  if (event != RecognizerEvent_Completed) {
    return;
  }
  // Content follows the finger: swiping left brings the next face in from the right.
  prv_step((swipe_recognizer_get_direction(recognizer) == SwipeDirection_Left) ? 1 : -1);
}

static void prv_tap_event(const Recognizer *recognizer, RecognizerEvent event) {
  if (event == RecognizerEvent_Completed) {
    prv_choose();
  }
}
#endif  // CONFIG_TOUCH

//////////////////
// Window handling

static void prv_window_load(Window *window) {
  StoneFacePickerData *data = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);

  prv_load_thumb(data);
  layer_init(&data->content_layer, &root->bounds);
  layer_set_update_proc(&data->content_layer, prv_content_update_proc);
  layer_add_child(root, &data->content_layer);
}

static void prv_window_unload(Window *window) {
  StoneFacePickerData *data = window_get_user_data(window);
  if (data->slide_animation) {
    animation_unschedule(data->slide_animation);
    data->slide_animation = NULL;
  }
  if (data->thumb) {
    gbitmap_destroy(data->thumb);
    data->thumb = NULL;
  }
  layer_deinit(&data->content_layer);
}

static bool prv_watchface_filter(AppMenuDataSource *source, AppInstallEntry *entry) {
  return app_install_entry_is_watchface(entry) && !app_install_entry_is_hidden(entry);
}

static void prv_data_changed(void *context) {
  StoneFacePickerData *data = context;
  const uint16_t count = prv_count(data);
  if (data->index >= count) {
    data->index = (count == 0) ? 0 : (count - 1);
  }
  layer_mark_dirty(&data->content_layer);
}

static void prv_main(void) {
  StoneFacePickerData *data = app_zalloc_check(sizeof(*data));
  app_state_set_user_data(data);

  app_menu_data_source_init(&data->data_source, &(AppMenuDataSourceCallbacks) {
    .changed = prv_data_changed,
    .filter = prv_watchface_filter,
  }, data);
  app_menu_data_source_enable_icons(&data->data_source,
                                    RESOURCE_ID_MENU_LAYER_GENERIC_WATCHFACE_ICON);

  // Open on the face being worn, so the picker starts where the wearer already is.
  data->thumb_id = INSTALL_ID_INVALID;
  data->active_id = watchface_get_default_install_id();
  data->index = app_menu_data_source_get_index_of_app_with_install_id(&data->data_source,
                                                                     data->active_id);
  if (data->index >= prv_count(data)) {
    // The worn face is not in the list (never fetched, or hidden): start at the beginning
    // rather than at an index that does not exist.
    data->index = 0;
  }

  Window *window = &data->window;
  window_init(window, WINDOW_NAME("Watchface Picker"));
  window_set_user_data(window, data);
  window_set_background_color(window, GColorBlack);
  window_set_click_config_provider(window, prv_click_config_provider);
  window_set_window_handlers(window, &(WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });

#ifdef CONFIG_TOUCH
  // Opt out of the touch-nav bridge: without this a horizontal swipe is turned into a Back or
  // Select press before these recognizers ever see it, and the picker would exit instead of
  // paging.
  window_set_touch_bridge_disabled(window, true);

  static RECOGNIZER_STATIC_STORAGE(s_swipe_storage, SWIPE_RECOGNIZER_STATIC_SIZE);
  static RECOGNIZER_STATIC_STORAGE(s_tap_storage, TAP_RECOGNIZER_STATIC_SIZE);
  Recognizer *swipe = swipe_recognizer_init_static(s_swipe_storage, prv_swipe_event, data,
                                                   SwipeDirection_Left | SwipeDirection_Right);
  Recognizer *tap = tap_recognizer_init_static(s_tap_storage, prv_tap_event, data);
  if (swipe) {
    window_attach_recognizer(window, swipe);
  }
  if (tap) {
    window_attach_recognizer(window, tap);
  }
#endif

  app_window_stack_push(window, true /* animated */);
  app_event_loop();

  app_menu_data_source_deinit(&data->data_source);
  i18n_free_all(data);
}

const PebbleProcessMd *stone_face_picker_get_app_info(void) {
  static const PebbleProcessMdSystem s_info = {
    .common = {
      .main_func = prv_main,
      // UUID: 5f0e3a41-6d2c-4a8b-9c17-2f6d8e0b4a93
      .uuid = {0x5f, 0x0e, 0x3a, 0x41, 0x6d, 0x2c, 0x4a, 0x8b,
               0x9c, 0x17, 0x2f, 0x6d, 0x8e, 0x0b, 0x4a, 0x93},
      // Reachable by holding the watchface and assignable in Quick Launch, but not something
      // that belongs in the app list next to Music.
      .visibility = ProcessVisibilityQuickLaunch,
    },
    .name = "Watchfaces",
  };
  return (const PebbleProcessMd *)&s_info;
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
