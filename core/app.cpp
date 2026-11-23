// App controller: owns navigation, reacts to buddyd and to the timer, and
// decides what the mascot, LEDs and piezo should be doing.
#include "app.h"

#include <stdio.h>
#include <string.h>

#include "fx.h"
#include "hal.h"
#include "theme.h"

using namespace theme;
using model::m;
using model::Phase;

namespace app {
namespace {

lv_obj_t* pages[kPageCount];
lv_obj_t* dock_icon[kPageCount];
lv_obj_t *indicator, *agent_badge, *focus_badge;
lv_obj_t *banner, *banner_l;
bool banner_up = false;
Page cur = Home;

bool was_connected = false;
char last_status[200] = "";
model::BreathStage last_stage = model::BreathStage::HoldOut;
bool was_quiet = false;
uint32_t last_alert_act = 0;
uint32_t frames_drawn = 0, last_stats = 0;
uint32_t render_t0 = 0, render_ms_sum = 0, render_ms_max = 0;

const lv_image_dsc_t* const kDockIcons[kPageCount] = {&icon_home, &icon_goals, &icon_focus, &icon_agents, &icon_breathe};

Mascot* visible_mascot() {
    switch (cur) {
        case Home: return &home::mascot();
        case Focus: return &focus::mascot();
        case Breathe: return &breathe::mascot();
        default: return nullptr;
    }
}

void act(Mascot::Act a) {
    if (Mascot* g = visible_mascot()) g->act(a);
}

void refresh_all() {
    home::refresh();
    focus::refresh();
    breathe::refresh();
    if (cur == Goals) goals::refresh();
    if (cur == Agents) agents::refresh();
    lv_obj_set_hidden(agent_badge, model::count_attention() == 0);
    lv_obj_set_hidden(focus_badge, m.timer.phase == Phase::Idle);
    lv_obj_set_style_bg_color(focus_badge, c(m.timer.phase == Phase::Break ? kBreak : kFocus), 0);
}

// ---- messages to buddyd ----

void send_status(bool force) {
    const model::Timer& t = m.timer;
    static const char* names[] = {"idle", "focus", "done", "break"};
    char head[160];
    snprintf(head, sizeof(head), "\"phase\":\"%s\",\"paused\":%s,\"goal\":\"%s\",\"total\":%u,\"pomos_today\":%d,\"breathing\":%s",
             names[(int)t.phase], t.paused ? "true" : "false", t.goal, (unsigned)(t.total_ms / 1000), t.done_today,
             m.breathe.active ? "true" : "false");
    if (!force && !strcmp(head, last_status)) return;
    char line[260];
    snprintf(line, sizeof(line), "{\"t\":\"status\",%s,\"remain\":%u}", head, (unsigned)((model::timer_remaining_ms() + 999) / 1000));
    if (hal::link_send(line)) strcpy(last_status, head);
}

void send_fmt(const char* fmt, const char* a, int b) {
    char line[160];
    snprintf(line, sizeof(line), fmt, a, b);
    hal::link_send(line);
}

// ---- alerts ----

void set_banner(bool up) {
    if (up == banner_up) return;
    banner_up = up;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, banner);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_y((lv_obj_t*)o, v); });
    lv_anim_set_values(&a, lv_obj_get_y(banner), up ? 10 : -70);
    lv_anim_set_duration(&a, 320);
    lv_anim_set_path_cb(&a, up ? lv_anim_path_overshoot : lv_anim_path_ease_in);
    lv_anim_start(&a);
}

void update_banner(const char* name) {
    const int n = model::count_unacked();
    if (n == 0) {
        set_banner(false);
        return;
    }
    char buf[96];
    if (n == 1 && name && name[0]) snprintf(buf, sizeof(buf), "%s needs you", name);
    else if (n == 1) snprintf(buf, sizeof(buf), "An agent needs you");
    else snprintf(buf, sizeof(buf), "%d agents need you", n);
    set_text(banner_l, buf);
    set_banner(true);
}

// How loudly to announce an agent, given what the user is doing.
void announce_attention() {
    const bool focusing = m.timer.phase == Phase::Focus && !m.timer.paused;
    const int level = (focusing || m.breathe.active) ? m.settings.focus_alerts : 2;
    fx::set_alert(true, true);
    if (level == 1) fx::play(fx::Tune::AgentChirpSoft);
    if (level == 2) {
        fx::play(fx::Tune::AgentChirp);
        act(Mascot::Act::Alert);
        last_alert_act = hal::millis();
    }
}

