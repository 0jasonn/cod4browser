#include <web/web_cooperative_scheduler.h>

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
using kisak::web::CooperativeFrameReport;
using kisak::web::CooperativeScheduler;
using kisak::web::CooperativeSchedulerLimits;
using kisak::web::CooperativeTaskBudget;
using kisak::web::CooperativeTaskHandle;
using kisak::web::CooperativeTaskResult;
using kisak::web::CooperativeTaskSnapshot;
using kisak::web::CooperativeTaskSpec;
using kisak::web::CooperativeTaskState;
using kisak::web::CooperativeTraceState;

struct FakeClock
{
    std::uint64_t now = 0u;
};

struct TaskFixture
{
    int id = 0;
    std::vector<int> *order = nullptr;
    FakeClock *clock = nullptr;
    std::uint64_t elapsed = 0u;
    CooperativeTaskResult result{CooperativeTaskState::Progress, 0u, 0u};
    std::uint32_t cancellations = 0u;
};

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::uint64_t ReadClock(void *userData)
{
    return static_cast<FakeClock *>(userData)->now;
}

CooperativeTaskResult RunTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *userData)
{
    auto &fixture = *static_cast<TaskFixture *>(userData);
    if (fixture.order)
    {
        fixture.order->push_back(fixture.id);
    }
    if (fixture.clock)
    {
        fixture.clock->now += fixture.elapsed;
    }
    return fixture.result;
}

void CancelTask(void *userData)
{
    ++static_cast<TaskFixture *>(userData)->cancellations;
}

CooperativeTaskSpec Spec(
    const char *name,
    std::uint32_t order,
    CooperativeTaskBudget reservation,
    TaskFixture &fixture)
{
    return {name, order, reservation, RunTask, CancelTask, &fixture};
}

void TestValidationAndDeterministicOrder()
{
    FakeClock clock;
    CooperativeScheduler scheduler;
    CooperativeSchedulerLimits limits{3u, 30u, 3u, 100u, 2u};
    Require(scheduler.Initialize(limits, ReadClock, &clock), "valid scheduler initializes");

    std::vector<int> order;
    TaskFixture third{3, &order, &clock, 1u, {CooperativeTaskState::Progress, 3u, 1u}};
    TaskFixture first{1, &order, &clock, 1u, {CooperativeTaskState::Progress, 1u, 1u}};
    TaskFixture second{2, &order, &clock, 1u, {CooperativeTaskState::Complete, 2u, 1u}};
    CooperativeTaskHandle thirdHandle;
    CooperativeTaskHandle firstHandle;
    CooperativeTaskHandle secondHandle;
    Require(scheduler.Register(Spec("third", 30u, {10u, 1u}, third), thirdHandle),
        "third-order task registers first");
    Require(scheduler.Register(Spec("first", 10u, {10u, 1u}, first), firstHandle),
        "first-order task registers second");
    Require(scheduler.Register(Spec("second", 20u, {10u, 1u}, second), secondHandle),
        "second-order task registers third");
    CooperativeTaskHandle duplicate;
    Require(!scheduler.Register(Spec("first", 40u, {1u, 1u}, third), duplicate),
        "duplicate task names are rejected");

    CooperativeFrameReport report;
    Require(scheduler.RunFrame(1u, report), "first frame runs");
    Require(order == std::vector<int>({1, 2, 3}),
        "execution follows order then registration serial");
    Require(report.registeredTasks == 3u && report.runnableTasks == 3u &&
            report.taskCalls == 3u && report.traceCount == 3u,
        "frame report has exact task counts");
    Require(report.reservedBytes == 30u && report.reservedRecords == 3u &&
            report.bytesUsed == 6u && report.recordsUsed == 3u,
        "reservations and actual work are reported independently");
    Require(report.elapsedMicroseconds == 3u && !report.budgetExhausted,
        "fake monotonic clock measures callback time");
    Require(std::string_view(report.trace[0].name) == "first" &&
            report.trace[1].state == CooperativeTraceState::Complete,
        "trace preserves deterministic names and outcomes");
    Require(!scheduler.RunFrame(1u, report), "duplicate frame number is rejected");
    Require(!scheduler.RunFrame(0u, report), "zero frame number is rejected");

    CooperativeScheduler invalid;
    CooperativeSchedulerLimits invalidLimits = limits;
    invalidLimits.maxTaskCallsPerFrame = 0u;
    Require(!invalid.Initialize(invalidLimits, ReadClock, &clock),
        "zero task-call budget is invalid");
    Require(!invalid.Initialize(limits, nullptr, nullptr), "missing monotonic clock is invalid");
}

