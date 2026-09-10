#include "run/DirectionSession.hpp"
#include <iostream>
#include <stdexcept>
using namespace flynes::product;
static void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
int main() { try {
    auto map = GamepadHitMap::from_layout(1000,500,1,0,0,0,0,ControlLayoutV2::recommended(),DirectionControlMode::Joystick,.22f);
    flynes::ios::DirectionSession follow(map);
    check(follow.capture(10,250), "capture near safe edge");
    check(follow.bits == 0, "edge capture stays neutral");
    follow.move(30,250);
    check(follow.bits == NES_RIGHT, "relative movement activates right");
    follow.move(10,250);
    check(follow.bits == NES_RIGHT, "dead zone retains activated direction");
    follow.move(200,250);
    check(follow.centerX > 100, "following base moves along long drag");
    check(std::hypot(follow.knobX-follow.centerX, follow.knobY-follow.centerY) <= map.joystick_travel_radius()+.01f, "knob stays within travel radius");
    follow.clear(); check(follow.bits == 0 && !follow.owned, "cancel clears direction");
    map = GamepadHitMap::from_layout(1000,500,1,0,0,0,0,ControlLayoutV2::recommended(),DirectionControlMode::DPad,.22f);
    flynes::ios::DirectionSession pad(map);
    const float x = map.dpad_bounds().center_x(), y = map.dpad_bounds().center_y();
    check(pad.capture(x+30,y), "capture D-pad right");
    pad.move(900,y); check(pad.bits == NES_RIGHT, "owned D-pad moves remain unbounded");
    pad.move(x,y); check(pad.bits == NES_RIGHT, "owned D-pad center retains direction");
    pad.move(x+50,y+25); check(pad.bits == NES_RIGHT, "sector hysteresis retains right at 26 degrees");
    pad.move(x+50,y+40); check(pad.bits == (NES_RIGHT|NES_DOWN), "crossing hysteresis accepts diagonal");
    check(pad.activated() && pad.saturated(), "active saturated gesture exposes visual state");
    check(std::abs(pad.knobRadius()+map.joystick_travel_radius()-map.joystick_radius()) < .001f,
        "knob plus travel must fit inside base radius");
    flynes::ios::DirectionFeedbackGate feedback;
    check(feedback.shouldEmit(1,1.0), "first direction transition emits feedback");
    check(!feedback.shouldEmit(2,1.020), "direction feedback is limited to 80ms");
    check(!feedback.shouldEmit(2,1.100), "suppressed transition is consumed");
    check(feedback.shouldEmit(3,1.100), "new transition after cooldown emits");
    feedback.consume(4);
    check(!feedback.shouldEmit(4,1.200), "terminal transition stays silent");
    flynes::ios::JoystickReturnAnimation animation;
    animation.start(100,100,130,100,1.0);
    auto position = animation.sample(1.040);
    check(std::abs(position.first-115) < .001f && position.second == 100, "return animation interpolates at 40ms");
    position = animation.sample(1.080);
    check(position.first == 100 && !animation.active(), "return animation ends at 80ms");
    std::cout << "ios_direction_session: PASS\n";
} catch(const std::exception& e) { std::cerr << "ios_direction_session: FAIL: " << e.what() << '\n'; return 1; } }
