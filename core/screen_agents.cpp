// Agents: one row per coding agent reported by buddyd (from Orca).
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "hal.h"
#include "theme.h"

using namespace theme;

namespace agents {
namespace {

lv_obj_t *list_o, *summary_l, *empty_l;
uint32_t last_refresh = 0;

int rank(model::AgentState s) {
    switch (s) {
        case model::AgentState::Blocked: return 0;
        case model::AgentState::Waiting: return 1;
        case model::AgentState::Failed: return 2;
        case model::AgentState::Working: return 3;
        case model::AgentState::Done: return 4;
        default: return 5;
    }
}

void describe(model::AgentState s, const char** text, uint32_t* color) {
    switch (s) {
        case model::AgentState::Blocked: *text = "BLOCKED"; *color = kAmber; break;
        case model::AgentState::Waiting: *text = "NEEDS YOU"; *color = kAmber; break;
        case model::AgentState::Failed: *text = "FAILED"; *color = kDanger; break;
        case model::AgentState::Working: *text = "WORKING"; *color = kBreak; break;
        case model::AgentState::Done: *text = "DONE"; *color = kMuted; break;
        default: *text = "IDLE"; *color = kMuted; break;
    }
}

void ago(uint32_t since, char* out, size_t n) {
    out[0] = 0;
    if (!model::m.have_time || since == 0) return;
    const int64_t now = model::m.unix_at_sync + (hal::millis() - model::m.ms_at_sync) / 1000;
    int64_t d = now - since;
    if (d < 0) d = 0;
    if (d < 60) snprintf(out, n, "just now");
    else if (d < 3600) snprintf(out, n, "%d min", (int)(d / 60));
    else snprintf(out, n, "%dh %02dm", (int)(d / 3600), (int)((d % 3600) / 60));
}

void on_row(lv_event_t*) { app::ack_alerts(); }

}  // namespace

void build(lv_obj_t* page) {
    lv_obj_t* title = label(page, "Agents", &font_title_32, kText);
    lv_obj_set_pos(title, 24, 14);
    summary_l = label(page, "", &font_ui_20, kMuted);
    lv_obj_align(summary_l, LV_ALIGN_TOP_RIGHT, -24, 26);
    list_o = list(page, 12, 68, 456, kPageH - 68);
    empty_l = label(page, "", &font_ui_20, kMuted);
    lv_obj_set_style_text_align(empty_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(empty_l, 6, 0);
    lv_obj_set_width(empty_l, 400);
    lv_obj_align(empty_l, LV_ALIGN_CENTER, 0, 0);
    refresh();
}

void refresh() {
    using namespace model;
    last_refresh = hal::millis();
    lv_obj_clean(list_o);

    char buf[96];
    const int attn = count_attention();
    if (attn) snprintf(buf, sizeof(buf), attn == 1 ? "1 needs you" : "%d need you", attn);
    else if (m.n_agents) snprintf(buf, sizeof(buf), "%d running", count_agents(AgentState::Working));
    else buf[0] = 0;
    set_text(summary_l, buf);
    lv_obj_set_style_text_color(summary_l, c(attn ? kAmber : kMuted), 0);

    lv_obj_set_hidden(empty_l, m.n_agents > 0);
    if (m.n_agents == 0) {
        if (hal::link_state() != hal::LinkState::Connected) {
            set_text(empty_l, "Not connected to your laptop.\nStart buddyd to see your agents.");
        } else if (m.agents_note[0]) {
            set_text(empty_l, m.agents_note);
        } else {
            set_text(empty_l, "No agents running.");
        }
        return;
    }

    // Selection sort by urgency; the list is short.
    int order[kMaxAgents];
    for (int i = 0; i < m.n_agents; i++) order[i] = i;
    for (int i = 0; i < m.n_agents; i++) {
        for (int j = i + 1; j < m.n_agents; j++) {
            if (rank(m.agents[order[j]].state) < rank(m.agents[order[i]].state)) {
                const int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    for (int k = 0; k < m.n_agents; k++) {
        const Agent& a = m.agents[order[k]];
        const char* st;
        uint32_t col;
        describe(a.state, &st, &col);
        const bool attn_row = needs_attention(a.state);

        lv_obj_t* row = card(list_o, 0, 0, 456, 70, attn_row ? 0x3A3320 : kCard);
        make_button(row, on_row);
        if (attn_row && a.needs_ack) {
            lv_obj_set_style_border_width(row, 2, 0);
            lv_obj_set_style_border_color(row, c(kAmber), 0);
        }
        card(row, 20, 27, 16, 16, col, LV_RADIUS_CIRCLE);

        lv_obj_t* name = label(row, a.name, &font_ui_20, kText);
        lv_obj_set_pos(name, 52, 9);
        lv_obj_set_width(name, 270);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

        char when[24];
        ago(a.since, when, sizeof(when));
        if (a.kind[0] && when[0]) snprintf(buf, sizeof(buf), "%s  \xE2\x80\xA2  %s", a.kind, when);
        else snprintf(buf, sizeof(buf), "%s%s", a.kind, when);
        lv_obj_t* sub = label(row, buf, &font_ui_16, kMuted);
        lv_obj_set_pos(sub, 52, 40);

        lv_obj_t* state = label(row, st, &font_ui_16, col);
        lv_obj_align(state, LV_ALIGN_RIGHT_MID, -18, 0);
    }
}

void tick() {
    // Keeps the "3 min" durations fresh while the screen is open.
    if (hal::millis() - last_refresh > 20000 && !lv_obj_is_scrolling(list_o)) refresh();
}

}  // namespace agents