void TestBudgetStarvationAndWarnings()
{
    FakeClock clock;
    CooperativeScheduler scheduler;
    Require(scheduler.Initialize({2u, 30u, 3u, 100u, 2u}, ReadClock, &clock),
        "starvation fixture initializes");
    TaskFixture first;
    TaskFixture second;
    TaskFixture third;
    CooperativeTaskHandle handles[3]{};
    Require(scheduler.Register(Spec("first", 10u, {10u, 1u}, first), handles[0]),
        "first starvation task registers");
    Require(scheduler.Register(Spec("second", 20u, {10u, 1u}, second), handles[1]),
        "second starvation task registers");
    Require(scheduler.Register(Spec("third", 30u, {10u, 1u}, third), handles[2]),
        "third starvation task registers");

    CooperativeFrameReport report;
    Require(scheduler.RunFrame(1u, report) && report.starvedTasks == 1u &&
            report.trace[2].state == CooperativeTraceState::StarvedCallBudget,
        "call budget deterministically starves the last task");
    Require(report.starvationWarnings == 0u, "first denied frame is below warning threshold");
    Require(scheduler.RunFrame(2u, report) && report.starvationWarnings == 1u,
        "repeated denial emits one starvation warning");
    CooperativeTaskSnapshot snapshot;
    Require(scheduler.TaskSnapshot(handles[2], snapshot) &&
            snapshot.totalStarvedFrames == 2u && snapshot.consecutiveStarvedFrames == 2u,
        "task snapshot retains starvation history");
    Require(scheduler.SetRunnable(handles[0], false), "task can be disabled without cancellation");
    Require(scheduler.RunFrame(3u, report) && report.taskCalls == 2u &&
            report.starvedTasks == 0u,
        "disabled task frees budget and resets starvation");
}

void TestByteRecordAndTimeBudgets()
{
    {
        FakeClock clock;
        CooperativeScheduler scheduler;
        Require(scheduler.Initialize({3u, 15u, 3u, 100u, 2u}, ReadClock, &clock),
            "byte-budget fixture initializes");
        TaskFixture first;
        TaskFixture second;
        CooperativeTaskHandle handle;
        Require(scheduler.Register(Spec("first", 10u, {10u, 1u}, first), handle),
            "first byte task registers");
        Require(scheduler.Register(Spec("second", 20u, {10u, 1u}, second), handle),
            "second byte task registers");
        CooperativeFrameReport report;
        Require(scheduler.RunFrame(1u, report) &&
                report.trace[1].state == CooperativeTraceState::StarvedByteBudget,
            "byte reservation cannot exceed the frame envelope");
    }
    {
        FakeClock clock;
        CooperativeScheduler scheduler;
        Require(scheduler.Initialize({3u, 30u, 1u, 100u, 2u}, ReadClock, &clock),
            "record-budget fixture initializes");
        TaskFixture first;
        TaskFixture second;
        CooperativeTaskHandle handle;
        Require(scheduler.Register(Spec("first", 10u, {1u, 1u}, first), handle),
            "first record task registers");
        Require(scheduler.Register(Spec("second", 20u, {1u, 1u}, second), handle),
            "second record task registers");
        CooperativeFrameReport report;
        Require(scheduler.RunFrame(1u, report) &&
                report.trace[1].state == CooperativeTraceState::StarvedRecordBudget,
            "record reservation cannot exceed the frame envelope");
    }
    {
        FakeClock clock;
        CooperativeScheduler scheduler;
        Require(scheduler.Initialize({3u, 30u, 3u, 50u, 2u}, ReadClock, &clock),
            "time-budget fixture initializes");
        TaskFixture slow{1, nullptr, &clock, 50u};
        TaskFixture later;
        CooperativeTaskHandle handle;
        Require(scheduler.Register(Spec("slow", 10u, {1u, 1u}, slow), handle),
            "slow task registers");
        Require(scheduler.Register(Spec("later", 20u, {1u, 1u}, later), handle),
            "later task registers");
        CooperativeFrameReport report;
        Require(scheduler.RunFrame(1u, report) &&
                report.trace[1].state == CooperativeTraceState::StarvedTimeBudget,
            "wall-time budget is enforced between non-preemptible callbacks");
    }
}

