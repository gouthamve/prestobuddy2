// Everything the device knows: goals, agents and settings mirrored from
// buddyd, plus the local timer and breathing session.
#pragma once
#include <stdint.h>

namespace model {

constexpr int kMaxGoals = 24;
constexpr int kMaxAgents = 24;

struct Goal {
    char id[20];
    char title[100];
    bool done;
    uint8_t est;    // estimated pomodoros (0 = none)
    uint8_t pomos;  // completed pomodoros
};

enum class AgentState : uint8_t { Working, Waiting, Blocked, Done, Failed, Unknown };

struct Agent {
    char id[48];
    char name[64];
    char kind[20];
    AgentState state;
    uint32_t since;        // unix seconds when it entered this state
    // Local attention tracking
    bool needs_ack;
    uint32_t attn_ms;      // hal::millis() when it started needing attention
    uint8_t reminders;     // how many reminder chirps have played
};

inline bool needs_attention(AgentState s) { return s == AgentState::Waiting || s == AgentState::Blocked; }

struct Settings {
    uint8_t focus_min = 25;
    uint8_t short_min = 5;
    uint8_t long_min = 15;
    uint8_t long_every = 4;
    uint8_t backlight = 80;    // screen, 5-100
    uint8_t quiet_backlight = 20;  // screen during quiet hours, 5-100
    uint8_t led = 60;          // 0-100
    bool sound = true;
    uint8_t volume = 50;       // 0-100
    bool quiet = false;        // quiet hours enabled
    uint8_t quiet_from = 22;   // hour
    uint8_t quiet_to = 7;
    uint8_t focus_alerts = 1;  // during focus: 0 silent (LEDs only), 1 soft chirp, 2 full
    char name[24] = "Garder";
};

// ---- timer ----
enum class Phase : uint8_t { Idle, Focus, FocusDone, Break };

struct Timer {
    Phase phase = Phase::Idle;
    bool paused = false;
    char goal[20] = "";        // goal id, empty for a free session
    uint32_t total_ms = 0;
    uint32_t elapsed_ms = 0;
    uint32_t last_ms = 0;
    uint8_t minutes = 0;
    int done_today = 0;
};

// ---- breathing ----
struct BreathPattern {
    const char* name;
    uint8_t in, hold_in, out, hold_out;  // seconds
};
extern const BreathPattern kPatterns[3];

struct Breathe {
    bool active = false;
    uint8_t pattern = 0;
    uint8_t minutes = 3;
    uint32_t start_ms = 0;
};

enum class BreathStage : uint8_t { In, HoldIn, Out, HoldOut };
struct BreathNow {
    float level;        // 0 (empty) .. 1 (full), eased
    BreathStage stage;
    int stage_left_s;   // whole seconds left in this stage
    uint32_t remain_ms; // of the whole session
};

struct Model {
    Goal goals[kMaxGoals];
    int n_goals = 0;
    Agent agents[kMaxAgents];
    int n_agents = 0;
    char agents_note[64] = "";  // e.g. "Orca CLI not found"
    Settings settings;
    Timer timer;
    Breathe breathe;

    bool have_time = false;
    int64_t unix_at_sync = 0;
    uint32_t ms_at_sync = 0;
    int32_t tz_offset = 0;

    bool ever_synced = false;
};
extern Model m;

// What changed when a line from buddyd was applied.
struct Change {
    bool goals = false;
    bool agents = false;
    bool settings = false;
    int new_attention = 0;  // agents that just started needing attention
    int new_done = 0;
    int new_failed = 0;
    char attention_name[64] = "";
    // Remote commands
    bool cmd_focus = false;
    char cmd_goal[20] = "";
    int cmd_minutes = 0;
    bool cmd_stop = false;
    int cmd_page = -1;  // show this page (0 home .. 4 breathe)
    bool cmd_pause = false;
    bool cmd_break = false;
    bool ping = false;
};
bool apply_line(const char* json, Change* out);

Goal* find_goal(const char* id);
Goal* next_goal();       // first goal not done, or nullptr
int goals_done();
int count_agents(AgentState s);
int count_attention();   // agents waiting or blocked
int count_unacked();

struct LocalTime {
    bool valid;
    int hour, minute, second, wday, mday, month;  // wday 0 = Sunday, month 1-12
};
LocalTime local_time();
bool quiet_now();

// Timer
void timer_start_focus(const char* goal_id, int minutes);
void timer_start_break(int minutes);
void timer_set_paused(bool paused);
void timer_stop();
// Advances the timer. Returns the phase that just completed (Focus or
// Break), or Idle if nothing completed this tick.
Phase timer_tick();
uint32_t timer_remaining_ms();
int next_break_minutes();

// Breathing
void breathe_start(int pattern, int minutes);
void breathe_stop();
BreathNow breathe_now();

// Escapes a string for embedding in a JSON string literal.
void json_escape(const char* in, char* out, int out_size);

}  // namespace model
