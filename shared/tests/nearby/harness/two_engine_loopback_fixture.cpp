/*
 * W0 two-engine loopback harness — shared storage.
 *
 * The harness header is deliberately header-only for the port implementations
 * (every namespace-scope function there is inline and every port struct defines
 * its callbacks in-class), so this translation unit owns the one piece of state
 * that must exist exactly once across the harness and the tests: the failure
 * counter.
 *
 * Later steps of path A put the shared deterministic provider world here as well
 * (the crypto/key state both engines must agree on, and the byte-accurate
 * loopback transport), for the same reason: one definition shared by every engine
 * and every test in the target.
 */

#include "two_engine_loopback_fixture.hpp"

namespace flynes::session::loopback {

int failures = 0;

} // namespace flynes::session::loopback
