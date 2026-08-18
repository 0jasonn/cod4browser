#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kisak::web
{
inline constexpr std::size_t QCOMMON_STARTUP_FILE_COUNT = 25u;
inline constexpr std::uint32_t QCOMMON_STARTUP_ARENA_BYTES = 256u * 1024u;
inline constexpr std::uint32_t QCOMMON_EVENT_CAPACITY = 64u;
inline constexpr std::uint32_t QCOMMON_COMMAND_DVAR_COUNT = 5u;

enum class QcommonFileKind : std::uint8_t
{
    Localization,
    Iwd,
    Fastfile,
};

struct QcommonStartupFile
{
    const char *path;
    QcommonFileKind kind;
    std::uint32_t probeBytes;
};

std::span<const QcommonStartupFile> QcommonStartupFiles() noexcept;

enum class QcommonPreinitStage : std::uint8_t
{
    Idle,
    Memory,
    Events,
    Commands,
    FilesystemStat,
    FilesystemRead,
    PreDatabase,
    Failed,
    Cancelled,
};

enum class QcommonActionKind : std::uint8_t
{
    InitializeMemory,
    InitializeEvents,
    RegisterCommands,
    StatFile,
    ReadFile,
};

enum class QcommonPreinitError : std::uint8_t
{
    None,
    InvalidState,
    ActionPending,
    InvalidCompletion,
    MemoryInitialization,
    EventInitialization,
    CommandInitialization,
    FilesystemStat,
    FileTooSmall,
    FilesystemRead,
    HeaderMismatch,
    Cancelled,
};

struct QcommonPreinitAction
{
    std::uint32_t token = 0;
    QcommonActionKind kind = QcommonActionKind::InitializeMemory;
    const char *path = nullptr;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

struct QcommonPreinitSnapshot
{
    QcommonPreinitStage stage = QcommonPreinitStage::Idle;
    QcommonPreinitError error = QcommonPreinitError::None;
    std::uint32_t generation = 0;
    std::uint32_t actionsIssued = 0;
    std::uint32_t filesChecked = 0;
    std::uint32_t totalFiles = static_cast<std::uint32_t>(QCOMMON_STARTUP_FILE_COUNT);
    std::uint32_t probeBytesRead = 0;
    std::uint32_t arenaBytes = 0;
    std::uint32_t eventCapacity = 0;
    std::uint32_t commandDvarCount = 0;
    const char *currentPath = nullptr;
    bool actionPending = false;
};

const char *QcommonPreinitStageString(QcommonPreinitStage stage) noexcept;
const char *QcommonPreinitErrorString(QcommonPreinitError error) noexcept;

// A deterministic, input-bounded startup sequencer. It owns no platform I/O:
// callers request at most one action per frame and complete asynchronous file
// actions later. Retail fastfiles are header-checked only and never inflated.
class QcommonPreinitMachine
{
public:
    bool Start(std::uint32_t generation) noexcept;
    void Cancel() noexcept;

    bool NextAction(QcommonPreinitAction &action) noexcept;
    bool CompleteLocal(std::uint32_t token, bool success) noexcept;
    bool CompleteStat(
        std::uint32_t token,
        bool success,
        std::uint32_t fileSize) noexcept;
    bool CompleteRead(
        std::uint32_t token,
        bool success,
        std::span<const std::uint8_t> bytes) noexcept;

    QcommonPreinitSnapshot Snapshot() const noexcept;
    bool Running() const noexcept;
    bool Ready() const noexcept;

private:
    bool CompleteToken(std::uint32_t token, QcommonActionKind expected) noexcept;
    void Fail(QcommonPreinitError error) noexcept;

    QcommonPreinitStage stage_ = QcommonPreinitStage::Idle;
    QcommonPreinitError error_ = QcommonPreinitError::None;
    std::uint32_t generation_ = 0;
    std::uint32_t nextToken_ = 0;
    std::uint32_t pendingToken_ = 0;
    QcommonActionKind pendingKind_ = QcommonActionKind::InitializeMemory;
    std::size_t fileIndex_ = 0;
    std::uint32_t currentFileSize_ = 0;
    std::uint32_t actionsIssued_ = 0;
    std::uint32_t filesChecked_ = 0;
    std::uint32_t probeBytesRead_ = 0;
    std::uint32_t arenaBytes_ = 0;
    std::uint32_t eventCapacity_ = 0;
    std::uint32_t commandDvarCount_ = 0;
};
} // namespace kisak::web
