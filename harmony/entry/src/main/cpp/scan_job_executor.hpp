#pragma once

#include "scan_job_queue.hpp"

struct fly_app_handle;
typedef struct fly_app_handle fly_app_t;

namespace flynes::harmony {

ScanExecutionResult execute_scan_job(
    fly_app_t& app,
    const ScanJobRequest& request,
    const ScanJobQueue::CancelCheck& cancelled,
    const ScanJobQueue::ProgressSink& progress);

} // namespace flynes::harmony