void on_banner(lv_event_t*) {
    ack_alerts();
    go(Agents);
}

// ---- incoming lines ----

void on_line(const char* line, size_t, void*) {
    model::Change ch;
    if (!model::apply_line(line, &ch)) return;

    if (ch.ping) send_status(true);
    // A session started from the laptop brings its timer into view.
    if (ch.cmd_focus) {
        start_focus(ch.cmd_goal, ch.cmd_minutes);
        go(Focus);
    }
    if (ch.cmd_break) {
        start_break();
        go(Focus);
    }
    if (ch.cmd_page >= 0) go((Page)ch.cmd_page);
    if (ch.cmd_stop) stop_timer();
    if (ch.cmd_pause) set_paused(!m.timer.paused);

    if (ch.new_attention > 0) {
        announce_attention();
        update_banner(ch.attention_name);
    } else if (ch.agents) {
        update_banner(nullptr);
    }
    const bool hush = (m.timer.phase == Phase::Focus || m.breathe.active) && m.settings.focus_alerts == 0;
    if (ch.new_failed > 0) {
        fx::flash(fx::Flash::AgentFail);
        if (!hush) fx::play(fx::Tune::AgentFail);
    } else if (ch.new_done > 0) {
        fx::flash(fx::Flash::AgentDone);
        if (!hush) fx::play(fx::Tune::AgentDone);
    }

    if (ch.goals || ch.agents || ch.settings) refresh_all();
}

void on_dock(lv_event_t* e) {
    fx::play(fx::Tune::Tap);
    go((Page)(intptr_t)lv_event_get_user_data(e));
}

void build_dock(lv_obj_t* scr) {
    lv_obj_t* dock = card(scr, 0, kPageH, 480, kDockH, 0x1A2140, 0);
    for (int i = 0; i < kPageCount; i++) {
        lv_obj_t* hit = box(dock, i * 96, 0, 96, kDockH);
        lv_obj_set_clickable(hit, true);
        lv_obj_add_event_cb(hit, on_dock, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        dock_icon[i] = icon(hit, kDockIcons[i], kMuted);
        lv_obj_align(dock_icon[i], LV_ALIGN_CENTER, 0, -4);
    }
    indicator = card(dock, 26, kDockH - 12, 44, 5, kGold, 3);
    agent_badge = card(dock, Agents * 96 + 60, 10, 14, 14, kAmber, LV_RADIUS_CIRCLE);
    focus_badge = card(dock, Focus * 96 + 60, 10, 14, 14, kFocus, LV_RADIUS_CIRCLE);
}

}  // namespace

Page page() { return cur; }

void go(Page p) {
    if (p < 0 || p >= kPageCount) return;
    // Measured on the Presto: a full-page slide runs at ~32 fps on the list pages but
    // only ~13 fps on the pages with the mascot or a large arc, so those switch
    // instantly and let the mascot's hop be the entrance.
    if (p != cur && (p == Goals || p == Agents)) {
        // Slide the new page in from the side it lives on in the dock.
        lv_anim_t s;
        lv_anim_init(&s);
        lv_anim_set_var(&s, pages[p]);
        lv_anim_set_exec_cb(&s, [](void* o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); });
        lv_anim_set_values(&s, p > cur ? 56 : -56, 0);
        lv_anim_set_duration(&s, 200);
        lv_anim_set_path_cb(&s, lv_anim_path_ease_out);
        static uint32_t t0, f0;
        t0 = hal::millis();
        f0 = frames_drawn;
        lv_anim_set_completed_cb(&s, [](lv_anim_t*) {
            hal::log("slide: %u frames in %u ms", (unsigned)(frames_drawn - f0), (unsigned)(hal::millis() - t0));
        });
        lv_anim_start(&s);
    }
    cur = p;
    for (int i = 0; i < kPageCount; i++) {
        lv_obj_set_hidden(pages[i], i != p);
        lv_obj_set_style_image_recolor(dock_icon[i], c(i == p ? kGold : kMuted), 0);
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, indicator);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_x((lv_obj_t*)o, v); });
    lv_anim_set_values(&a, lv_obj_get_x(indicator), p * 96 + 26);
    lv_anim_set_duration(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
    refresh_all();
    if (Mascot* g = visible_mascot()) g->act(Mascot::Act::Hop);
}

