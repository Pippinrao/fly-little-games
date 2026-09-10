#include "run/FrameInputLatch.hpp"
#include <iostream>
int main() {
    flynes::ios::FrameInputLatch input;
    input.update(1); input.update(0); input.release(1, 1.0, 1.002);
    if (input.sample(1.005) != 1 || input.sample(1.016) != 1 || input.sample(1.018) != 0) {
        std::cerr << "FAIL: short A tap must last at least 17ms\n"; return 1;
    }
    input.update(1); input.update(0); input.release(1, 2.0, 2.002);
    input.update(128); input.update(0); // unrelated direction cancellation, no normal release
    if (input.sample(2.010) != 1) { std::cerr << "FAIL: unrelated cancellation erased A pulse\n"; return 1; }
    input.clear(); input.update(2); input.update(0); // canceled B must not create a pulse
    if (input.sample(3.001) != 0) { std::cerr << "FAIL: cancellation synthesized B tap\n"; return 1; }
    input.update(1); input.update(0); input.release(1, 4.0, 4.002);
    if (input.sample(4.100) != 1 || input.sample(4.101) != 0) {
        std::cerr << "FAIL: delayed frame must still consume a completed short tap once\n"; return 1;
    }
    input.release(2, 5.0, 5.050);
    if (input.sample(5.051) != 0) { std::cerr << "FAIL: long hold must not be extended\n"; return 1; }
    input.update(128); input.update(0);
    if (input.sample() != 0) { std::cerr << "FAIL: directions are not sticky\n"; return 1; }
    input.update(3); input.clear();
    if (input.sample() != 0) { std::cerr << "FAIL: cancel clears pending input\n"; return 1; }
    input.update(8);
    if (input.sample() != 8 || input.sample() != 8) { std::cerr << "FAIL: held START persists\n"; return 1; }
    std::cout << "ios_frame_input_latch: PASS\n";
}