void TestProtocolQuarantineAndGenerationOwnership()
{
    FakeClock clock;
    CooperativeScheduler scheduler;
    Require(scheduler.Initialize({2u, 20u, 2u, 100u, 2u}, ReadClock, &clock),
        "protocol fixture initializes");
    TaskFixture violating;
    violating.result = {CooperativeTaskState::Progress, 11u, 1u};
    CooperativeTaskHandle first;
    Require(scheduler.Register(Spec("owned", 10u, {10u, 1u}, violating), first),
        "owned task registers");
    CooperativeFrameReport report;
    Require(scheduler.RunFrame(1u, report) && report.protocolViolations == 1u &&
            report.trace[0].state == CooperativeTraceState::ProtocolViolation,
        "task exceeding its reservation is quarantined");
    Require(violating.cancellations == 1u, "protocol quarantine delivers cancellation once");
    CooperativeTaskSnapshot snapshot;
    Require(scheduler.TaskSnapshot(first, snapshot) && !snapshot.runnable,
        "quarantined task remains diagnosable but cannot run");
    Require(!scheduler.SetRunnable(first, true),
        "quarantined ownership cannot silently resume work");
    Require(scheduler.Unregister(first), "current generation can unregister");
    Require(violating.cancellations == 1u, "unregister does not duplicate cancellation");

    TaskFixture replacement;
    CooperativeTaskHandle second;
    Require(scheduler.Register(Spec("owned", 10u, {10u, 1u}, replacement), second),
        "same slot can host a replacement generation");
    Require(second.slot == first.slot && second.generation != first.generation,
        "replacement task receives a fresh generational handle");
    Require(!scheduler.Unregister(first), "stale generation cannot cancel replacement");
    scheduler.Reset();
    Require(replacement.cancellations == 1u, "scheduler reset cancels live ownership once");
}

void TestTraceStrings()
{
    for (CooperativeTraceState state : {
            CooperativeTraceState::Idle,
            CooperativeTraceState::Progress,
            CooperativeTraceState::Blocked,
            CooperativeTraceState::Complete,
            CooperativeTraceState::Failed,
            CooperativeTraceState::StarvedCallBudget,
            CooperativeTraceState::StarvedByteBudget,
            CooperativeTraceState::StarvedRecordBudget,
            CooperativeTraceState::StarvedTimeBudget,
            CooperativeTraceState::ProtocolViolation})
    {
        Require(std::string_view(kisak::web::CooperativeTraceStateString(state)) != "unknown",
            "every trace state has a stable string");
    }
}
} // namespace

int main()
{
    TestValidationAndDeterministicOrder();
    TestBudgetStarvationAndWarnings();
    TestByteRecordAndTimeBudgets();
    TestProtocolQuarantineAndGenerationOwnership();
    TestTraceStrings();
    std::cout << "web_cooperative_scheduler_tests: all checks passed\n";
    return 0;
}
