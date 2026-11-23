#include "mascot.h"

#include <math.h>

#include "hal.h"
#include "theme.h"

namespace {

constexpr float kPi = 3.14159265f;

enum { kEyesOpen = 0, kEyesWide, kEyesHalf, kEyesClosed, kEyesHappy, kEyesCount };
enum { kMouthSmile = 0, kMouthGrin, kMouthO, kMouthFlat, kMouthCount };

const uint32_t kConfettiColors[] = {theme::kGold, theme::kFocus, theme::kBreak, theme::kTeal, 0xFFFFFF, 0xB58CFF};

inline float lerp_to(float cur, float target, float k) { return cur + (target - cur) * k; }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

}  // namespace

uint32_t Mascot::rand(uint32_t n) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return n ? rng_ % n : rng_;
}

void Mascot::create(lv_obj_t* parent, const mascot_set_t* set, int feet_x, int feet_y) {
    set_ = set;
    parent_ = parent;
    const float s = set->scale;
    pad_ = (int)(34 * s);
    ox_ = (int)lroundf(120 * s);
    oy_ = (int)lroundf(347 * s);
    base_x_ = feet_x - (pad_ + ox_);
    base_y_ = feet_y - (pad_ + oy_);
    rng_ ^= (uint32_t)feet_x * 2654435761u + (uint32_t)(s * 1000);

    const mascot_part_t& sh = set->parts[MASCOT_SHADOW];
    shadow_ = lv_image_create(parent);
    lv_image_set_src(shadow_, sh.img);
    lv_obj_set_pos(shadow_, feet_x + sh.x, feet_y + sh.y);

    root_ = theme::box(parent, base_x_, base_y_, set->width + 2 * pad_, set->height + 2 * pad_);
    lv_obj_set_flag(root_, LV_OBJ_FLAG_OVERFLOW_VISIBLE, true);

    // Back to front. Arms go last so a raised hand passes in front of the hat.
    static const int order[] = {MASCOT_LEG_L,      MASCOT_LEG_R,     MASCOT_BODY,       MASCOT_HEAD,
                                MASCOT_EYES_OPEN,  MASCOT_EYES_WIDE, MASCOT_EYES_HALF,  MASCOT_EYES_CLOSED,
                                MASCOT_EYES_HAPPY, MASCOT_MOUTH_SMILE, MASCOT_MOUTH_GRIN, MASCOT_MOUTH_O,
                                MASCOT_MOUTH_FLAT, MASCOT_HAT,       MASCOT_ARM_L,      MASCOT_ARM_R};
    for (int id : order) {
        const mascot_part_t& p = set->parts[id];
        lv_obj_t* img = lv_image_create(root_);
        lv_image_set_src(img, p.img);
        lv_image_set_pivot(img, p.px, p.py);
        lv_obj_set_pos(img, pad_ + ox_ + p.x, pad_ + oy_ + p.y);
        img_[id] = img;
    }
    for (int i = 0; i < kEyesCount; i++) lv_obj_set_hidden(img_[MASCOT_EYES_OPEN + i], true);
    for (int i = 0; i < kMouthCount; i++) lv_obj_set_hidden(img_[MASCOT_MOUTH_SMILE + i], true);

    // "!" bubble for alerts.
    const int bs = (int)(46 * s);
    bubble_ = theme::card(root_, 0, 0, bs, bs, theme::kAmber, LV_RADIUS_CIRCLE);
    lv_obj_t* bang = theme::label(bubble_, "!", s > 0.6f ? &font_ui_24 : &font_ui_16, theme::kInk);
    lv_obj_center(bang);
    lv_obj_set_hidden(bubble_, true);

    // "z z z" for dozing.
    const lv_font_t* zf[3] = {&font_ui_16, &font_ui_20, &font_ui_24};
    for (int i = 0; i < 3; i++) {
        zzz_[i] = theme::label(root_, "z", zf[s > 0.6f ? i : 0], theme::kMuted);
        lv_obj_set_hidden(zzz_[i], true);
    }

    for (int i = 0; i < 14; i++) {
        const int sz = 6 + (int)rand(5);
        confetti_[i] = theme::card(parent, 0, 0, sz, sz, kConfettiColors[i % 6], i % 2 ? 2 : LV_RADIUS_CIRCLE);
        lv_obj_set_hidden(confetti_[i], true);
        lv_obj_set_flag(confetti_[i], LV_OBJ_FLAG_IGNORE_LAYOUT, true);
    }

    last_tick_ = hal::millis();
    next_blink_ = last_tick_ + 1500 + rand(2000);
    next_look_ = last_tick_ + 3000 + rand(3000);
    next_fidget_ = last_tick_ + 14000 + rand(10000);
}

