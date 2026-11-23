// Focus: pomodoro timer for a goal, or a free session.
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "theme.h"

using namespace theme;

namespace focus {
namespace {

Mascot guy;
lv_obj_t *idle_v, *run_v, *done_v;
// idle
lv_obj_t *pick_title, *preset_num[3];
int preset_min[3] = {25, 50, 10};
char picked[20] = "";
// running
lv_obj_t *arc, *phase_l, *digit[5], *goal_l, *pause_btn, *today_l;
// done
lv_obj_t *break_btn;

void on_pick(lv_event_t*) {
    // Cycle: each unfinished goal in order, then "free focus".
    using namespace model;
    int start = -1;
    for (int i = 0; i < m.n_goals; i++) {
        if (!strcmp(m.goals[i].id, picked)) start = i;
    }
    picked[0] = 0;
    for (int i = start + 1; i < m.n_goals; i++) {
        if (!m.goals[i].done) {
            strcpy(picked, m.goals[i].id);
            break;
        }
    }
    refresh();
}

void on_preset(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    app::start_focus(picked, preset_min[i]);
}

void on_pause(lv_event_t*) { app::set_paused(!model::m.timer.paused); }
void on_stop(lv_event_t*) { app::stop_timer(); }
void on_break(lv_event_t*) { app::start_break(); }

}  // namespace

Mascot& mascot() { return guy; }
const char* picked_goal() { return picked; }

void pick_goal(const char* id) {
    strncpy(picked, id ? id : "", sizeof(picked) - 1);
    picked[sizeof(picked) - 1] = 0;
    if (idle_v) refresh();
}

void build(lv_obj_t* page) {
    // ---- idle: choose a goal and a length ----
    idle_v = box(page, 0, 0, 480, kPageH);
    lv_obj_t* title = label(idle_v, "Focus", &font_title_32, kText);
    lv_obj_set_pos(title, 24, 14);

    lv_obj_t* chip = card(idle_v, 24, 70, 432, 68);
    make_button(chip, on_pick);
    lv_obj_t* cap = label(chip, "WORKING ON  (tap to change)", &font_ui_16, kGold);
    lv_obj_set_pos(cap, 18, 9);
    pick_title = label(chip, "", &font_ui_20, kText);
    lv_obj_set_pos(pick_title, 18, 33);
    one_line(pick_title, 396);

    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = card(idle_v, 24 + i * 148, 152, 136, 88, i == 0 ? kFocus : kCard);
        lv_obj_set_clickable(b, true);
        lv_obj_set_style_bg_color(b, c(i == 0 ? 0xFF8C8C : kCardHi), LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, on_preset, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        preset_num[i] = label(b, "25", &font_title_32, i == 0 ? kInk : kText);
        lv_obj_align(preset_num[i], LV_ALIGN_CENTER, 0, -11);
        lv_obj_t* mn = label(b, "minutes", &font_ui_16, i == 0 ? kInk : kMuted);
        lv_obj_align(mn, LV_ALIGN_CENTER, 0, 20);
    }

    // ---- running ----
    run_v = box(page, 0, 0, 480, kPageH);
    arc = lv_arc_create(run_v);
    lv_obj_set_size(arc, 256, 256);
    lv_obj_set_pos(arc, 112, 14);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, c(kCard), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    phase_l = label(run_v, "FOCUS", &font_ui_16, kMuted);
    lv_obj_set_width(phase_l, 200);
    lv_obj_set_style_text_align(phase_l, LV_TEXT_ALIGN_CENTER, 0);
    // The time is centred in the ring (arc centre y = 14 + 256/2); the phase sits above it.
    const int time_y = 14 + 128 - font_timer_72.line_height / 2;
    lv_obj_set_pos(phase_l, 140, time_y - 28);

    // Each digit sits in its own fixed cell so the time doesn't jiggle.
    const int cell = 44, colon = 22, total = 4 * cell + colon, x0 = 240 - total / 2;
    for (int i = 0; i < 5; i++) {
        const int w = i == 2 ? colon : cell;
        const int x = x0 + (i < 2 ? i * cell : (i == 2 ? 2 * cell : 2 * cell + colon + (i - 3) * cell));
        digit[i] = label(run_v, i == 2 ? ":" : "0", &font_timer_72, kText);
        lv_obj_set_width(digit[i], w);
        lv_obj_set_style_text_align(digit[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(digit[i], x, time_y);
    }

    goal_l = label(run_v, "", &font_ui_20, kText);
    one_line(goal_l, 432);
    lv_obj_set_style_text_align(goal_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(goal_l, 24, 284);

    pause_btn = icon_button(run_v, 174, 328, 60, &icon_pause, kCard, kText, on_pause);
    icon_button(run_v, 246, 328, 60, &icon_stop, kCard, kDanger, on_stop);
    today_l = label(run_v, "", &font_ui_16, kMuted);
    lv_obj_align(today_l, LV_ALIGN_BOTTOM_RIGHT, -24, -26);

    // ---- done: offer a break ----
    done_v = box(page, 0, 0, 480, kPageH);
    lv_obj_t* d1 = label(done_v, "Session complete!", &font_title_32, kText);
    lv_obj_align(d1, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_t* d2 = label(done_v, "Well guarded. Time to rest your eyes.", &font_ui_20, kMuted);
    lv_obj_align(d2, LV_ALIGN_TOP_MID, 0, 78);
    break_btn = text_button(done_v, 200, 130, 256, 60, "Take a break", &font_ui_24, kBreak, kInk, on_break);
    text_button(done_v, 200, 204, 256, 52, "Skip", &font_ui_20, kCard, kText, on_stop);

    // One small mascot shared by the three views.
    guy.create(page, &mascot_small, 78, 400);
    refresh();
}

void refresh() {
    using namespace model;
    const Timer& t = m.timer;
    const bool running = t.phase == Phase::Focus || t.phase == Phase::Break;
    lv_obj_set_hidden(idle_v, t.phase != Phase::Idle);
    lv_obj_set_hidden(run_v, !running);
    lv_obj_set_hidden(done_v, t.phase != Phase::FocusDone);

    char buf[64];
    // idle
    if (picked[0] && (!find_goal(picked) || find_goal(picked)->done)) picked[0] = 0;
    Goal* g = find_goal(picked);
    set_text(pick_title, g ? g->title : "Free focus, no goal");
    preset_min[0] = m.settings.focus_min;
    preset_min[1] = m.settings.focus_min >= 50 ? 25 : 50;
    for (int i = 0; i < 3; i++) {
        snprintf(buf, sizeof(buf), "%d", preset_min[i]);
        set_text(preset_num[i], buf);
    }

    // running
    const bool brk = t.phase == Phase::Break;
    const uint32_t col = brk ? kBreak : (t.paused ? kAmber : kFocus);
    lv_obj_set_style_arc_color(arc, c(col), LV_PART_INDICATOR);
    set_text(phase_l, t.paused ? "PAUSED" : (brk ? "BREAK" : "FOCUS"));
    lv_obj_set_style_text_color(phase_l, c(col), 0);
    Goal* rg = find_goal(t.goal);
    set_text(goal_l, brk ? "Stretch, look out of the window." : (rg ? rg->title : "Free focus"));
    lv_image_set_src(lv_obj_get_child(pause_btn, 0), t.paused ? &icon_play : &icon_pause);
    snprintf(buf, sizeof(buf), t.done_today == 1 ? "1 session today" : "%d sessions today", t.done_today);
    set_text(today_l, buf);

    // done
    snprintf(buf, sizeof(buf), "Break  %d min", next_break_minutes());
    set_text(lv_obj_get_child(break_btn, 0), buf);
}

void tick() {
    guy.tick();
    const model::Timer& t = model::m.timer;
    if (t.phase != model::Phase::Focus && t.phase != model::Phase::Break) return;

    const uint32_t rem = model::timer_remaining_ms();
    const uint32_t s = (rem + 999) / 1000;  // round up so a live timer never reads 00:00
    const unsigned mm = s / 60, ss = s % 60;
    const char d[5] = {(char)('0' + (mm / 10) % 10), (char)('0' + mm % 10), ':', (char)('0' + ss / 10), (char)('0' + ss % 10)};
    for (int i = 0; i < 5; i++) {
        if (i == 2) continue;
        const char txt[2] = {d[i], 0};
        set_text(digit[i], txt);
    }
    const int v = t.total_ms ? (int)((uint64_t)t.elapsed_ms * 1000 / t.total_ms) : 0;
    if (lv_arc_get_value(arc) != v) lv_arc_set_value(arc, v);
}

}  // namespace focus
