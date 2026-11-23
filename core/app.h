// App controller: navigation and the actions screens can trigger.
#pragma once
#include "mascot.h"
#include "model.h"

namespace app {

enum Page : int { Home, Goals, Focus, Agents, Breathe, kPageCount };

void go(Page p);
Page page();

void start_focus(const char* goal_id, int minutes);
void start_break();
void set_paused(bool paused);
void stop_timer();        // abandon a running session, or dismiss the "done" prompt
void toggle_goal(const char* id);
void start_breathe(int pattern, int minutes);
void stop_breathe();
void ack_alerts();
void boop();              // the mascot was tapped

}  // namespace app

// Each screen builds into its page container once, then is told when it is
// shown, when the model changed and once per frame while visible.
namespace home {
void build(lv_obj_t* page);
void refresh();
void tick();
Mascot& mascot();
}
namespace goals {
void build(lv_obj_t* page);
void refresh();
}
namespace focus {
void build(lv_obj_t* page);
void refresh();
void tick();
Mascot& mascot();
const char* picked_goal();
void pick_goal(const char* id);
}
namespace agents {
void build(lv_obj_t* page);
void refresh();
void tick();
}
namespace breathe {
void build(lv_obj_t* page);
void refresh();
void tick();
void note_completed();  // the last session ran to its end
Mascot& mascot();
}
