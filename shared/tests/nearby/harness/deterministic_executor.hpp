#ifndef FLYNES_TESTS_NEARBY_HARNESS_DETERMINISTIC_EXECUTOR_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_DETERMINISTIC_EXECUTOR_HPP

#include <flynes/flynes_session.h>

#include <cstdint>
#include <vector>

class DeterministicExecutor final
{
public:
    ~DeterministicExecutor()
    {
        for (auto* task : tasks_)
        {
            fly_session_task_release_v2(task);
        }
    }

    fly_session_executor_port_v2 port()
    {
        fly_session_executor_port_v2 result{};
        result.struct_size = FLY_SESSION_EXECUTOR_PORT_V2_SIZE;
        result.abi_version = FLY_SESSION_ABI_VERSION_2;
        result.context = this;
        result.retain = retain;
        result.release = release;
        result.post = post;
        result.arm_timer = arm_timer;
        result.cancel_timer = cancel_timer;
        return result;
    }

    std::size_t size() const noexcept { return tasks_.size(); }
    int retains() const noexcept { return retains_; }
    int releases() const noexcept { return releases_; }

    void run_all()
    {
        while (!tasks_.empty())
        {
            auto* task = tasks_.front();
            tasks_.erase(tasks_.begin());
            fly_session_task_run_v2(task);
        }
    }

private:
    static void retain(void* context)
    {
        static_cast<DeterministicExecutor*>(context)->retains_ += 1;
    }

    static void release(void* context)
    {
        static_cast<DeterministicExecutor*>(context)->releases_ += 1;
    }

    static fly_session_result_v2 post(void* context, fly_session_task_v2_t* task)
    {
        static_cast<DeterministicExecutor*>(context)->tasks_.push_back(task);
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 arm_timer(void*,
                                           std::uint64_t,
                                           const std::uint8_t*,
                                           std::uint64_t,
                                           fly_session_task_v2_t*)
    {
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 cancel_timer(void*, std::uint64_t)
    {
        return FLY_SESSION_V2_ACCEPTED;
    }

    std::vector<fly_session_task_v2_t*> tasks_;
    int retains_ = 0;
    int releases_ = 0;
};

#endif
