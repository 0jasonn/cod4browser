#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kisak::web
{
inline constexpr std::size_t COOPERATIVE_SCHEDULER_MAX_TASKS = 8u;

struct CooperativeSchedulerLimits
{
    std::uint32_t maxTaskCallsPerFrame = 8u;
    std::uint32_t maxReservedBytesPerFrame = 256u * 1024u;
    std::uint32_t maxReservedRecordsPerFrame = 256u;
    std::uint32_t maxWallMicrosecondsPerFrame = 12u * 1000u;
    std::uint32_t starvationWarningFrames = 3u;
};

struct CooperativeTaskBudget
{
    std::uint32_t maxBytes = 0u;
    std::uint32_t maxRecords = 0u;
};

enum class CooperativeTaskState : std::uint8_t
{
    Idle,
    Progress,
    Blocked,
    Complete,
    Failed,
};

struct CooperativeTaskResult
{
    CooperativeTaskState state = CooperativeTaskState::Idle;
    std::uint32_t bytesUsed = 0u;
    std::uint32_t recordsUsed = 0u;
};

using CooperativeTaskRun = CooperativeTaskResult (*)(
    std::uint32_t frame,
    const CooperativeTaskBudget &budget,
    void *userData);
using CooperativeTaskCancel = void (*)(void *userData);
using CooperativeClock = std::uint64_t (*)(void *userData);

struct CooperativeTaskSpec
{
    const char *name = nullptr;
    std::uint32_t order = 0u;
    CooperativeTaskBudget reservation;
    CooperativeTaskRun run = nullptr;
    CooperativeTaskCancel cancel = nullptr;
    void *userData = nullptr;
};

struct CooperativeTaskHandle
{
    std::uint32_t slot = 0u;
    std::uint32_t generation = 0u;
};

enum class CooperativeTraceState : std::uint8_t
{
    Idle,
    Progress,
    Blocked,
    Complete,
    Failed,
    StarvedCallBudget,
    StarvedByteBudget,
    StarvedRecordBudget,
    StarvedTimeBudget,
    ProtocolViolation,
};

struct CooperativeTraceEntry
{
    const char *name = nullptr;
    std::uint32_t order = 0u;
    std::uint32_t taskGeneration = 0u;
    CooperativeTraceState state = CooperativeTraceState::Idle;
    CooperativeTaskBudget reservation;
    std::uint32_t bytesUsed = 0u;
    std::uint32_t recordsUsed = 0u;
    std::uint32_t consecutiveStarvedFrames = 0u;
};

struct CooperativeFrameReport
{
    std::uint32_t frame = 0u;
    std::uint32_t schedulerGeneration = 0u;
    std::uint32_t registeredTasks = 0u;
    std::uint32_t runnableTasks = 0u;
    std::uint32_t taskCalls = 0u;
    std::uint32_t reservedBytes = 0u;
    std::uint32_t reservedRecords = 0u;
    std::uint32_t bytesUsed = 0u;
    std::uint32_t recordsUsed = 0u;
    std::uint32_t starvedTasks = 0u;
    std::uint32_t starvationWarnings = 0u;
    std::uint32_t protocolViolations = 0u;
    std::uint64_t elapsedMicroseconds = 0u;
    bool budgetExhausted = false;
    std::size_t traceCount = 0u;
    std::array<CooperativeTraceEntry, COOPERATIVE_SCHEDULER_MAX_TASKS> trace{};
};

struct CooperativeTaskSnapshot
{
    bool registered = false;
    bool runnable = false;
    const char *name = nullptr;
    std::uint32_t generation = 0u;
    std::uint64_t totalCalls = 0u;
    std::uint64_t totalStarvedFrames = 0u;
    std::uint32_t consecutiveStarvedFrames = 0u;
    CooperativeTaskState lastState = CooperativeTaskState::Idle;
};

const char *CooperativeTraceStateString(CooperativeTraceState state) noexcept;

// Fixed-capacity, allocation-free scheduler. Task reservations are charged
// before invocation, so admitted work cannot exceed the configured per-frame
// byte/record/call envelope. Wall time is checked between callbacks; callbacks
// remain responsible for their own hard internal work ceilings.
class CooperativeScheduler
{
public:
    bool Initialize(
        const CooperativeSchedulerLimits &limits,
        CooperativeClock clock,
        void *clockUserData) noexcept;
    void Reset() noexcept;

    bool Register(
        const CooperativeTaskSpec &spec,
        CooperativeTaskHandle &handle) noexcept;
    bool Unregister(CooperativeTaskHandle handle) noexcept;
    bool SetRunnable(CooperativeTaskHandle handle, bool runnable) noexcept;
    bool TaskSnapshot(
        CooperativeTaskHandle handle,
        CooperativeTaskSnapshot &snapshot) const noexcept;

    bool RunFrame(std::uint32_t frame, CooperativeFrameReport &report) noexcept;
    const CooperativeSchedulerLimits &Limits() const noexcept { return limits_; }
    std::uint32_t Generation() const noexcept { return schedulerGeneration_; }

private:
    struct TaskSlot
    {
        bool registered = false;
        bool runnable = false;
        bool cancellationDelivered = false;
        std::uint32_t generation = 0u;
        std::uint64_t registrationSerial = 0u;
        CooperativeTaskSpec spec;
        std::uint64_t totalCalls = 0u;
        std::uint64_t totalStarvedFrames = 0u;
        std::uint32_t consecutiveStarvedFrames = 0u;
        CooperativeTaskState lastState = CooperativeTaskState::Idle;
    };

    TaskSlot *Find(CooperativeTaskHandle handle) noexcept;
    const TaskSlot *Find(CooperativeTaskHandle handle) const noexcept;
    void CancelSlot(TaskSlot &slot) noexcept;

    bool initialized_ = false;
    CooperativeSchedulerLimits limits_{};
    CooperativeClock clock_ = nullptr;
    void *clockUserData_ = nullptr;
    std::uint32_t schedulerGeneration_ = 0u;
    std::uint32_t lastFrame_ = 0u;
    std::uint64_t registrationSerial_ = 0u;
    std::uint32_t starvationWarnings_ = 0u;
    std::uint32_t protocolViolations_ = 0u;
    std::array<TaskSlot, COOPERATIVE_SCHEDULER_MAX_TASKS> tasks_{};
};
} // namespace kisak::web