void start_focus(const char* goal_id, int minutes) {
    model::timer_start_focus(goal_id, minutes);
    fx::play(fx::Tune::FocusStart);
    refresh_all();
    act(Mascot::Act::Salute);
    send_status(true);
}

void start_break() {
    model::timer_start_break(0);
    refresh_all();
    act(Mascot::Act::March);  // changing of the guard
    send_status(true);
}

void set_paused(bool paused) {
    model::timer_set_paused(paused);
    refresh_all();
    send_status(true);
}

void stop_timer() {
    model::timer_stop();
    refresh_all();
    send_status(true);
}

void toggle_goal(const char* id) {
    model::Goal* g = model::find_goal(id);
    if (!g) return;
    g->done = !g->done;  // shown at once; buddyd confirms with the next snapshot
    send_fmt(g->done ? "{\"t\":\"goal\",\"id\":\"%s\",\"done\":true}" : "{\"t\":\"goal\",\"id\":\"%s\",\"done\":false}", g->id, 0);
    if (g->done) {
        fx::play(fx::Tune::GoalDone);
        fx::flash(fx::Flash::Celebrate);
        act(Mascot::Act::Celebrate);
    } else {
        fx::play(fx::Tune::Tap);
    }
    goals::refresh();
    refresh_all();
}

void start_breathe(int pattern, int minutes) {
    model::breathe_start(pattern, minutes);
    last_stage = model::BreathStage::HoldOut;
    refresh_all();
    send_status(true);
}

void stop_breathe() {
    model::breathe_stop();
    refresh_all();
    send_status(true);
}

void ack_alerts() {
    bool any = false;
    for (int i = 0; i < m.n_agents; i++) {
        any |= m.agents[i].needs_ack;
        m.agents[i].needs_ack = false;
    }
    if (!any) return;
    fx::set_alert(false, false);
    set_banner(false);
    hal::link_send("{\"t\":\"ack\"}");
    refresh_all();
}

void boop() {
    fx::play(fx::Tune::Boop);
    act(Mascot::Act::Salute);
}

}  // namespace app

void app_init() {
    using namespace app;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, c(kBg), 0);
    lv_obj_set_scrollable(scr, false);

    for (int i = 0; i < kPageCount; i++) pages[i] = box(scr, 0, 0, 480, kPageH);
    build_dock(scr);
    home::build(pages[Home]);
    goals::build(pages[Goals]);
    focus::build(pages[Focus]);
    agents::build(pages[Agents]);
    breathe::build(pages[Breathe]);

    banner = card(lv_layer_top(), 20, -70, 440, 56, kAmber, 28);
    make_button(banner, on_banner, nullptr, 0xFFC860);
    banner_l = label(banner, "", &font_ui_20, kInk);
    one_line(banner_l, 400);
    lv_obj_set_style_text_align(banner_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(banner_l);

    hal::link_set_handler(on_line, nullptr);
    lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t*) { render_t0 = hal::millis(); }, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t*) {
        const uint32_t d = hal::millis() - render_t0;
        frames_drawn++;
        render_ms_sum += d;
        if (d > render_ms_max) render_ms_max = d;
    }, LV_EVENT_REFR_READY, nullptr);
    go(Home);
    fx::flash(fx::Flash::Startup);
    fx::play(fx::Tune::Startup);
    home::mascot().act(Mascot::Act::Salute);
    hal::log("prestobuddy2 started");
}

