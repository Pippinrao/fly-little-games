#ifndef FLYNES_TEST_ENDPOINT_OFFER_SCHEDULER_HPP
#define FLYNES_TEST_ENDPOINT_OFFER_SCHEDULER_HPP

#include "link/endpoint_offer_scheduler.hpp"

namespace flynes::session {

// Tests below the public engine may start from already-authenticated endpoint
// evidence. Production has no equivalent state-forcing API.
struct EndpointOfferSchedulerTestFactory
{
    static void seal_ready(EndpointOfferScheduler& scheduler,
                           bool listener,
                           std::vector<std::uint8_t> endpoint)
    {
        scheduler.begun_ = true;
        scheduler.ready_ = true;
        scheduler.failed_ = false;
        scheduler.local_listener_ = listener;
        scheduler.endpoint_ = std::move(endpoint);
        scheduler.stage_ = EndpointOfferScheduler::Stage::Ready;
    }
};

} // namespace flynes::session

#endif
