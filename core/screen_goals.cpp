// Goals: today's list. Tick a goal off or start a focus session on it.
// Goals are typed on the laptop; this screen never edits their text.
#include <stdio.h>

#include "app.h"
#include "theme.h"

using namespace theme;

namespace goals {
namespace {

lv_obj_t *list_o, *count_l, *empty_l;

void on_check(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < model::m.n_goals) app::toggle_goal(model::m.goals[i].id);
}

void on_play(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= model::m.n_goals) return;
    focus::pick_goal(model::m.goals[i].id);
    app::start_focus(model::m.goals[i].id, 0);
    app::go(app::Focus);
}

}  // namespace

void build(lv_obj_t* page) {
    lv_obj_t* title = label(page, "Today", &font_title_32, kText);
    lv_obj_set_pos(title, 24, 14);
    count_l = label(page, "", &font_ui_20, kMuted);
    lv_obj_align(count_l, LV_ALIGN_TOP_RIGHT, -24, 26);

    list_o = list(page, 12, 68, 456, kPageH - 68);
    empty_l = label(page, "No goals for today yet.\nType them on your laptop\nand they will appear here.", &font_ui_20, kMuted);
    lv_obj_set_style_text_align(empty_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(empty_l, 6, 0);
    lv_obj_align(empty_l, LV_ALIGN_CENTER, 0, 0);
    refresh();
}

void refresh() {
    using namespace model;
    lv_obj_clean(list_o);
    lv_obj_set_hidden(empty_l, m.n_goals > 0);

    char buf[48];
    if (m.n_goals) snprintf(buf, sizeof(buf), "%d/%d done", goals_done(), m.n_goals);
    else buf[0] = 0;
    set_text(count_l, buf);

    for (int i = 0; i < m.n_goals; i++) {
        const Goal& g = m.goals[i];
        const bool sub = g.est > 0 || g.pomos > 0;
        lv_obj_t* row = card(list_o, 0, 0, 456, 78);

        lv_obj_t* hit = box(row, 0, 0, 64, 78);
        lv_obj_set_clickable(hit, true);
        lv_obj_add_event_cb(hit, on_check, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* ring = card(hit, 16, 21, 36, 36, g.done ? kBreak : kCard, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(ring, 3, 0);
        lv_obj_set_style_border_color(ring, c(g.done ? kBreak : kMuted), 0);
        lv_obj_set_clickable(ring, false);  // let the tap reach the larger hit area behind it
        if (g.done) {
            lv_obj_t* tick = icon(ring, &icon_check, kInk);
            lv_obj_center(tick);
        }

        lv_obj_t* t = label(row, g.title, &font_ui_20, g.done ? kMuted : kText);
        lv_obj_set_pos(t, 68, sub ? 10 : 12);
        lv_obj_set_size(t, g.done ? 370 : 310, sub ? 28 : 54);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        if (sub) {
            if (g.est > 0) snprintf(buf, sizeof(buf), "%d of %d sessions", g.pomos, g.est);
            else snprintf(buf, sizeof(buf), g.pomos == 1 ? "1 session" : "%d sessions", g.pomos);
            lv_obj_t* s = label(row, buf, &font_ui_16, g.pomos >= g.est && g.est > 0 ? kBreak : kMuted);
            lv_obj_set_pos(s, 68, 44);
        }
        if (!g.done) icon_button(row, 394, 15, 48, &icon_play, kCardHi, kGold, on_play, (void*)(intptr_t)i);
    }
}

}  // namespace goals