void app_tick() {
    using namespace app;
    const uint32_t now = hal::millis();

    // ---- link ----
    const bool connected = hal::link_state() == hal::LinkState::Connected;
    if (connected != was_connected) {
        was_connected = connected;
        if (connected) {
            hal::link_send("{\"t\":\"hello\",\"fw\":\"0.1\"}");
            last_status[0] = 0;
        }
        refresh_all();
    }

    // ---- timer ----
    const Phase finished = model::timer_tick();
    if (finished == Phase::Focus) {
        if (model::Goal* g = model::find_goal(m.timer.goal)) g->pomos++;
        send_fmt("{\"t\":\"pomo\",\"goal\":\"%s\",\"min\":%d}", m.timer.goal, m.timer.minutes);
        fx::play(fx::Tune::FocusDone);
        fx::flash(fx::Flash::Celebrate);
        go(Focus);  // bring up the "take a break?" prompt wherever the user was
        act(Mascot::Act::Celebrate);
    } else if (finished == Phase::Break) {
        fx::play(fx::Tune::BreakDone);
        refresh_all();
        act(Mascot::Act::March);
    }

    // ---- breathing ----
    if (m.breathe.active) {
        const model::BreathNow b = model::breathe_now();
        if (b.remain_ms == 0) {
            send_fmt("{\"t\":\"breathe\",\"pattern\":\"%s\",\"min\":%d}", model::kPatterns[m.breathe.pattern].name, m.breathe.minutes);
            breathe::note_completed();
            stop_breathe();
            fx::play(fx::Tune::BreatheEnd);
            act(Mascot::Act::Hop);
        } else if (b.stage != last_stage) {
            last_stage = b.stage;
            if (b.stage == model::BreathStage::In) fx::play(fx::Tune::BreathIn);
            if (b.stage == model::BreathStage::Out) fx::play(fx::Tune::BreathOut);
        }
        fx::set_breath(b.level);
    }

    // ---- alert reminders: 1 and 5 minutes after an agent started waiting ----
    int unacked = 0;
    for (int i = 0; i < m.n_agents; i++) {
        model::Agent& a = m.agents[i];
        if (!model::needs_attention(a.state) || !a.needs_ack) continue;
        unacked++;
        const uint32_t waited = now - a.attn_ms;
        if ((a.reminders == 0 && waited > 60000) || (a.reminders == 1 && waited > 300000)) {
            a.reminders++;
            announce_attention();
        }
    }
    fx::set_alert(unacked > 0, false);
    if (unacked == 0 && banner_up) set_banner(false);

    // ---- LEDs ----
    const model::Timer& t = m.timer;
    if (m.breathe.active) fx::set_base(fx::Base::Breathe);
    else if (t.phase == Phase::Focus) fx::set_base(t.paused ? fx::Base::Paused : fx::Base::Focus);
    else if (t.phase == Phase::Break) fx::set_base(t.paused ? fx::Base::Paused : fx::Base::Break);
    else fx::set_base(fx::Base::Idle);
    if (t.total_ms) fx::set_progress((float)t.elapsed_ms / (float)t.total_ms);
    fx::tick();

    // ---- quiet hours dim the screen ----
    const bool quiet = model::quiet_now();
    if (quiet != was_quiet) {
        was_quiet = quiet;
        hal::set_backlight(quiet ? 50 : 200);
    }

    // ---- moods ----
    const model::LocalTime lt = model::local_time();
    const bool late = quiet || (lt.valid && (lt.hour >= 23 || lt.hour < 6));
    const bool guarding = t.phase == Phase::Focus && !t.paused;
    const Mascot::Mood timer_mood = guarding ? Mascot::Mood::Guard
                                             : (t.phase == Phase::Break ? Mascot::Mood::AtEase : Mascot::Mood::Idle);
    home::mascot().set_mood(timer_mood != Mascot::Mood::Idle ? timer_mood : (late ? Mascot::Mood::Sleepy : Mascot::Mood::Idle));
    focus::mascot().set_mood(timer_mood);
    breathe::mascot().set_mood(m.breathe.active ? Mascot::Mood::Meditate : Mascot::Mood::Idle);

    // ---- the visible screen ----
    switch (cur) {
        case Home: home::tick(); break;
        case Focus: focus::tick(); break;
        case Agents: agents::tick(); break;
        case Breathe: breathe::tick(); break;
        default: break;
    }

    send_status(false);

    // Frame-rate log, for tuning on the hardware.
    if (now - last_stats >= 5000) {
        const hal::Stats st = hal::take_stats();
        if (st.free_sram) {
            hal::log("page %d: %u frames in 5 s, render avg %u ms max %u ms, flush max %u us, free sram %u", (int)cur,
                     (unsigned)frames_drawn, (unsigned)(frames_drawn ? render_ms_sum / frames_drawn : 0),
                     (unsigned)render_ms_max, (unsigned)st.flush_us_max, (unsigned)st.free_sram);
        }
        render_ms_sum = render_ms_max = 0;
        frames_drawn = 0;
        last_stats = now;
    }
}
