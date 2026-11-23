// Rear LEDs and piezo: effect engine and melody player.
#pragma once
#include <stdint.h>

namespace fx {

// The steady LED state, chosen from what the app is doing.
enum class Base : uint8_t { Off, Idle, Focus, Paused, Break, Breathe };
// Short overlays that play once on top of the base.
enum class Flash : uint8_t { None, Startup, Celebrate, AgentDone, AgentFail };

void set_base(Base b);
void set_progress(float p);    // Focus: 0..1, fills the LEDs left to right
void set_breath(float level);  // Breathe: 0..1
void flash(Flash f);
// While true an amber alert runs on top of everything: a lively chase for
// the first seconds after it (re)starts, then a slow pulse until cleared.
void set_alert(bool pending, bool restart);

enum class Tune : uint8_t {
    Startup, FocusStart, FocusDone, BreakDone, GoalDone,
    AgentChirp, AgentChirpSoft, AgentDone, AgentFail,
    Boop, Tap, BreathIn, BreathOut, BreatheEnd,
};
void play(Tune t);

void tick();

}  // namespace fx
