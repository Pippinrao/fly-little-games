#pragma once
#include "flynes/product/gamepad_hit_map.hpp"
#include "flynes/product/nes_input_bits.hpp"
#include <cmath>
#include <algorithm>
#include <utility>

namespace flynes::ios {
// Stateful port of Android input/DirectionSession.java; geometry stays shared.
class DirectionSession {
public:
    explicit DirectionSession(const product::GamepadHitMap& map) : map_(map) {}
    bool owned = false;
    float centerX = 0, centerY = 0, knobX = 0, knobY = 0;
    std::uint32_t bits = 0;
    std::uint64_t feedbackRevision = 0;
    bool activated() const { return bits != 0; }
    bool saturated() const { return owned && std::hypot(effectiveX_-centerX,effectiveY_-centerY) > map_.joystick_travel_radius(); }
    float knobRadius() const { return map_.joystick_radius() * .4375f; }
    bool capture(float x, float y) {
        if (owned || !finite(x,y) || !map_.can_start_direction(x,y)) return false;
        owned = true; rawX_ = x; rawY_ = y; sector_ = -1; bits = 0;
        if (map_.following_joystick_mode()) {
            centerX = map_.clamp_joystick_center_x(x); centerY = map_.clamp_joystick_center_y(y);
            effectiveX_ = centerX; effectiveY_ = centerY;
        } else {
            centerX = map_.dpad_bounds().center_x(); centerY = map_.dpad_bounds().center_y();
            effectiveX_ = x; effectiveY_ = y;
        }
        refresh(); update(); return true;
    }
    void move(float x, float y) {
        if (!owned || !finite(x,y)) return;
        const float dx = x-rawX_, dy = y-rawY_;
        rawX_ = x; rawY_ = y; effectiveX_ += dx; effectiveY_ += dy;
        if (map_.following_joystick_mode() && (dx != 0 || dy != 0)) {
            centerX = map_.clamp_joystick_center_x(centerX); centerY = map_.clamp_joystick_center_y(centerY);
            const float vx = effectiveX_-centerX, vy = effectiveY_-centerY;
            const float distance = std::hypot(vx,vy), travel = map_.joystick_travel_radius();
            if (distance > travel && distance > 0) {
                const float follow = (distance-travel)/distance;
                centerX = map_.clamp_joystick_center_x(centerX+vx*follow);
                centerY = map_.clamp_joystick_center_y(centerY+vy*follow);
            }
        }
        refresh(); update();
    }
    void reconfigure(const product::GamepadHitMap& map) {
        const bool changedMode = map.direction_mode() != map_.direction_mode();
        map_ = map;
        if (changedMode) { clear(); return; }
        if (!owned) return;
        const float previousX = centerX, previousY = centerY;
        centerX = map_.following_joystick_mode() ? map_.clamp_joystick_center_x(centerX) : map_.dpad_bounds().center_x();
        centerY = map_.following_joystick_mode() ? map_.clamp_joystick_center_y(centerY) : map_.dpad_bounds().center_y();
        effectiveX_ += centerX-previousX; effectiveY_ += centerY-previousY;
        refresh();
    }
    void clear() {
        owned = false; bits = 0; sector_ = -1;
        rawX_ = rawY_ = effectiveX_ = effectiveY_ = centerX = centerY = knobX = knobY = 0;
    }
private:
    product::GamepadHitMap map_;
    float rawX_ = 0, rawY_ = 0, effectiveX_ = 0, effectiveY_ = 0;
    int sector_ = -1;
    static constexpr double pi = 3.14159265358979323846;
    static bool finite(float x,float y) { return std::isfinite(x) && std::isfinite(y); }
    void refresh() {
        const float dx = effectiveX_-centerX, dy = effectiveY_-centerY;
        const float distance = std::hypot(dx,dy), travel = map_.joystick_travel_radius();
        const float scale = distance > travel && distance > 0 ? travel/distance : 1;
        knobX = centerX+dx*scale; knobY = centerY+dy*scale;
    }
    void update() {
        const float dx = effectiveX_-centerX, dy = effectiveY_-centerY;
        if (std::hypot(dx,dy) < map_.dead_zone()*map_.joystick_radius()) return;
        double angle = std::atan2(static_cast<double>(dy),static_cast<double>(dx)); if (angle < 0) angle += 2*pi;
        const int candidate = ((int)std::floor((angle+pi/8)/(pi/4))) & 7;
        if (sector_ >= 0) {
            double delta = angle-sector_*pi/4;
            if (delta <= -pi) delta += 2*pi; else if (delta > pi) delta -= 2*pi;
            if (candidate == sector_ || std::abs(delta) <= pi/8+pi/24) return;
        }
        sector_ = candidate;
        using namespace product;
        static constexpr std::uint32_t sectors[] = {NES_RIGHT, NES_RIGHT|NES_DOWN, NES_DOWN,
            NES_LEFT|NES_DOWN, NES_LEFT, NES_LEFT|NES_UP, NES_UP, NES_RIGHT|NES_UP};
        bits = sectors[sector_];
        ++feedbackRevision;
    }
};
class DirectionFeedbackGate {
public:
    bool shouldEmit(std::uint64_t revision, double time) {
        if (revision == observed_) return false;
        observed_ = revision;
        if (hasTime_ && time-lastTime_ < .080) return false;
        hasTime_ = true; lastTime_ = time; return true;
    }
    void consume(std::uint64_t revision) { observed_ = revision; }
    void reset(std::uint64_t revision = 0) { observed_ = revision; hasTime_ = false; }
private:
    std::uint64_t observed_ = 0;
    double lastTime_ = 0;
    bool hasTime_ = false;
};
class JoystickReturnAnimation {
public:
    void start(float x,float y,float knobX,float knobY,double time) {
        centerX_=x; centerY_=y; startX_=knobX; startY_=knobY; time_=time;
        active_ = x != knobX || y != knobY;
    }
    std::pair<float,float> sample(double time) {
        if (!active_) return {centerX_,centerY_};
        const float progress = static_cast<float>(std::clamp((time-time_)/.080,0.0,1.0));
        if (progress >= 1) active_ = false;
        return {startX_+(centerX_-startX_)*progress,startY_+(centerY_-startY_)*progress};
    }
    bool active() const { return active_; }
    void cancel() { active_=false; }
private:
    float centerX_=0,centerY_=0,startX_=0,startY_=0;
    double time_=0;
    bool active_=false;
};
}
