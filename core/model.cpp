#include "model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "hal.h"
#include "lvgl.h"

namespace model {

BUDDY_BIG_DATA Model m;

const BreathPattern kPatterns[3] = {
    {"Box 4-4-4-4", 4, 4, 4, 4},
    {"Calm 4-7-8", 4, 7, 8, 0},
    {"Even 5-5", 5, 0, 5, 0},
};

namespace {

void copy(char* dst, size_t n, const char* src) {
    if (!src) src = "";
    // Never cut a UTF-8 sequence in half.
    size_t len = strlen(src);
    if (len >= n) {
        len = n - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

const char* str(const cJSON* o, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : nullptr;
}

double num(const cJSON* o, const char* key, double def) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

bool boolean(const cJSON* o, const char* key, bool def) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

uint8_t clamp8(double v, int lo, int hi) { return (uint8_t)(v < lo ? lo : (v > hi ? hi : v)); }

AgentState parse_state(const char* s) {
    if (!s) return AgentState::Unknown;
    if (!strcmp(s, "working")) return AgentState::Working;
    if (!strcmp(s, "waiting")) return AgentState::Waiting;
    if (!strcmp(s, "blocked")) return AgentState::Blocked;
    if (!strcmp(s, "done")) return AgentState::Done;
    if (!strcmp(s, "failed")) return AgentState::Failed;
    return AgentState::Unknown;
}

void apply_time(const cJSON* root) {
    const cJSON* t = cJSON_GetObjectItemCaseSensitive(root, "time");
    if (!cJSON_IsNumber(t)) return;
    m.unix_at_sync = (int64_t)t->valuedouble;
    m.ms_at_sync = hal::millis();
    m.tz_offset = (int32_t)num(root, "tz", 0);
    m.have_time = true;
}

void apply_goals(const cJSON* arr, Change* ch) {
    BUDDY_BIG_DATA static Goal next[kMaxGoals];
    int n = 0;
    const cJSON* g;
    cJSON_ArrayForEach(g, arr) {
        if (n >= kMaxGoals) break;
        Goal& d = next[n];
        memset(&d, 0, sizeof(d));
        copy(d.id, sizeof(d.id), str(g, "id"));
        copy(d.title, sizeof(d.title), str(g, "title"));
        d.done = boolean(g, "done", false);
        d.est = clamp8(num(g, "est", 0), 0, 12);
        d.pomos = clamp8(num(g, "pomos", 0), 0, 99);
        if (d.id[0]) n++;
    }
    if (n != m.n_goals || memcmp(next, m.goals, sizeof(Goal) * n) != 0) {
        memcpy(m.goals, next, sizeof(Goal) * n);
        m.n_goals = n;
        ch->goals = true;
    }
}

void apply_agents(const cJSON* arr, Change* ch) {
    BUDDY_BIG_DATA static Agent next[kMaxAgents];
    int n = 0;
    const uint32_t now = hal::millis();
    const cJSON* a;
    cJSON_ArrayForEach(a, arr) {
        if (n >= kMaxAgents) break;
        Agent& d = next[n];
        memset(&d, 0, sizeof(d));
        copy(d.id, sizeof(d.id), str(a, "id"));
        copy(d.name, sizeof(d.name), str(a, "name"));
        copy(d.kind, sizeof(d.kind), str(a, "kind"));
        d.state = parse_state(str(a, "state"));
        d.since = (uint32_t)num(a, "since", 0);
        if (!d.id[0]) continue;
        if (!d.name[0]) copy(d.name, sizeof(d.name), d.id);

        const Agent* old = nullptr;
        for (int i = 0; i < m.n_agents; i++) {
            if (!strcmp(m.agents[i].id, d.id)) old = &m.agents[i];
        }
        const bool was_attn = old && needs_attention(old->state);
        if (needs_attention(d.state)) {
            if (was_attn) {
                d.needs_ack = old->needs_ack;
                d.attn_ms = old->attn_ms;
                d.reminders = old->reminders;
            } else {
                d.needs_ack = true;
                d.attn_ms = now;
                ch->new_attention++;
                copy(ch->attention_name, sizeof(ch->attention_name), d.name);
            }
        } else if (old && old->state != d.state) {
            // Only transitions seen live count; a fresh snapshot of old
            // finished agents should not set off fanfares.
            if (d.state == AgentState::Done) ch->new_done++;
            if (d.state == AgentState::Failed) ch->new_failed++;
        }
        n++;
    }
    bool changed = n != m.n_agents;
    for (int i = 0; i < n && !changed; i++) {
        changed = strcmp(next[i].id, m.agents[i].id) || strcmp(next[i].name, m.agents[i].name) ||
                  next[i].state != m.agents[i].state || next[i].since != m.agents[i].since;
    }
    memcpy(m.agents, next, sizeof(Agent) * n);
    m.n_agents = n;
    if (changed) ch->agents = true;
}

void apply_settings(const cJSON* s, Change* ch) {
    Settings n = m.settings;
    n.focus_min = clamp8(num(s, "focus_min", n.focus_min), 1, 180);
    n.short_min = clamp8(num(s, "short_min", n.short_min), 1, 60);
    n.long_min = clamp8(num(s, "long_min", n.long_min), 1, 90);
    n.long_every = clamp8(num(s, "long_every", n.long_every), 2, 10);
    n.led = clamp8(num(s, "led", n.led), 0, 100);
    n.sound = boolean(s, "sound", n.sound);
    n.volume = clamp8(num(s, "volume", n.volume), 0, 100);
    n.quiet = boolean(s, "quiet", n.quiet);
    n.quiet_from = clamp8(num(s, "quiet_from", n.quiet_from), 0, 23);
    n.quiet_to = clamp8(num(s, "quiet_to", n.quiet_to), 0, 23);
    const char* fa = str(s, "focus_alerts");
    if (fa) n.focus_alerts = !strcmp(fa, "off") ? 0 : (!strcmp(fa, "full") ? 2 : 1);
    const char* name = str(s, "name");
    if (name && name[0]) copy(n.name, sizeof(n.name), name);
    if (memcmp(&n, &m.settings, sizeof(n)) != 0) {
        m.settings = n;
        ch->settings = true;
    }
}

}  // namespace

bool apply_line(const char* json, Change* ch) {
    static bool hooked = false;
    if (!hooked) {
        cJSON_Hooks hooks = {lv_malloc, lv_free};
        cJSON_InitHooks(&hooks);
        hooked = true;
    }
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    const char* t = str(root, "t");
    bool ok = true;
    if (t && !strcmp(t, "state")) {
        apply_time(root);
        const cJSON* goals = cJSON_GetObjectItemCaseSensitive(root, "goals");
        if (cJSON_IsArray(goals)) apply_goals(goals, ch);
        const cJSON* agents = cJSON_GetObjectItemCaseSensitive(root, "agents");
        if (cJSON_IsArray(agents)) apply_agents(agents, ch);
        const char* note = str(root, "agents_note");
        if (strcmp(m.agents_note, note ? note : "") != 0) {
            copy(m.agents_note, sizeof(m.agents_note), note);
            ch->agents = true;
        }
        const cJSON* settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
        if (cJSON_IsObject(settings)) apply_settings(settings, ch);
        const cJSON* pomos = cJSON_GetObjectItemCaseSensitive(root, "pomos_today");
        if (cJSON_IsNumber(pomos)) m.timer.done_today = (int)pomos->valuedouble;
        m.ever_synced = true;
    } else if (t && !strcmp(t, "ping")) {
        apply_time(root);
        ch->ping = true;
    } else if (t && !strcmp(t, "cmd")) {
        const char* cmd = str(root, "cmd");
        if (cmd && !strcmp(cmd, "focus_start")) {
            ch->cmd_focus = true;
            copy(ch->cmd_goal, sizeof(ch->cmd_goal), str(root, "goal"));
            ch->cmd_minutes = (int)num(root, "min", 0);
        } else if (cmd && !strcmp(cmd, "page")) {
            ch->cmd_page = (int)num(root, "n", -1);
        } else if (cmd && !strcmp(cmd, "stop")) {
            ch->cmd_stop = true;
        } else if (cmd && !strcmp(cmd, "pause")) {
            ch->cmd_pause = true;
        } else if (cmd && !strcmp(cmd, "break_start")) {
            ch->cmd_break = true;
            ch->cmd_minutes = (int)num(root, "min", 0);
        }
    } else {
        ok = false;
    }
    cJSON_Delete(root);
    return ok;
}

Goal* find_goal(const char* id) {
    if (!id || !id[0]) return nullptr;
    for (int i = 0; i < m.n_goals; i++) {
        if (!strcmp(m.goals[i].id, id)) return &m.goals[i];
    }
    return nullptr;
}

Goal* next_goal() {
    for (int i = 0; i < m.n_goals; i++) {
        if (!m.goals[i].done) return &m.goals[i];
    }
    return nullptr;
}

int goals_done() {
    int n = 0;
    for (int i = 0; i < m.n_goals; i++) n += m.goals[i].done;
    return n;
}

int count_agents(AgentState s) {
    int n = 0;
    for (int i = 0; i < m.n_agents; i++) n += m.agents[i].state == s;
    return n;
}

int count_attention() { return count_agents(AgentState::Waiting) + count_agents(AgentState::Blocked); }

int count_unacked() {
    int n = 0;
    for (int i = 0; i < m.n_agents; i++) n += needs_attention(m.agents[i].state) && m.agents[i].needs_ack;
    return n;
}

LocalTime local_time() {
    LocalTime lt = {};
    if (!m.have_time) return lt;
    int64_t t = m.unix_at_sync + (hal::millis() - m.ms_at_sync) / 1000 + m.tz_offset;
    int64_t days = t / 86400;
    int rem = (int)(t % 86400);
    lt.valid = true;
    lt.hour = rem / 3600;
    lt.minute = (rem % 3600) / 60;
    lt.second = rem % 60;
    lt.wday = (int)((days + 4) % 7);  // 1970-01-01 was a Thursday
    // Civil date from days since epoch (Howard Hinnant's algorithm).
    int64_t z = days + 719468;
    int64_t era = z / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    lt.mday = doy - (153 * mp + 2) / 5 + 1;
    lt.month = mp < 10 ? mp + 3 : mp - 9;
    return lt;
}

bool quiet_now() {
    if (!m.settings.quiet) return false;
    LocalTime lt = local_time();
    if (!lt.valid) return false;
    const int from = m.settings.quiet_from, to = m.settings.quiet_to;
    if (from == to) return false;
    return from < to ? (lt.hour >= from && lt.hour < to) : (lt.hour >= from || lt.hour < to);
}

// ---- timer ----

void timer_start_focus(const char* goal_id, int minutes) {
    Timer& t = m.timer;
    if (minutes <= 0) minutes = m.settings.focus_min;
    t.phase = Phase::Focus;
    t.paused = false;
    copy(t.goal, sizeof(t.goal), find_goal(goal_id) ? goal_id : "");
    t.minutes = (uint8_t)minutes;
    t.total_ms = (uint32_t)minutes * 60000u;
    t.elapsed_ms = 0;
    t.last_ms = hal::millis();
}

void timer_start_break(int minutes) {
    Timer& t = m.timer;
    if (minutes <= 0) minutes = next_break_minutes();
    t.phase = Phase::Break;
    t.paused = false;
    t.minutes = (uint8_t)minutes;
    t.total_ms = (uint32_t)minutes * 60000u;
    t.elapsed_ms = 0;
    t.last_ms = hal::millis();
}

void timer_set_paused(bool paused) {
    Timer& t = m.timer;
    if (t.phase != Phase::Focus && t.phase != Phase::Break) return;
    t.paused = paused;
    t.last_ms = hal::millis();
}

void timer_stop() {
    m.timer.phase = Phase::Idle;
    m.timer.paused = false;
    m.timer.elapsed_ms = 0;
}

Phase timer_tick() {
    Timer& t = m.timer;
    const uint32_t now = hal::millis();
    if (t.phase != Phase::Focus && t.phase != Phase::Break) return Phase::Idle;
    // Elapsed time comes from the clock, never from frame counts.
    if (!t.paused) t.elapsed_ms += now - t.last_ms;
    t.last_ms = now;
    if (t.elapsed_ms < t.total_ms) return Phase::Idle;
    t.elapsed_ms = t.total_ms;
    if (t.phase == Phase::Focus) {
        t.phase = Phase::FocusDone;
        t.done_today++;
        return Phase::Focus;
    }
    t.phase = Phase::Idle;
    return Phase::Break;
}

uint32_t timer_remaining_ms() {
    const Timer& t = m.timer;
    if (t.phase != Phase::Focus && t.phase != Phase::Break) return 0;
    return t.total_ms - t.elapsed_ms;
}

int next_break_minutes() {
    const Settings& s = m.settings;
    const bool is_long = m.timer.done_today > 0 && m.timer.done_today % s.long_every == 0;
    return is_long ? s.long_min : s.short_min;
}

// ---- breathing ----

void breathe_start(int pattern, int minutes) {
    m.breathe.active = true;
    m.breathe.pattern = (uint8_t)(pattern < 0 || pattern > 2 ? 0 : pattern);
    m.breathe.minutes = (uint8_t)minutes;
    m.breathe.start_ms = hal::millis();
}

void breathe_stop() { m.breathe.active = false; }

BreathNow breathe_now() {
    BreathNow b = {0, BreathStage::In, 0, 0};
    const Breathe& br = m.breathe;
    if (!br.active) return b;
    const BreathPattern& p = kPatterns[br.pattern];
    const uint32_t el = hal::millis() - br.start_ms;
    const uint32_t total = (uint32_t)br.minutes * 60000u;
    b.remain_ms = el >= total ? 0 : total - el;
    const uint32_t cycle = (uint32_t)(p.in + p.hold_in + p.out + p.hold_out) * 1000u;
    uint32_t t = el % cycle;
    const uint32_t d[4] = {p.in * 1000u, p.hold_in * 1000u, p.out * 1000u, p.hold_out * 1000u};
    int stage = 0;
    while (stage < 3 && t >= d[stage]) t -= d[stage++];
    b.stage = (BreathStage)stage;
    b.stage_left_s = (int)((d[stage] - t + 999) / 1000);
    const float f = d[stage] ? (float)t / (float)d[stage] : 1.0f;
    const float ease = 0.5f - 0.5f * cosf(f * 3.14159265f);
    switch (b.stage) {
        case BreathStage::In: b.level = ease; break;
        case BreathStage::HoldIn: b.level = 1.0f; break;
        case BreathStage::Out: b.level = 1.0f - ease; break;
        case BreathStage::HoldOut: b.level = 0.0f; break;
    }
    return b;
}

void json_escape(const char* in, char* out, int out_size) {
    int o = 0;
    for (; *in && o < out_size - 7; in++) {
        const unsigned char ch = (unsigned char)*in;
        if (ch == '"' || ch == '\\') {
            out[o++] = '\\';
            out[o++] = (char)ch;
        } else if (ch < 0x20) {
            o += snprintf(out + o, 7, "\\u%04x", ch);
        } else {
            out[o++] = (char)ch;
        }
    }
    out[o] = 0;
}

}  // namespace model