void Mascot::set_mood(Mood m) { mood_ = m; }

void Mascot::act(Act a) {
    act_ = a;
    act_start_ = hal::millis();
    if (a == Act::Celebrate) {
        confetti_on_ = true;
        const float s = set_->scale;
        for (int i = 0; i < 14; i++) {
            conf_x_[i] = base_x_ + pad_ + ox_ + ((int)rand(60) - 30) * s;
            conf_y_[i] = base_y_ + pad_ + oy_ - 250 * s;
            conf_vx_[i] = ((int)rand(520) - 260) * s;
            conf_vy_[i] = -(260 + (int)rand(300)) * s;
            lv_obj_set_hidden(confetti_[i], false);
        }
    }
}

void Mascot::mood_pose(float t, Pose& p, int& eyes, int& mouth) {
    const uint32_t now = hal::millis();
    eyes = kEyesOpen;
    mouth = kMouthSmile;

    // Glances: pick a direction now and then, hold it, look back.
    if (now >= next_look_) {
        look_dx_ = ((int)rand(3) - 1) * 3.5f;
        look_dy_ = rand(4) == 0 ? -1.5f : 0.0f;
        look_until_ = now + 900 + rand(900);
        next_look_ = look_until_ + 2500 + rand(mood_ == Mood::Guard ? 7000 : 4000);
    }
    if (now < look_until_) {
        p.eye_dx = look_dx_;
        p.eye_dy = look_dy_;
    }

    switch (mood_) {
        case Mood::Idle: {
            p.bob = 1.6f * sinf(t * 2 * kPi / 3.6f);
            p.hat_dy = 1.0f * sinf(t * 2 * kPi / 3.6f - 0.7f);
            // Every so often he marches on the spot for a couple of seconds.
            if (now >= next_fidget_) {
                fidget_until_ = now + 2200;
                next_fidget_ = fidget_until_ + 18000 + rand(14000);
            }
            if (now < fidget_until_) {
                const float ph = t * 2 * kPi * 2.2f;
                p.lift_l = fmaxf(0, sinf(ph)) * 9;
                p.lift_r = fmaxf(0, -sinf(ph)) * 9;
                p.arm_l = -sinf(ph) * 16;
                p.arm_r = sinf(ph) * 16;
                p.bob = fabsf(sinf(ph)) * 2.5f;
            }
            break;
        }
        case Mood::Guard:
            // At attention: nearly still, so he doesn't pull focus.
            p.bob = 0.7f * sinf(t * 2 * kPi / 4.5f);
            mouth = kMouthFlat;
            break;
        case Mood::AtEase:
            p.bob = 2.0f * sinf(t * 2 * kPi / 3.0f);
            p.root_x = 3.0f * sinf(t * 2 * kPi / 5.0f);
            p.arm_l = 12;
            p.arm_r = 12;
            p.hat_rot = 6;
            if (fmodf(t, 7.0f) < 1.6f) eyes = kEyesHappy;
            break;
        case Mood::Sleepy:
            p.bob = 2.5f * sinf(t * 2 * kPi / 5.2f);
            p.head_dy = 3;
            p.hat_dy = 19;  // slides down over his eyes
            p.hat_rot = -4;
            p.eye_dx = p.eye_dy = 0;
            eyes = kEyesClosed;
            mouth = kMouthFlat;
            break;
        case Mood::Meditate:
            p.sit = 1;
            p.bob = -3.0f * breath_;
            p.hat_dy = -5.0f * breath_;
            p.arm_l = 22;
            p.arm_r = 22;
            p.eye_dx = p.eye_dy = 0;
            eyes = kEyesClosed;
            break;
    }

    if (eyes == kEyesOpen) {
        if (now >= next_blink_) {
            blink_until_ = now + 130;
            next_blink_ = now + 2200 + rand(3200);
        }
        if (now < blink_until_) eyes = kEyesClosed;
    }
}

