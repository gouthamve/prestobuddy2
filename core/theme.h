// Colours, fonts and small LVGL helpers shared by every screen.
#pragma once
#include <string.h>

#include "gen/assets.h"
#include "lvgl.h"

namespace theme {

constexpr uint32_t kBg = 0x151B33;
constexpr uint32_t kBgSpot = 0x1D2545;   // soft spotlight behind the mascot
constexpr uint32_t kCard = 0x232C50;
constexpr uint32_t kCardHi = 0x303B69;
constexpr uint32_t kText = 0xF4F6FF;
constexpr uint32_t kMuted = 0x9AA6D1;
constexpr uint32_t kGold = 0xFFC94A;
constexpr uint32_t kFocus = 0xFF6B6B;
constexpr uint32_t kBreak = 0x5BD68B;
constexpr uint32_t kTeal = 0x4FD1C5;
constexpr uint32_t kAmber = 0xFFB020;
constexpr uint32_t kDanger = 0xFF5D73;
constexpr uint32_t kInk = 0x151B33;

constexpr int kDockH = 72;
constexpr int kPageH = 480 - kDockH;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

// A plain box with no theme styling, padding or scrolling.
inline lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    return o;
}

inline lv_obj_t* card(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color = kCard, int radius = 18) {
    lv_obj_t* o = box(parent, x, y, w, h);
    lv_obj_set_style_bg_color(o, c(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

inline lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    lv_label_set_text(l, text);
    return l;
}

// One line, `w` wide, ending in "..." when the text is longer. LVGL only truncates
// with DOTS when the height is fixed; with just a width it wraps onto more lines.
inline void one_line(lv_obj_t* l, int32_t w) {
    lv_obj_set_size(l, w, lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
}

// lv_label_set_text always invalidates; skip it when nothing changed.
inline void set_text(lv_obj_t* l, const char* text) {
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

inline lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* src, uint32_t color) {
    lv_obj_t* i = lv_image_create(parent);
    lv_image_set_src(i, src);
    lv_obj_set_style_image_recolor(i, c(color), 0);
    lv_obj_set_style_image_recolor_opa(i, LV_OPA_COVER, 0);
    return i;
}

// Makes a box behave like a button: clickable, with a pressed tint.
inline void make_button(lv_obj_t* o, lv_event_cb_t cb, void* user = nullptr, uint32_t pressed = kCardHi) {
    lv_obj_set_clickable(o, true);
    lv_obj_set_style_bg_color(o, c(pressed), LV_STATE_PRESSED);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user);
}

}  // namespace theme

namespace theme {

// Rounded button with a centred text label. Returns the button; the label is child 0.
inline lv_obj_t* text_button(lv_obj_t* parent, int x, int y, int w, int h, const char* text, const lv_font_t* font,
                             uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* user = nullptr) {
    lv_obj_t* b = card(parent, x, y, w, h, bg, h / 2 < 20 ? h / 2 : 20);
    lv_obj_t* l = label(b, text, font, fg);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_set_clickable(b, true);
    lv_obj_set_style_bg_color(b, lv_color_mix(lv_color_white(), c(bg), 40), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

// Round icon button.
inline lv_obj_t* icon_button(lv_obj_t* parent, int x, int y, int size, const lv_image_dsc_t* src, uint32_t bg,
                             uint32_t fg, lv_event_cb_t cb, void* user = nullptr) {
    lv_obj_t* b = card(parent, x, y, size, size, bg, LV_RADIUS_CIRCLE);
    lv_obj_t* i = icon(b, src, fg);
    lv_obj_center(i);
    lv_obj_set_clickable(b, true);
    lv_obj_set_style_bg_color(b, lv_color_mix(lv_color_white(), c(bg), 40), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

// Vertically scrolling list area with a column layout.
inline lv_obj_t* list(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* o = box(parent, x, y, w, h);
    lv_obj_set_scrollable(o, true);
    lv_obj_set_scroll_dir(o, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, 10, 0);
    lv_obj_set_style_pad_bottom(o, 12, 0);
    return o;
}

}  // namespace theme
