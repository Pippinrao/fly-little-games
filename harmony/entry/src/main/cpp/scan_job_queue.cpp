#include "scan_job_queue.hpp"

#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace flynes::harmony {

ScanExecutionResult ScanExecutionResult::completed(std::uint64_t count)
{
    return {ScanJobPhase::COMPLETED, count, {}};
}

ScanExecutionResult ScanExecutionResult::cancelled()
{
    return {ScanJobPhase::CANCELLED, 0u, {}};
}

ScanExecutionResult ScanExecutionResult::failed(std::string detail)
{
    return {ScanJobPhase::FAILED, 0u, std::move(detail)};
}

bool ScanJobSnapshot::terminal() const noexcept
{
    return phase == ScanJobPhase::COMPLETED || phase == ScanJobPhase::CANCELLED ||
           phase == ScanJobPhase::FAILED;
}

class ScanJobQueue::Impl final
{
public:
    struct Job final
    {
        ScanJobRequest request;
        ScanJobSnapshot snapshot;
    };

    Impl(Executor execute, FdCloser closer)
        : executor(std::move(execute)), close_fd(std::move(closer)), worker(&Impl::run, this)
    {
        if (!executor || !close_fd)
        {
            throw std::invalid_argument("scan queue callbacks must be set");
        }
    }

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            for (auto& item : jobs)
            {
                if (!item.second->snapshot.terminal()) item.second->snapshot.cancel_requested = true;
            }
        }
        changed.notify_all();
        if (worker.joinable()) worker.join();
    }

    std::uint64_t start(ScanJobRequest request)
    {
        std::uint64_t id = 0u;
        bool merged = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) throw std::runtime_error("scan queue is stopping");
            for (const auto& item : jobs)
            {
                const Job& active = *item.second;
                if (!active.snapshot.terminal() &&
                    active.request.source_uuid_hex == request.source_uuid_hex &&
                    active.request.source_scope == request.source_scope)
                {
                    id = active.snapshot.id;
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                id = next_id++;
                auto job = std::make_shared<Job>();
                job->request = std::move(request);
                job->snapshot.id = id;
                job->snapshot.phase = ScanJobPhase::QUEUED;
                jobs.emplace(id, job);
                pending.push_back(id);
            }
        }
        if (merged)
        {
            close_files(request);
        }
        else
        {
            changed.notify_one();
        }
        return id;
    }

    ScanJobSnapshot status(std::uint64_t id) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = jobs.find(id);
        return found == jobs.end() ? ScanJobSnapshot{} : found->second->snapshot;
    }

    bool cancel(std::uint64_t id)
    {
        std::shared_ptr<Job> cancelled_queued_job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto found = jobs.find(id);
            if (found == jobs.end() || found->second->snapshot.terminal()) return false;
            found->second->snapshot.cancel_requested = true;
            if (found->second->snapshot.phase == ScanJobPhase::QUEUED)
            {
                found->second->snapshot.phase = ScanJobPhase::CANCELLED;
                for (auto queued = pending.begin(); queued != pending.end(); ++queued)
                {
                    if (*queued == id)
                    {
                        pending.erase(queued);
                        break;
                    }
                }
                cancelled_queued_job = found->second;
            }
        }
        if (cancelled_queued_job) close_files(cancelled_queued_job->request);
        changed.notify_all();
        return true;
    }

    std::uint64_t cancel_source(const std::string& source_uuid_hex, std::uint32_t source_scope)
    {
        std::vector<std::shared_ptr<Job>> cancelled_queued_jobs;
        std::uint64_t count = 0u;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto& item : jobs)
            {
                Job& job = *item.second;
                if (job.snapshot.terminal() || job.request.source_uuid_hex != source_uuid_hex ||
                    job.request.source_scope != source_scope)
                {
                    continue;
                }
                ++count;
                job.snapshot.cancel_requested = true;
                if (job.snapshot.phase == ScanJobPhase::QUEUED)
                {
                    job.snapshot.phase = ScanJobPhase::CANCELLED;
                    for (auto queued = pending.begin(); queued != pending.end(); ++queued)
                    {
                        if (*queued == job.snapshot.id)
                        {
                            pending.erase(queued);
                            break;
                        }
                    }
                    cancelled_queued_jobs.push_back(item.second);
                }
            }
        }
        for (const auto& job : cancelled_queued_jobs) close_files(job->request);
        changed.notify_all();
        return count;
    }

private:
    void run()
    {
        for (;;)
        {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&]() { return stopping || !pending.empty(); });
                if (pending.empty() && stopping) return;
                const std::uint64_t id = pending.front();
                pending.pop_front();
                job = jobs.at(id);
                if (job->snapshot.cancel_requested)
                {
                    job->snapshot.phase = ScanJobPhase::CANCELLED;
                }
                else
                {
                    job->snapshot.phase = ScanJobPhase::RUNNING;
                }
            }

            if (job->snapshot.phase == ScanJobPhase::CANCELLED)
            {
                close_files(job->request);
                continue;
            }

            ScanExecutionResult result;
            try
            {
                result = executor(
                    job->request,
                    [this, job]() {
                        std::lock_guard<std::mutex> lock(mutex);
                        return job->snapshot.cancel_requested;
                    },
                    [this, job](std::uint64_t processed, std::uint64_t results) {
                        std::lock_guard<std::mutex> lock(mutex);
                        job->snapshot.processed_files = processed;
                        job->snapshot.result_count = results;
                    });
            }
            catch (const std::exception& error)
            {
                result = ScanExecutionResult::failed(error.what());
            }
            catch (...)
            {
                result = ScanExecutionResult::failed("unknown scan worker error");
            }
            close_files(job->request);
            {
                std::lock_guard<std::mutex> lock(mutex);
                job->snapshot.phase = result.phase;
                job->snapshot.result_count = result.result_count;
                job->snapshot.error = std::move(result.error);
            }
        }
    }

    void close_files(ScanJobRequest& request) noexcept
    {
        for (ScanJobFile& file : request.files)
        {
            if (file.owned_fd >= 0)
            {
                close_fd(file.owned_fd);
                file.owned_fd = -1;
            }
        }
    }

    Executor executor;
    FdCloser close_fd;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<std::uint64_t, std::shared_ptr<Job>> jobs;
    std::deque<std::uint64_t> pending;
    std::uint64_t next_id = 1u;
    bool stopping = false;
    std::thread worker;
};

ScanJobQueue::ScanJobQueue(Executor executor, FdCloser close_fd)
    : impl_(std::make_unique<Impl>(std::move(executor), std::move(close_fd)))
{
}

ScanJobQueue::~ScanJobQueue() = default;

std::uint64_t ScanJobQueue::start(ScanJobRequest request)
{
    return impl_->start(std::move(request));
}

ScanJobSnapshot ScanJobQueue::status(std::uint64_t id) const
{
    return impl_->status(id);
}

bool ScanJobQueue::cancel(std::uint64_t id)
{
    return impl_->cancel(id);
}

std::uint64_t ScanJobQueue::cancel_source(
    const std::string& source_uuid_hex, std::uint32_t source_scope)
{
    return impl_->cancel_source(source_uuid_hex, source_scope);
}

} // namespace flynes::harmony