void Mascot::act_pose(float ta, Pose& p, int& eyes, int& mouth) {
    switch (act_) {
        case Act::Celebrate: {
            if (ta > 2.4f) { act_ = Act::None; break; }
            const float hop = fabsf(sinf(kPi * ta / 0.8f));
            p.jump = 34 * hop;
            p.arm_l = 150 + 14 * sinf(ta * 18);
            p.arm_r = 150 - 14 * sinf(ta * 18);
            p.hat_dy = -11 * hop;  // the hat floats up at the top of each jump
            p.hat_sq = hop < 0.12f ? 0.86f : 1.0f;
            p.sit = 0;
            eyes = kEyesHappy;
            mouth = kMouthGrin;
            break;
        }
        case Act::Salute: {
            if (ta > 1.5f) { act_ = Act::None; break; }
            const float up = ta < 1.2f ? 1.0f : 1.0f - (ta - 1.2f) / 0.3f;
            p.arm_r = 132 * up;
            p.jump = ta < 0.25f ? 8 * sinf(kPi * ta / 0.25f) : 0;
            p.bob = -1.5f * up;
            eyes = kEyesOpen;
            mouth = kMouthSmile;
            break;
        }
        case Act::Alert: {
            if (ta > 2.4f) { act_ = Act::None; break; }
            p.jump = ta < 0.9f ? 18 * fabsf(sinf(kPi * ta / 0.45f)) : 0;
            p.arm_l = 140 + 24 * sinf(ta * 2 * kPi * 4.5f);
            p.sit = 0;
            p.hat_dy = 0;
            eyes = kEyesWide;
            mouth = kMouthO;
            break;
        }
        case Act::Hop: {
            if (ta > 0.5f) { act_ = Act::None; break; }
            p.jump = 16 * sinf(kPi * ta / 0.5f);
            eyes = kEyesHappy;
            break;
        }
        case Act::March: {
            if (ta > 2.6f) { act_ = Act::None; break; }
            const float ph = ta * 2 * kPi * 2.2f;
            p.lift_l = fmaxf(0, sinf(ph)) * 10;
            p.lift_r = fmaxf(0, -sinf(ph)) * 10;
            p.arm_l = -sinf(ph) * 20;
            p.arm_r = sinf(ph) * 20;
            p.bob = fabsf(sinf(ph)) * 2.5f;
            p.root_x = 26 * sinf(ta * 2 * kPi / 2.6f);
            p.sit = 0;
            mouth = kMouthSmile;
            break;
        }
        default:
            break;
    }
}

void Mascot::place(int part, float dx, float dy) {
    const mascot_part_t& p = set_->parts[part];
    const float s = set_->scale;
    lv_obj_set_pos(img_[part], pad_ + ox_ + p.x + (int)lroundf(dx * s), pad_ + oy_ + p.y + (int)lroundf(dy * s));
}

void Mascot::apply(const Pose& p, int eyes, int mouth) {
    const float s = set_->scale;
    const float sit_drop = 44 * p.sit;
    lv_obj_set_pos(root_, base_x_ + (int)lroundf(p.root_x * s), base_y_ + (int)lroundf((sit_drop - p.jump) * s));

    const float up = p.bob;
    place(MASCOT_BODY, 0, up);
    place(MASCOT_ARM_L, 0, up);
    place(MASCOT_ARM_R, 0, up);
    place(MASCOT_HEAD, 0, up + p.head_dy);
    place(MASCOT_HAT, 0, up + p.head_dy + p.hat_dy);
    place(MASCOT_LEG_L, 0, -p.lift_l);
    place(MASCOT_LEG_R, 0, -p.lift_r);

    // Rotations are quantised so tiny changes don't force a redraw.
    lv_image_set_rotation(img_[MASCOT_ARM_L], (int)lroundf(p.arm_l / 2) * 20);
    lv_image_set_rotation(img_[MASCOT_ARM_R], -(int)lroundf(p.arm_r / 2) * 20);
    lv_image_set_rotation(img_[MASCOT_LEG_L], (int)lroundf((p.leg_l + 78 * p.sit) / 2) * 20);
    lv_image_set_rotation(img_[MASCOT_LEG_R], -(int)lroundf((p.leg_r + 78 * p.sit) / 2) * 20);
    lv_image_set_rotation(img_[MASCOT_HAT], (int)lroundf(p.hat_rot) * 10);
    lv_image_set_scale_y(img_[MASCOT_HAT], p.hat_sq > 0.99f ? 256 : (int)(256 * p.hat_sq));

    if (eyes != eyes_shown_) {
        for (int i = 0; i < kEyesCount; i++) lv_obj_set_hidden(img_[MASCOT_EYES_OPEN + i], i != eyes);
        eyes_shown_ = eyes;
    }
    if (mouth != mouth_shown_) {
        for (int i = 0; i < kMouthCount; i++) lv_obj_set_hidden(img_[MASCOT_MOUTH_SMILE + i], i != mouth);
        mouth_shown_ = mouth;
    }
    place(MASCOT_EYES_OPEN + eyes, p.eye_dx, up + p.head_dy + p.eye_dy);
    place(MASCOT_MOUTH_SMILE + mouth, p.eye_dx * 0.5f, up + p.head_dy);
}

