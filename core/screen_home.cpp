// Home: the mascot, the clock, what's next and how the agents are doing.
#include <stdio.h>

#include "app.h"
#include "hal.h"
#include "theme.h"

using namespace theme;

namespace home {
namespace {

Mascot guy;
lv_obj_t *clock_l, *date_l, *offline_i;
lv_obj_t *next_card, *next_cap, *next_title, *next_btn, *next_time;
lv_obj_t *prog_l, *prog_fill;
lv_obj_t *agent_dot, *agent_l;

const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void on_mascot(lv_event_t*) { app::boop(); }

void on_next(lv_event_t*) {
    const model::Timer& t = model::m.timer;
    if (t.phase == model::Phase::Idle) {
        if (model::Goal* g = model::next_goal()) focus::pick_goal(g->id);
    }
    app::go(app::Focus);
}

void on_start(lv_event_t*) {
    model::Goal* g = model::next_goal();
    app::start_focus(g ? g->id : "", 0);
    app::go(app::Focus);
}

void on_agents(lv_event_t*) { app::go(app::Agents); }
void on_goals(lv_event_t*) { app::go(app::Goals); }

}  // namespace

Mascot& mascot() { return guy; }

void build(lv_obj_t* page) {
    card(page, 6, 158, 244, 244, kBgSpot, LV_RADIUS_CIRCLE);

    clock_l = label(page, "--:--", &font_clock_48, kText);
    lv_obj_set_pos(clock_l, 24, 18);
    date_l = label(page, "", &font_ui_20, kMuted);
    lv_obj_align(date_l, LV_ALIGN_TOP_RIGHT, -24, 30);
    offline_i = icon(page, &icon_wifi_off, kMuted);
    lv_obj_align(offline_i, LV_ALIGN_TOP_RIGHT, -24, 62);

    guy.create(page, &mascot_large, 128, 390);
    lv_obj_t* tap = box(page, 28, 100, 200, 300);
    lv_obj_set_clickable(tap, true);
    lv_obj_add_event_cb(tap, on_mascot, LV_EVENT_CLICKED, nullptr);

    const int x = 252, w = 208;
    next_card = card(page, x, 92, w, 172);
    make_button(next_card, on_next);
    next_cap = label(next_card, "NEXT UP", &font_ui_16, kGold);
    lv_obj_set_pos(next_cap, 16, 12);
    next_title = label(next_card, "", &font_ui_20, kText);
    lv_obj_set_pos(next_title, 16, 36);
    lv_obj_set_size(next_title, w - 32, 82);
    lv_label_set_long_mode(next_title, LV_LABEL_LONG_MODE_DOTS);
    next_btn = text_button(next_card, 16, 124, w - 32, 38, "Start focus", &font_ui_16, kGold, kInk, on_start);
    next_time = label(next_card, "", &font_ui_24, kGold);
    lv_obj_set_pos(next_time, 16, 128);

    lv_obj_t* prog = card(page, x, 274, w, 54);
    make_button(prog, on_goals);
    prog_l = label(prog, "", &font_ui_16, kText);
    lv_obj_set_pos(prog_l, 16, 10);
    card(prog, 16, 38, w - 32, 6, kCardHi, 3);
    prog_fill = card(prog, 16, 38, 0, 6, kGold, 3);

    lv_obj_t* ag = card(page, x, 338, w, 54);
    make_button(ag, on_agents);
    agent_dot = card(ag, 16, 20, 14, 14, kMuted, LV_RADIUS_CIRCLE);
    agent_l = label(ag, "", &font_ui_16, kText);
    lv_obj_set_pos(agent_l, 40, 16);
    lv_obj_set_size(agent_l, w - 52, 22);
    lv_label_set_long_mode(agent_l, LV_LABEL_LONG_MODE_DOTS);

    refresh();
}

void refresh() {
    using namespace model;
    const Timer& t = m.timer;
    const bool running = t.phase == Phase::Focus || t.phase == Phase::Break;
    Goal* g = running ? find_goal(t.goal) : next_goal();

    const char* cap = "NEXT UP";
    uint32_t cap_color = kGold;
    if (t.phase == Phase::Focus) cap = t.paused ? "PAUSED" : "FOCUSING", cap_color = kFocus;
    else if (t.phase == Phase::Break) cap = "ON A BREAK", cap_color = kBreak;
    else if (t.phase == Phase::FocusDone) cap = "SESSION DONE", cap_color = kBreak;
    else if (m.n_goals > 0 && !g) cap = "ALL DONE", cap_color = kBreak;
    set_text(next_cap, cap);
    lv_obj_set_style_text_color(next_cap, c(cap_color), 0);

    if (g) set_text(next_title, g->title);
    else if (t.phase == Phase::Focus) set_text(next_title, "Free focus");
    else if (t.phase == Phase::Break) set_text(next_title, "Stretch, sip some water.");
    else if (m.n_goals == 0) set_text(next_title, "No goals yet. Type some on your laptop.");
    else set_text(next_title, "Every goal ticked off today!");

    lv_obj_set_hidden(next_btn, running || t.phase == Phase::FocusDone);
    lv_obj_set_hidden(next_time, !running);

    char buf[48];
    const int done = goals_done();
    if (m.n_goals == 0) snprintf(buf, sizeof(buf), "No goals today");
    else snprintf(buf, sizeof(buf), "%d of %d goals done", done, m.n_goals);
    set_text(prog_l, buf);
    lv_obj_set_width(prog_fill, m.n_goals ? (208 - 32) * done / m.n_goals : 0);

    const int attn = count_attention();
    const int working = count_agents(AgentState::Working);
    uint32_t dot = kMuted;
    if (attn > 0) {
        snprintf(buf, sizeof(buf), attn == 1 ? "1 agent needs you" : "%d agents need you", attn);
        dot = kAmber;
    } else if (working > 0) {
        snprintf(buf, sizeof(buf), working == 1 ? "1 agent working" : "%d agents working", working);
        dot = kBreak;
    } else if (hal::link_state() != hal::LinkState::Connected) {
        snprintf(buf, sizeof(buf), "Laptop offline");
    } else {
        snprintf(buf, sizeof(buf), "No agents running");
    }
    set_text(agent_l, buf);
    lv_obj_set_style_bg_color(agent_dot, c(dot), 0);
}

void tick() {
    guy.tick();

    char buf[32];
    const model::LocalTime lt = model::local_time();
    if (lt.valid) {
        snprintf(buf, sizeof(buf), "%02d:%02d", lt.hour, lt.minute);
        set_text(clock_l, buf);
        snprintf(buf, sizeof(buf), "%s %d %s", kDays[lt.wday], lt.mday, kMonths[lt.month - 1]);
        set_text(date_l, buf);
    }
    lv_obj_set_hidden(offline_i, hal::link_state() == hal::LinkState::Connected);

    const model::Timer& t = model::m.timer;
    if (t.phase == model::Phase::Focus || t.phase == model::Phase::Break) {
        const uint32_t s = (model::timer_remaining_ms() + 999) / 1000;
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
        set_text(next_time, buf);
    }
}

}  // namespace home
