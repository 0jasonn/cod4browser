#include <web/web_qcommon_preinit.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace
{
using kisak::web::QcommonActionKind;
using kisak::web::QcommonFileKind;
using kisak::web::QcommonPreinitAction;
using kisak::web::QcommonPreinitError;
using kisak::web::QcommonPreinitMachine;
using kisak::web::QcommonPreinitStage;
using kisak::web::QcommonStartupFile;

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::array<std::uint8_t, 14> ValidProbe(const QcommonStartupFile &file)
{
    std::array<std::uint8_t, 14> bytes{};
    if (file.kind == QcommonFileKind::Localization)
    {
        constexpr std::array<std::uint8_t, 8> LOCALIZATION = {
            'e', 'n', 'g', 'l', 'i', 's', 'h', '\n',
        };
        std::copy(LOCALIZATION.begin(), LOCALIZATION.end(), bytes.begin());
    }
    else if (file.kind == QcommonFileKind::Iwd)
    {
        bytes[0] = 'P';
        bytes[1] = 'K';
        bytes[2] = 3u;
        bytes[3] = 4u;
    }
    else
    {
        constexpr std::array<std::uint8_t, 14> FASTFILE = {
            'I', 'W', 'f', 'f', 'u', '1', '0', '0',
            5u, 0u, 0u, 0u, 0x78u, 0xdau,
        };
        bytes = FASTFILE;
    }
    return bytes;
}

void CompleteLocalInitialization(QcommonPreinitMachine &machine)
{
    for (QcommonActionKind kind : {
            QcommonActionKind::InitializeMemory,
            QcommonActionKind::InitializeEvents,
            QcommonActionKind::RegisterCommands})
    {
        QcommonPreinitAction action;
        Require(machine.NextAction(action), "local initialization action is available");
        Require(action.kind == kind, "local initialization order is deterministic");
        Require(machine.CompleteLocal(action.token, true), "local initialization completes");
    }
}

void CompleteOneFile(QcommonPreinitMachine &machine, const QcommonStartupFile &file)
{
    QcommonPreinitAction stat;
    Require(machine.NextAction(stat) && stat.kind == QcommonActionKind::StatFile,
        "file stat action is issued");
    Require(std::string_view(stat.path) == file.path, "stat uses the exact allowlisted path");
    Require(machine.CompleteStat(stat.token, true, file.probeBytes),
        "file stat completion succeeds at the exact probe size");

    QcommonPreinitAction read;
    Require(machine.NextAction(read) && read.kind == QcommonActionKind::ReadFile,
        "file read follows stat");
    Require(std::string_view(read.path) == file.path && read.offset == 0u &&
            read.length == file.probeBytes,
        "read is a bounded header request");
    const auto bytes = ValidProbe(file);
    Require(machine.CompleteRead(
            read.token,
            true,
            std::span<const std::uint8_t>(bytes.data(), file.probeBytes)),
        "valid bounded header completes");
}

void TestProfileAndSuccessfulSequence()
{
    const auto files = kisak::web::QcommonStartupFiles();
    Require(files.size() == 25u, "startup profile has exactly 25 files");
    Require(std::string_view(files.front().path) == "localization.txt",
        "startup profile begins with localization");
    Require(std::string_view(files.back().path) == "zone/english/common.ff",
        "startup profile ends with the final prerequisite zone");

    QcommonPreinitMachine machine;
    Require(machine.Start(7u), "startup begins with a nonzero generation");
    CompleteLocalInitialization(machine);
    for (const QcommonStartupFile &file : files)
    {
        CompleteOneFile(machine, file);
    }

    const auto snapshot = machine.Snapshot();
    Require(machine.Ready() && !machine.Running(), "startup stops at pre-database ready");
    Require(snapshot.stage == QcommonPreinitStage::PreDatabase &&
            snapshot.error == QcommonPreinitError::None,
        "successful startup has no error");
    Require(snapshot.generation == 7u && snapshot.filesChecked == 25u &&
            snapshot.totalFiles == 25u,
        "ready snapshot retains generation and exact file counts");
    Require(snapshot.probeBytesRead == 134u,
        "startup reads only 134 aggregate header bytes");
    Require(snapshot.actionsIssued == 53u,
        "three local actions plus stat/read pairs are issued");
    Require(snapshot.arenaBytes == kisak::web::QCOMMON_STARTUP_ARENA_BYTES &&
            snapshot.eventCapacity == kisak::web::QCOMMON_EVENT_CAPACITY &&
            snapshot.commandDvarCount == kisak::web::QCOMMON_COMMAND_DVAR_COUNT,
        "local runtime capacities are explicit at the boundary");
    QcommonPreinitAction none;
    Require(!machine.NextAction(none), "ready startup issues no database action");
}

void TestActionOwnershipAndCancellation()
{
    QcommonPreinitMachine machine;
    Require(!machine.Start(0u), "zero generation is rejected");
    Require(machine.Start(1u), "first generation starts");
    Require(!machine.Start(2u), "running generation cannot be replaced implicitly");

    QcommonPreinitAction memory;
    Require(machine.NextAction(memory), "memory action begins");
    QcommonPreinitAction blocked{99u, QcommonActionKind::ReadFile, "sentinel", 1u, 1u};
    Require(!machine.NextAction(blocked), "a second action cannot overlap");
    Require(blocked.token == 0u && blocked.path == nullptr,
        "failed action request clears its output");
    Require(!machine.CompleteLocal(memory.token + 1u, true),
        "foreign completion token is ignored");
    Require(machine.Snapshot().actionPending, "foreign completion preserves pending ownership");

    machine.Cancel();
    Require(machine.Snapshot().stage == QcommonPreinitStage::Cancelled &&
            machine.Snapshot().error == QcommonPreinitError::Cancelled,
        "cancellation is explicit and terminal");
    Require(!machine.NextAction(blocked), "cancelled startup issues no action");
    Require(machine.Start(2u), "a cancelled machine can start a fresh generation");
    Require(machine.Snapshot().filesChecked == 0u && machine.Snapshot().actionsIssued == 0u,
        "restart resets prior generation metrics");
}

void TestLocalFailures()
{
    for (QcommonPreinitError expected : {
            QcommonPreinitError::MemoryInitialization,
            QcommonPreinitError::EventInitialization,
            QcommonPreinitError::CommandInitialization})
    {
        QcommonPreinitMachine machine;
        Require(machine.Start(1u), "local failure fixture starts");
        while (true)
        {
            QcommonPreinitAction action;
            Require(machine.NextAction(action), "local failure action exists");
            const bool isTarget =
                (expected == QcommonPreinitError::MemoryInitialization &&
                 action.kind == QcommonActionKind::InitializeMemory) ||
                (expected == QcommonPreinitError::EventInitialization &&
                 action.kind == QcommonActionKind::InitializeEvents) ||
                (expected == QcommonPreinitError::CommandInitialization &&
                 action.kind == QcommonActionKind::RegisterCommands);
            Require(machine.CompleteLocal(action.token, !isTarget),
                "local failure completion is accepted");
            if (isTarget)
            {
                break;
            }
        }
        Require(machine.Snapshot().stage == QcommonPreinitStage::Failed &&
                machine.Snapshot().error == expected,
            "local initialization reports its exact failure");
    }
}

void TestFilesystemFailures()
{
    const auto files = kisak::web::QcommonStartupFiles();

    {
        QcommonPreinitMachine machine;
        Require(machine.Start(1u), "stat failure fixture starts");
        CompleteLocalInitialization(machine);
        QcommonPreinitAction stat;
        Require(machine.NextAction(stat), "stat failure action begins");
        Require(machine.CompleteStat(stat.token, false, 0u), "failed stat is consumed");
        Require(machine.Snapshot().error == QcommonPreinitError::FilesystemStat,
            "failed stat has a typed error");
    }
    {
        QcommonPreinitMachine machine;
        Require(machine.Start(1u), "small file fixture starts");
        CompleteLocalInitialization(machine);
        QcommonPreinitAction stat;
        Require(machine.NextAction(stat), "small file stat begins");
        Require(machine.CompleteStat(stat.token, true, files.front().probeBytes - 1u),
            "short stat is consumed");
        Require(machine.Snapshot().error == QcommonPreinitError::FileTooSmall,
            "short file is rejected before reading");
    }
    for (std::size_t target : {std::size_t{0u}, std::size_t{1u}, files.size() - 1u})
    {
        QcommonPreinitMachine machine;
        Require(machine.Start(1u), "header failure fixture starts");
        CompleteLocalInitialization(machine);
        for (std::size_t index = 0u; index < target; ++index)
        {
            CompleteOneFile(machine, files[index]);
        }
        QcommonPreinitAction stat;
        Require(machine.NextAction(stat), "target stat begins");
        Require(machine.CompleteStat(stat.token, true, files[target].probeBytes),
            "target stat completes");
        QcommonPreinitAction read;
        Require(machine.NextAction(read), "target read begins");
        auto bytes = ValidProbe(files[target]);
        bytes[0] ^= 0xffu;
        Require(machine.CompleteRead(
                read.token,
                true,
                std::span<const std::uint8_t>(bytes.data(), files[target].probeBytes)),
            "malformed target read is consumed");
        Require(machine.Snapshot().error == QcommonPreinitError::HeaderMismatch &&
                machine.Snapshot().filesChecked == target,
            "localization, IWD, and fastfile mismatches stop before publication");
    }
}

void TestStrings()
{
    for (QcommonPreinitStage stage : {
            QcommonPreinitStage::Idle,
            QcommonPreinitStage::Memory,
            QcommonPreinitStage::Events,
            QcommonPreinitStage::Commands,
            QcommonPreinitStage::FilesystemStat,
            QcommonPreinitStage::FilesystemRead,
            QcommonPreinitStage::PreDatabase,
            QcommonPreinitStage::Failed,
            QcommonPreinitStage::Cancelled})
    {
        Require(std::string_view(kisak::web::QcommonPreinitStageString(stage)) != "unknown",
            "every stage has a stable diagnostic string");
    }
    for (QcommonPreinitError error : {
            QcommonPreinitError::None,
            QcommonPreinitError::InvalidState,
            QcommonPreinitError::ActionPending,
            QcommonPreinitError::InvalidCompletion,
            QcommonPreinitError::MemoryInitialization,
            QcommonPreinitError::EventInitialization,
            QcommonPreinitError::CommandInitialization,
            QcommonPreinitError::FilesystemStat,
            QcommonPreinitError::FileTooSmall,
            QcommonPreinitError::FilesystemRead,
            QcommonPreinitError::HeaderMismatch,
            QcommonPreinitError::Cancelled})
    {
        Require(std::string_view(kisak::web::QcommonPreinitErrorString(error)) !=
                "unknown startup error",
            "every error has a stable diagnostic string");
    }
}
} // namespace

int main()
{
    TestProfileAndSuccessfulSequence();
    TestActionOwnershipAndCancellation();
    TestLocalFailures();
    TestFilesystemFailures();
    TestStrings();
    std::cout << "web_qcommon_preinit_tests: all checks passed\n";
    return 0;
}