void Mascot::update_extras(float dt) {
    const float s = set_->scale;
    const uint32_t now = hal::millis();

    const bool alerting = act_ == Act::Alert;
    lv_obj_set_hidden(bubble_, !alerting);
    if (alerting) {
        const float bobble = 3 * sinf(now / 90.0f);
        lv_obj_set_pos(bubble_, pad_ + ox_ + (int)(52 * s), pad_ + oy_ - (int)((330 + bobble) * s));
    }

    const bool dozing = mood_ == Mood::Sleepy && act_ == Act::None;
    for (int i = 0; i < 3; i++) {
        lv_obj_set_hidden(zzz_[i], !dozing);
        if (!dozing) continue;
        // Each "z" drifts up and to the right, then starts over.
        const float ph = fmodf(now / 3000.0f + i / 3.0f, 1.0f);
        lv_obj_set_pos(zzz_[i], pad_ + ox_ + (int)((48 + 46 * ph) * s), pad_ + oy_ - (int)((235 + 80 * ph) * s));
        lv_obj_set_style_text_opa(zzz_[i], (lv_opa_t)(255 * (ph < 0.7f ? 1.0f : (1.0f - ph) / 0.3f)), 0);
    }

    if (confetti_on_) {
        bool any = false;
        const float floor_y = base_y_ + pad_ + oy_;
        for (int i = 0; i < 14; i++) {
            conf_vy_[i] += 900 * s * dt;
            conf_x_[i] += conf_vx_[i] * dt;
            conf_y_[i] += conf_vy_[i] * dt;
            const bool alive = conf_y_[i] < floor_y;
            lv_obj_set_hidden(confetti_[i], !alive);
            if (alive) {
                lv_obj_set_pos(confetti_[i], (int)conf_x_[i], (int)conf_y_[i]);
                any = true;
            }
        }
        confetti_on_ = any;
    }
}

void Mascot::tick() {
    const uint32_t now = hal::millis();
    float dt = (now - last_tick_) / 1000.0f;
    last_tick_ = now;
    if (dt > 0.25f) dt = 0.25f;  // returning to this screen after a while

    Pose target;
    int eyes, mouth;
    mood_pose(now / 1000.0f, target, eyes, mouth);
    if (act_ != Act::None) act_pose((now - act_start_) / 1000.0f, target, eyes, mouth);

    // Ease every joint toward its target so mood changes never snap. Jumps
    // and the hat squash follow their own curves exactly.
    const float k = first_ ? 1.0f : clamp01(1.0f - expf(-dt / 0.09f));
    first_ = false;
    Pose& c = cur_;
    c.root_x = lerp_to(c.root_x, target.root_x, k);
    c.bob = lerp_to(c.bob, target.bob, k);
    c.head_dy = lerp_to(c.head_dy, target.head_dy, k);
    c.hat_dy = lerp_to(c.hat_dy, target.hat_dy, k);
    c.hat_rot = lerp_to(c.hat_rot, target.hat_rot, k);
    c.arm_l = lerp_to(c.arm_l, target.arm_l, k);
    c.arm_r = lerp_to(c.arm_r, target.arm_r, k);
    c.leg_l = lerp_to(c.leg_l, target.leg_l, k);
    c.leg_r = lerp_to(c.leg_r, target.leg_r, k);
    c.lift_l = lerp_to(c.lift_l, target.lift_l, k);
    c.lift_r = lerp_to(c.lift_r, target.lift_r, k);
    c.eye_dx = lerp_to(c.eye_dx, target.eye_dx, k);
    c.eye_dy = lerp_to(c.eye_dy, target.eye_dy, k);
    c.sit = lerp_to(c.sit, target.sit, k * 0.6f);
    c.jump = target.jump;
    c.hat_sq = target.hat_sq;

    apply(c, eyes, mouth);
    update_extras(dt);
}
