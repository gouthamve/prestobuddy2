// Garder, the little guardsman: a rig of sprite parts animated procedurally.
#pragma once
#include "gen/assets.h"
#include "lvgl.h"

class Mascot {
public:
    enum class Mood : uint8_t { Idle, Guard, AtEase, Sleepy, Meditate };
    enum class Act : uint8_t { None, Celebrate, Salute, Alert, Hop, March };

    // feet_x / feet_y: where the centre of his feet stand, in parent coordinates.
    void create(lv_obj_t* parent, const mascot_set_t* set, int feet_x, int feet_y);
    void set_mood(Mood m);
    Mood mood() const { return mood_; }
    void act(Act a);
    void set_breath(float level) { breath_ = level; }
    // Advances the animation. Call only while he is on screen.
    void tick();
    lv_obj_t* obj() const { return root_; }

private:
    struct Pose {
        float root_x = 0, jump = 0, bob = 0, head_dy = 0, hat_dy = 0, hat_rot = 0, hat_sq = 1;
        float arm_l = 0, arm_r = 0;            // degrees raised away from the body
        float leg_l = 0, leg_r = 0;            // degrees splayed outwards
        float lift_l = 0, lift_r = 0;          // leg lift in design px
        float eye_dx = 0, eye_dy = 0, sit = 0;
    };
    void mood_pose(float t, Pose& p, int& eyes, int& mouth);
    void act_pose(float ta, Pose& p, int& eyes, int& mouth);
    void apply(const Pose& p, int eyes, int mouth);
    void place(int part, float dx, float dy);
    void update_extras(float dt);

    const mascot_set_t* set_ = nullptr;
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* shadow_ = nullptr;
    lv_obj_t* img_[MASCOT_PART_COUNT] = {};
    lv_obj_t* bubble_ = nullptr;
    lv_obj_t* zzz_[3] = {};
    lv_obj_t* confetti_[14] = {};
    float conf_x_[14] = {}, conf_y_[14] = {}, conf_vx_[14] = {}, conf_vy_[14] = {};
    bool confetti_on_ = false;

    int base_x_ = 0, base_y_ = 0, ox_ = 0, oy_ = 0, pad_ = 0;
    Mood mood_ = Mood::Idle;
    Act act_ = Act::None;
    uint32_t act_start_ = 0, last_tick_ = 0;
    Pose cur_;
    bool first_ = true;
    float breath_ = 0;
    int eyes_shown_ = -1, mouth_shown_ = -1;

    // Idle behaviour state
    uint32_t next_blink_ = 0, blink_until_ = 0;
    uint32_t next_look_ = 0, look_until_ = 0;
    float look_dx_ = 0, look_dy_ = 0;
    uint32_t next_fidget_ = 0, fidget_until_ = 0;
    uint32_t rng_ = 0x2545F491;
    uint32_t rand(uint32_t n);
};
