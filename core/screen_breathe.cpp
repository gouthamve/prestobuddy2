// Breathe: a paced breathing session with a circle that swells and settles.
#include <stdio.h>

#include "app.h"
#include "hal.h"
#include "theme.h"

using namespace theme;

namespace breathe {
namespace {

Mascot guy;
lv_obj_t *setup_v, *run_v;
lv_obj_t *pattern_btn[3], *len_btn[4];
lv_obj_t *disc, *stage_l, *count_l, *remain_l, *done_l;
int pattern = 0, len_index = 1;
const int kLengths[4] = {1, 3, 5, 10};
uint32_t done_until = 0;

constexpr int kCx = 240, kCy = 140, kMinD = 104, kMaxD = 236;

void select(lv_obj_t* b, bool on) {
    lv_obj_set_style_bg_color(b, c(on ? kTeal : kCard), 0);
    const uint32_t n = lv_obj_get_child_count(b);
    for (uint32_t i = 0; i < n; i++) lv_obj_set_style_text_color(lv_obj_get_child(b, i), c(on ? kInk : kText), 0);
}

void update_selection() {
    for (int i = 0; i < 3; i++) select(pattern_btn[i], i == pattern);
    for (int i = 0; i < 4; i++) select(len_btn[i], i == len_index);
}

void on_pattern(lv_event_t* e) {
    pattern = (int)(intptr_t)lv_event_get_user_data(e);
    update_selection();
}
void on_len(lv_event_t* e) {
    len_index = (int)(intptr_t)lv_event_get_user_data(e);
    update_selection();
}
void on_begin(lv_event_t*) { app::start_breathe(pattern, kLengths[len_index]); }
void on_end(lv_event_t*) { app::stop_breathe(); }

}  // namespace

Mascot& mascot() { return guy; }

void build(lv_obj_t* page) {
    // ---- setup ----
    setup_v = box(page, 0, 0, 480, kPageH);
    lv_obj_t* title = label(setup_v, "Breathe", &font_title_32, kText);
    lv_obj_set_pos(title, 24, 14);
    done_l = label(setup_v, "", &font_ui_20, kTeal);
    lv_obj_align(done_l, LV_ALIGN_TOP_RIGHT, -24, 26);

    static const char* names[3] = {"Box", "Calm", "Even"};
    static const char* counts[3] = {"4-4-4-4", "4-7-8", "5-5"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = card(setup_v, 24 + i * 148, 70, 136, 64);
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, on_pattern, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* n = label(b, names[i], &font_ui_20, kText);
        lv_obj_align(n, LV_ALIGN_CENTER, 0, -11);
        lv_obj_t* k = label(b, counts[i], &font_ui_16, kText);
        lv_obj_align(k, LV_ALIGN_CENTER, 0, 13);
        pattern_btn[i] = b;
    }
    for (int i = 0; i < 4; i++) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d min", kLengths[i]);
        len_btn[i] = text_button(setup_v, 24 + i * 110, 146, 102, 50, buf, &font_ui_20, kCard, kText, on_len,
                                 (void*)(intptr_t)i);
    }
    text_button(setup_v, 200, 216, 256, 60, "Begin", &font_ui_24, kTeal, kInk, on_begin);
    update_selection();

    // ---- running ----
    run_v = box(page, 0, 0, 480, kPageH);
    remain_l = label(run_v, "", &font_ui_20, kMuted);
    lv_obj_set_pos(remain_l, 24, 18);
    text_button(run_v, 380, 12, 76, 40, "End", &font_ui_16, kCard, kText, on_end);

    lv_obj_t* ring = box(run_v, kCx - (kMaxD + 16) / 2, kCy - (kMaxD + 16) / 2, kMaxD + 16, kMaxD + 16);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, c(kCardHi), 0);
    disc = card(run_v, kCx - kMinD / 2, kCy - kMinD / 2, kMinD, kMinD, 0x2A8F8B, LV_RADIUS_CIRCLE);
    stage_l = label(run_v, "In", &font_ui_24, kText);
    lv_obj_set_width(stage_l, 100);
    lv_obj_set_style_text_align(stage_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(stage_l, kCx - 50, kCy - 30);
    count_l = label(run_v, "", &font_ui_20, kText);
    lv_obj_set_width(count_l, 100);
    lv_obj_set_style_text_align(count_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(count_l, kCx - 50, kCy + 4);

    guy.create(page, &mascot_small, 78, 400);
    refresh();
}

void refresh() {
    const bool on = model::m.breathe.active;
    lv_obj_set_hidden(setup_v, on);
    lv_obj_set_hidden(run_v, !on);
    // After a finished session, say so on the setup view for a few seconds.
    if (!on && done_until == 1) {
        set_text(done_l, "Session complete");
        done_until = hal::millis() + 6000;
    }
}

void note_completed() { done_until = 1; }

void tick() {
    guy.tick();
    if (done_until > 1 && hal::millis() > done_until) {
        set_text(done_l, "");
        done_until = 0;
    }
    if (!model::m.breathe.active) return;

    const model::BreathNow b = model::breathe_now();
    guy.set_breath(b.level);

    // Even sizes only, so the disc stays centred to the pixel.
    const int d = (kMinD + (int)((kMaxD - kMinD) * b.level)) & ~1;
    if (lv_obj_get_width(disc) != d) {
        lv_obj_set_size(disc, d, d);
        lv_obj_set_pos(disc, kCx - d / 2, kCy - d / 2);
    }
    static const char* names[4] = {"In", "Hold", "Out", "Hold"};
    set_text(stage_l, names[(int)b.stage]);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", b.stage_left_s);
    set_text(count_l, buf);
    const uint32_t s = (b.remain_ms + 999) / 1000;
    snprintf(buf, sizeof(buf), "%u:%02u left", (unsigned)(s / 60), (unsigned)(s % 60));
    set_text(remain_l, buf);
}

}  // namespace breathe
