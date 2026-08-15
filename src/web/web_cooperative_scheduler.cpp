#include <web/web_cooperative_scheduler.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace kisak::web
{
namespace
{
bool AddWouldOverflow(std::uint32_t left, std::uint32_t right) noexcept
{
    return right > std::numeric_limits<std::uint32_t>::max() - left;
}

CooperativeTraceState TraceState(CooperativeTaskState state) noexcept
{
    switch (state)
    {
    case CooperativeTaskState::Idle: return CooperativeTraceState::Idle;
    case CooperativeTaskState::Progress: return CooperativeTraceState::Progress;
    case CooperativeTaskState::Blocked: return CooperativeTraceState::Blocked;
    case CooperativeTaskState::Complete: return CooperativeTraceState::Complete;
    case CooperativeTaskState::Failed: return CooperativeTraceState::Failed;
    }
    return CooperativeTraceState::Failed;
}
} // namespace

const char *CooperativeTraceStateString(CooperativeTraceState state) noexcept
{
    switch (state)
    {
    case CooperativeTraceState::Idle: return "idle";
    case CooperativeTraceState::Progress: return "progress";
    case CooperativeTraceState::Blocked: return "blocked";
    case CooperativeTraceState::Complete: return "complete";
    case CooperativeTraceState::Failed: return "failed";
    case CooperativeTraceState::StarvedCallBudget: return "starved-call-budget";
    case CooperativeTraceState::StarvedByteBudget: return "starved-byte-budget";
    case CooperativeTraceState::StarvedRecordBudget: return "starved-record-budget";
    case CooperativeTraceState::StarvedTimeBudget: return "starved-time-budget";
    case CooperativeTraceState::ProtocolViolation: return "protocol-violation";
    }
    return "unknown";
}

bool CooperativeScheduler::Initialize(
    const CooperativeSchedulerLimits &limits,
    CooperativeClock clock,
    void *clockUserData) noexcept
{
    if (limits.maxTaskCallsPerFrame == 0u ||
        limits.maxTaskCallsPerFrame > COOPERATIVE_SCHEDULER_MAX_TASKS ||
        limits.maxReservedBytesPerFrame == 0u ||
        limits.maxReservedRecordsPerFrame == 0u ||
        limits.maxWallMicrosecondsPerFrame == 0u ||
        limits.starvationWarningFrames == 0u || !clock)
    {
        return false;
    }
    Reset();
    limits_ = limits;
    clock_ = clock;
    clockUserData_ = clockUserData;
    schedulerGeneration_ = schedulerGeneration_ == UINT32_MAX
        ? 1u
        : schedulerGeneration_ + 1u;
    initialized_ = true;
    return true;
}

void CooperativeScheduler::Reset() noexcept
{
    for (TaskSlot &slot : tasks_)
    {
        CancelSlot(slot);
        const std::uint32_t generation = slot.generation;
        slot = {};
        slot.generation = generation;
    }
    initialized_ = false;
    clock_ = nullptr;
    clockUserData_ = nullptr;
    lastFrame_ = 0u;
    registrationSerial_ = 0u;
    starvationWarnings_ = 0u;
    protocolViolations_ = 0u;
}

bool CooperativeScheduler::Register(
    const CooperativeTaskSpec &spec,
    CooperativeTaskHandle &handle) noexcept
{
    handle = {};
    if (!initialized_ || !spec.name || spec.name[0] == '\0' || !spec.run ||
        spec.reservation.maxBytes > limits_.maxReservedBytesPerFrame ||
        spec.reservation.maxRecords > limits_.maxReservedRecordsPerFrame)
    {
        return false;
    }
    for (std::size_t index = 0u; index < tasks_.size(); ++index)
    {
        TaskSlot &slot = tasks_[index];
        if (slot.registered)
        {
            if (std::strcmp(slot.spec.name, spec.name) == 0)
            {
                return false;
            }
            continue;
        }
        slot.generation = slot.generation == UINT32_MAX ? 1u : slot.generation + 1u;
        slot.registered = true;
        slot.runnable = true;
        slot.cancellationDelivered = false;
        slot.registrationSerial = ++registrationSerial_;
        slot.spec = spec;
        handle = {
            static_cast<std::uint32_t>(index + 1u),
            slot.generation,
        };
        return true;
    }
    return false;
}

bool CooperativeScheduler::Unregister(CooperativeTaskHandle handle) noexcept
{
    TaskSlot *slot = Find(handle);
    if (!slot)
    {
        return false;
    }
    CancelSlot(*slot);
    const std::uint32_t generation = slot->generation;
    *slot = {};
    slot->generation = generation;
    return true;
}

bool CooperativeScheduler::SetRunnable(
    CooperativeTaskHandle handle,
    bool runnable) noexcept
{
    TaskSlot *slot = Find(handle);
    if (!slot)
    {
        return false;
    }
    if (runnable && slot->cancellationDelivered)
    {
        return false;
    }
    slot->runnable = runnable;
    if (!runnable)
    {
        slot->consecutiveStarvedFrames = 0u;
    }
    return true;
}

bool CooperativeScheduler::TaskSnapshot(
    CooperativeTaskHandle handle,
    CooperativeTaskSnapshot &snapshot) const noexcept
{
    snapshot = {};
    const TaskSlot *slot = Find(handle);
    if (!slot)
    {
        return false;
    }
    snapshot = {
        true,
        slot->runnable,
        slot->spec.name,
        slot->generation,
        slot->totalCalls,
        slot->totalStarvedFrames,
        slot->consecutiveStarvedFrames,
        slot->lastState,
    };
    return true;
}

bool CooperativeScheduler::RunFrame(
    std::uint32_t frame,
    CooperativeFrameReport &report) noexcept
{
    report = {};
    if (!initialized_ || frame == 0u || frame <= lastFrame_ || !clock_)
    {
        return false;
    }
    lastFrame_ = frame;
    report.frame = frame;
    report.schedulerGeneration = schedulerGeneration_;
    report.starvationWarnings = starvationWarnings_;
    report.protocolViolations = protocolViolations_;

    std::array<std::size_t, COOPERATIVE_SCHEDULER_MAX_TASKS> order{};
    std::size_t orderCount = 0u;
    for (std::size_t index = 0u; index < tasks_.size(); ++index)
    {
        if (!tasks_[index].registered)
        {
            continue;
        }
        ++report.registeredTasks;
        if (tasks_[index].runnable)
        {
            order[orderCount++] = index;
            ++report.runnableTasks;
        }
    }
    std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(orderCount),
        [this](std::size_t left, std::size_t right) {
            const TaskSlot &lhs = tasks_[left];
            const TaskSlot &rhs = tasks_[right];
            return lhs.spec.order != rhs.spec.order
                ? lhs.spec.order < rhs.spec.order
                : lhs.registrationSerial < rhs.registrationSerial;
        });

    const std::uint64_t start = clock_(clockUserData_);
    for (std::size_t orderIndex = 0u; orderIndex < orderCount; ++orderIndex)
    {
        TaskSlot &slot = tasks_[order[orderIndex]];
        CooperativeTraceEntry &trace = report.trace[report.traceCount++];
        trace.name = slot.spec.name;
        trace.order = slot.spec.order;
        trace.taskGeneration = slot.generation;
        trace.reservation = slot.spec.reservation;

        CooperativeTraceState starvedState = CooperativeTraceState::Idle;
        const std::uint64_t before = clock_(clockUserData_);
        if (report.taskCalls >= limits_.maxTaskCallsPerFrame)
        {
            starvedState = CooperativeTraceState::StarvedCallBudget;
        }
        else if (AddWouldOverflow(report.reservedBytes, slot.spec.reservation.maxBytes) ||
                 report.reservedBytes + slot.spec.reservation.maxBytes >
                    limits_.maxReservedBytesPerFrame)
        {
            starvedState = CooperativeTraceState::StarvedByteBudget;
        }
        else if (AddWouldOverflow(report.reservedRecords, slot.spec.reservation.maxRecords) ||
                 report.reservedRecords + slot.spec.reservation.maxRecords >
                    limits_.maxReservedRecordsPerFrame)
        {
            starvedState = CooperativeTraceState::StarvedRecordBudget;
        }
        else if (before - start >= limits_.maxWallMicrosecondsPerFrame)
        {
            starvedState = CooperativeTraceState::StarvedTimeBudget;
        }

        if (starvedState != CooperativeTraceState::Idle)
        {
            trace.state = starvedState;
            ++slot.totalStarvedFrames;
            ++slot.consecutiveStarvedFrames;
            trace.consecutiveStarvedFrames = slot.consecutiveStarvedFrames;
            ++report.starvedTasks;
            report.budgetExhausted = true;
            if (slot.consecutiveStarvedFrames == limits_.starvationWarningFrames)
            {
                ++starvationWarnings_;
            }
            continue;
        }

        report.reservedBytes += slot.spec.reservation.maxBytes;
        report.reservedRecords += slot.spec.reservation.maxRecords;
        ++report.taskCalls;
        ++slot.totalCalls;
        slot.consecutiveStarvedFrames = 0u;
        const CooperativeTaskResult result = slot.spec.run(
            frame,
            slot.spec.reservation,
            slot.spec.userData);
        trace.bytesUsed = result.bytesUsed;
        trace.recordsUsed = result.recordsUsed;
        trace.consecutiveStarvedFrames = 0u;
        if (result.bytesUsed > slot.spec.reservation.maxBytes ||
            result.recordsUsed > slot.spec.reservation.maxRecords ||
            AddWouldOverflow(report.bytesUsed, result.bytesUsed) ||
            AddWouldOverflow(report.recordsUsed, result.recordsUsed))
        {
            trace.state = CooperativeTraceState::ProtocolViolation;
            slot.lastState = CooperativeTaskState::Failed;
            slot.runnable = false;
            CancelSlot(slot);
            ++protocolViolations_;
            continue;
        }
        report.bytesUsed += result.bytesUsed;
        report.recordsUsed += result.recordsUsed;
        slot.lastState = result.state;
        trace.state = TraceState(result.state);
    }
    const std::uint64_t finish = clock_(clockUserData_);
    report.elapsedMicroseconds = finish >= start ? finish - start : 0u;
    report.starvationWarnings = starvationWarnings_;
    report.protocolViolations = protocolViolations_;
    return true;
}

CooperativeScheduler::TaskSlot *CooperativeScheduler::Find(
    CooperativeTaskHandle handle) noexcept
{
    if (handle.slot == 0u || handle.slot > tasks_.size())
    {
        return nullptr;
    }
    TaskSlot &slot = tasks_[handle.slot - 1u];
    return slot.registered && slot.generation == handle.generation ? &slot : nullptr;
}

const CooperativeScheduler::TaskSlot *CooperativeScheduler::Find(
    CooperativeTaskHandle handle) const noexcept
{
    if (handle.slot == 0u || handle.slot > tasks_.size())
    {
        return nullptr;
    }
    const TaskSlot &slot = tasks_[handle.slot - 1u];
    return slot.registered && slot.generation == handle.generation ? &slot : nullptr;
}

void CooperativeScheduler::CancelSlot(TaskSlot &slot) noexcept
{
    if (slot.registered && !slot.cancellationDelivered && slot.spec.cancel)
    {
        slot.cancellationDelivered = true;
        slot.spec.cancel(slot.spec.userData);
    }
}
} // namespace kisak::web
