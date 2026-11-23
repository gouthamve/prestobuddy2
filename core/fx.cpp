#include "fx.h"

#include <math.h>

#include "hal.h"
#include "model.h"

namespace fx {
namespace {

constexpr float kPi = 3.14159265f;
constexpr int N = hal::kNumLeds;

Base base = Base::Idle;
float progress = 0, breath = 0;
Flash cur_flash = Flash::None;
uint32_t flash_start = 0;
bool alert = false;
uint32_t alert_start = 0;

// The LEDs wrap round the back; seen from the front, index 6 is bottom-left
// and index 0 bottom-right. pos() maps an LED to 0 (left) .. 1 (right).
inline float pos(int i) { return (float)(N - 1 - i) / (float)(N - 1); }

struct Rgb {
    float r, g, b;
};
inline Rgb scale(Rgb c, float k) { return {c.r * k, c.g * k, c.b * k}; }
inline Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

Rgb hsv(float h, float s, float v) {
    h = h - floorf(h);
    const float i = floorf(h * 6), f = h * 6 - i;
    const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch ((int)i % 6) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

const Rgb kGold = {1.0f, 0.62f, 0.18f};
const Rgb kCoral = {1.0f, 0.30f, 0.20f};
const Rgb kGreen = {0.20f, 1.0f, 0.42f};
const Rgb kTeal = {0.10f, 0.85f, 0.80f};
const Rgb kAmber = {1.0f, 0.55f, 0.0f};
const Rgb kRed = {1.0f, 0.05f, 0.10f};

Rgb base_color(int i, float t) {
    switch (base) {
        case Base::Idle:
            return scale(kGold, 0.05f + 0.03f * sinf(t * 2 * kPi / 6.0f));
        case Base::Focus: {
            // Fill from the left as the session progresses; the leading LED fades in.
            const float lit = progress * N - (pos(i) * (N - 1));
            const float k = lit <= 0 ? 0.0f : (lit >= 1 ? 1.0f : lit);
            return scale(kCoral, 0.05f + 0.45f * k);
        }
        case Base::Paused:
            return scale(kAmber, 0.12f);
        case Base::Break:
            return scale(kGreen, 0.14f + 0.22f * (0.5f + 0.5f * sinf(t * 2 * kPi / 5.0f)));
        case Base::Breathe:
            return scale(kTeal, 0.04f + 0.55f * breath);
        default:
            return {0, 0, 0};
    }
}

// ---- melodies ----
struct Note {
    uint16_t hz;
    uint16_t ms;
};
#define TUNE(name, ...) const Note name[] = {__VA_ARGS__, {0, 0}}
TUNE(kStartup, {523, 90}, {659, 90}, {784, 90}, {1047, 170});
TUNE(kFocusStart, {784, 100}, {0, 40}, {1047, 170});
TUNE(kFocusDone, {784, 140}, {784, 70}, {784, 70}, {1047, 200}, {0, 40}, {1319, 140}, {1047, 100}, {1319, 100}, {1568, 340});
TUNE(kBreakDone, {1319, 120}, {1047, 120}, {784, 220});
TUNE(kGoalDone, {1047, 80}, {1319, 80}, {1568, 80}, {2093, 200});
TUNE(kChirp, {1319, 70}, {0, 50}, {1760, 120});
TUNE(kAgentDone, {1047, 80}, {1568, 150});
TUNE(kAgentFail, {880, 160}, {698, 260});
TUNE(kBoop, {880, 45});
TUNE(kTap, {1400, 14});
TUNE(kBreathIn, {660, 70});
TUNE(kBreathOut, {494, 70});
TUNE(kBreatheEnd, {784, 220}, {1047, 220}, {1319, 420});

const Note* playing = nullptr;
int note_index = 0;
uint32_t note_until = 0;
float tune_gain = 1.0f;

}  // namespace

void set_base(Base b) { base = b; }
void set_progress(float p) { progress = p < 0 ? 0 : (p > 1 ? 1 : p); }
void set_breath(float level) { breath = level; }

void flash(Flash f) {
    cur_flash = f;
    flash_start = hal::millis();
}

void set_alert(bool pending, bool restart) {
    if (pending && (!alert || restart)) alert_start = hal::millis();
    alert = pending;
}

void play(Tune t) {
    const Note* n = nullptr;
    float gain = 1.0f;
    switch (t) {
        case Tune::Startup: n = kStartup; break;
        case Tune::FocusStart: n = kFocusStart; break;
        case Tune::FocusDone: n = kFocusDone; break;
        case Tune::BreakDone: n = kBreakDone; break;
        case Tune::GoalDone: n = kGoalDone; break;
        case Tune::AgentChirp: n = kChirp; break;
        case Tune::AgentChirpSoft: n = kChirp; gain = 0.3f; break;
        case Tune::AgentDone: n = kAgentDone; gain = 0.7f; break;
        case Tune::AgentFail: n = kAgentFail; break;
        case Tune::Boop: n = kBoop; gain = 0.6f; break;
        case Tune::Tap: n = kTap; gain = 0.35f; break;
        case Tune::BreathIn: n = kBreathIn; gain = 0.25f; break;
        case Tune::BreathOut: n = kBreathOut; gain = 0.25f; break;
        case Tune::BreatheEnd: n = kBreatheEnd; gain = 0.5f; break;
    }
    // A tap never cuts off a melody that is already playing.
    if (t == Tune::Tap && playing) return;
    playing = n;
    note_index = -1;
    note_until = 0;
    tune_gain = gain;
}

void tick() {
    const uint32_t now = hal::millis();
    const float t = now / 1000.0f;
    const model::Settings& st = model::m.settings;
    const bool quiet = model::quiet_now();

    // ---- sound ----
    if (playing && now >= note_until) {
        note_index++;
        const Note& n = playing[note_index];
        if (n.ms == 0) {
            playing = nullptr;
            hal::tone(0, 0);
        } else {
            const bool audible = st.sound && !quiet && st.volume > 0 && n.hz > 0;
            const float vol = st.volume * 2.55f * tune_gain;
            hal::tone(audible ? n.hz : 0, audible ? (uint8_t)(vol < 1 ? 1 : vol) : 0);
            note_until = now + n.ms;
        }
    }

    // ---- LEDs ----
    uint8_t out[N][3];
    const float fl = (now - flash_start) / 1000.0f;
    const float al = (now - alert_start) / 1000.0f;
    for (int i = 0; i < N; i++) {
        Rgb c = base_color(i, t);

        if (alert) {
            float k;
            if (al < 6.0f) {
                // A bright spot racing back and forth.
                const float head = 0.5f + 0.5f * sinf(al * 2 * kPi * 1.1f);
                const float d = fabsf(pos(i) - head);
                k = 0.18f + 0.82f * (d < 0.33f ? 1.0f - d / 0.33f : 0.0f);
            } else {
                k = 0.10f + 0.40f * (0.5f + 0.5f * sinf(al * 2 * kPi / 2.2f));
            }
            c = scale(kAmber, k);
        }

        switch (cur_flash) {
            case Flash::Startup: {
                const float head = fl / 1.2f * 1.4f - 0.2f;
                const float d = fabsf(pos(i) - head);
                const float k = d < 0.3f ? 1.0f - d / 0.3f : 0;
                c = mix(c, kGold, k * 0.9f);
                if (fl > 1.4f) cur_flash = Flash::None;
                break;
            }
            case Flash::Celebrate: {
                const float env = fl < 2.2f ? 1.0f : (fl < 3.0f ? 1.0f - (fl - 2.2f) / 0.8f : 0.0f);
                c = mix(c, scale(hsv(pos(i) * 0.8f - fl * 0.9f, 1.0f, 0.9f), 1.0f), env);
                if (fl > 3.0f) cur_flash = Flash::None;
                break;
            }
            case Flash::AgentDone: {
                const float head = fl / 1.0f * 1.5f - 0.25f;
                const float d = fabsf(pos(i) - head);
                c = mix(c, kGreen, d < 0.35f ? (1.0f - d / 0.35f) * 0.8f : 0);
                if (fl > 1.2f) cur_flash = Flash::None;
                break;
            }
            case Flash::AgentFail: {
                const float k = fl < 1.8f ? 0.5f + 0.5f * sinf(fl * 2 * kPi * 1.67f - kPi / 2) : 0;
                c = mix(c, kRed, k * 0.85f);
                if (fl > 1.8f) cur_flash = Flash::None;
                break;
            }
            default:
                break;
        }

        const float bright = quiet ? 0.0f : st.led / 100.0f;
        const float ch[3] = {c.r, c.g, c.b};
        for (int k = 0; k < 3; k++) {
            float v = ch[k] * bright;
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            out[i][k] = (uint8_t)(v * v * 255.0f + 0.5f);  // gamma 2 for even fades
        }
    }
    hal::set_leds(out);
}

}  // namespace fx
