#include <web/web_qcommon_preinit.h>

#include <algorithm>
#include <array>

namespace kisak::web
{
namespace
{
constexpr std::array<QcommonStartupFile, QCOMMON_STARTUP_FILE_COUNT> STARTUP_FILES = {{
    {"localization.txt", QcommonFileKind::Localization, 8u},
    {"main/iw_00.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_01.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_02.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_03.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_04.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_05.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_06.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_07.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_08.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_09.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_10.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_11.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_12.iwd", QcommonFileKind::Iwd, 4u},
    {"main/iw_13.iwd", QcommonFileKind::Iwd, 4u},
    {"main/localized_english_iw00.iwd", QcommonFileKind::Iwd, 4u},
    {"main/localized_english_iw01.iwd", QcommonFileKind::Iwd, 4u},
    {"main/localized_english_iw02.iwd", QcommonFileKind::Iwd, 4u},
    {"main/localized_english_iw03.iwd", QcommonFileKind::Iwd, 4u},
    {"main/localized_english_iw04.iwd", QcommonFileKind::Iwd, 4u},
    {"main/localized_english_iw05.iwd", QcommonFileKind::Iwd, 4u},
    {"main/localized_english_iw06.iwd", QcommonFileKind::Iwd, 4u},
    {"zone/english/code_post_gfx.ff", QcommonFileKind::Fastfile, 14u},
    {"zone/english/ui.ff", QcommonFileKind::Fastfile, 14u},
    {"zone/english/common.ff", QcommonFileKind::Fastfile, 14u},
}};

bool ValidHeader(QcommonFileKind kind, std::span<const std::uint8_t> bytes) noexcept
{
    if (kind == QcommonFileKind::Localization)
    {
        constexpr std::array<std::uint8_t, 7> ENGLISH = {
            'e', 'n', 'g', 'l', 'i', 's', 'h',
        };
        return bytes.size() == 8u &&
            std::equal(ENGLISH.begin(), ENGLISH.end(), bytes.begin()) &&
            (bytes[7] == '\r' || bytes[7] == '\n' || bytes[7] == 0u || bytes[7] == ' ');
    }
    if (kind == QcommonFileKind::Iwd)
    {
        constexpr std::array<std::uint8_t, 4> ZIP_LOCAL = {'P', 'K', 3u, 4u};
        return bytes.size() == ZIP_LOCAL.size() &&
            std::equal(ZIP_LOCAL.begin(), ZIP_LOCAL.end(), bytes.begin());
    }

    constexpr std::array<std::uint8_t, 8> FASTFILE_MAGIC = {
        'I', 'W', 'f', 'f', 'u', '1', '0', '0',
    };
    if (bytes.size() != 14u ||
        !std::equal(FASTFILE_MAGIC.begin(), FASTFILE_MAGIC.end(), bytes.begin()) ||
        bytes[8] != 5u || bytes[9] != 0u || bytes[10] != 0u || bytes[11] != 0u)
    {
        return false;
    }
    const std::uint32_t cmf = bytes[12];
    const std::uint32_t flg = bytes[13];
    return (cmf & 0x0fu) == 8u && (cmf >> 4u) <= 7u &&
        (flg & 0x20u) == 0u && ((cmf << 8u) + flg) % 31u == 0u;
}
} // namespace

std::span<const QcommonStartupFile> QcommonStartupFiles() noexcept
{
    return STARTUP_FILES;
}

const char *QcommonPreinitStageString(QcommonPreinitStage stage) noexcept
{
    switch (stage)
    {
    case QcommonPreinitStage::Idle: return "idle";
    case QcommonPreinitStage::Memory: return "memory";
    case QcommonPreinitStage::Events: return "events";
    case QcommonPreinitStage::Commands: return "commands";
    case QcommonPreinitStage::FilesystemStat: return "filesystem-stat";
    case QcommonPreinitStage::FilesystemRead: return "filesystem-read";
    case QcommonPreinitStage::PreDatabase: return "pre-database";
    case QcommonPreinitStage::Failed: return "failed";
    case QcommonPreinitStage::Cancelled: return "cancelled";
    }
    return "unknown";
}

const char *QcommonPreinitErrorString(QcommonPreinitError error) noexcept
{
    switch (error)
    {
    case QcommonPreinitError::None: return "none";
    case QcommonPreinitError::InvalidState: return "invalid startup state";
    case QcommonPreinitError::ActionPending: return "a startup action is already pending";
    case QcommonPreinitError::InvalidCompletion: return "startup completion did not match its action";
    case QcommonPreinitError::MemoryInitialization: return "bounded startup memory initialization failed";
    case QcommonPreinitError::EventInitialization: return "startup event queue initialization failed";
    case QcommonPreinitError::CommandInitialization: return "startup command/dvar registration failed";
    case QcommonPreinitError::FilesystemStat: return "startup file stat failed";
    case QcommonPreinitError::FileTooSmall: return "startup file is shorter than its bounded probe";
    case QcommonPreinitError::FilesystemRead: return "startup file read failed";
    case QcommonPreinitError::HeaderMismatch: return "startup file header did not match its validated type";
    case QcommonPreinitError::Cancelled: return "startup was cancelled";
    }
    return "unknown startup error";
}

bool QcommonPreinitMachine::Start(std::uint32_t generation) noexcept
{
    if (generation == 0u || Running())
    {
        return false;
    }
    *this = {};
    generation_ = generation;
    stage_ = QcommonPreinitStage::Memory;
    return true;
}

void QcommonPreinitMachine::Cancel() noexcept
{
    if (stage_ == QcommonPreinitStage::Idle)
    {
        return;
    }
    pendingToken_ = 0u;
    error_ = QcommonPreinitError::Cancelled;
    stage_ = QcommonPreinitStage::Cancelled;
}

bool QcommonPreinitMachine::NextAction(QcommonPreinitAction &action) noexcept
{
    action = {};
    if (!Running() || pendingToken_ != 0u)
    {
        return false;
    }

    QcommonPreinitAction next;
    switch (stage_)
    {
    case QcommonPreinitStage::Memory:
        next.kind = QcommonActionKind::InitializeMemory;
        break;
    case QcommonPreinitStage::Events:
        next.kind = QcommonActionKind::InitializeEvents;
        break;
    case QcommonPreinitStage::Commands:
        next.kind = QcommonActionKind::RegisterCommands;
        break;
    case QcommonPreinitStage::FilesystemStat:
        next.kind = QcommonActionKind::StatFile;
        next.path = STARTUP_FILES[fileIndex_].path;
        break;
    case QcommonPreinitStage::FilesystemRead:
        next.kind = QcommonActionKind::ReadFile;
        next.path = STARTUP_FILES[fileIndex_].path;
        next.length = STARTUP_FILES[fileIndex_].probeBytes;
        break;
    case QcommonPreinitStage::Idle:
    case QcommonPreinitStage::PreDatabase:
    case QcommonPreinitStage::Failed:
    case QcommonPreinitStage::Cancelled:
        return false;
    }

    nextToken_ = nextToken_ == UINT32_MAX ? 1u : nextToken_ + 1u;
    next.token = nextToken_;
    pendingToken_ = next.token;
    pendingKind_ = next.kind;
    ++actionsIssued_;
    action = next;
    return true;
}

bool QcommonPreinitMachine::CompleteToken(
    std::uint32_t token,
    QcommonActionKind expected) noexcept
{
    if (token == 0u || token != pendingToken_ || expected != pendingKind_)
    {
        return false;
    }
    pendingToken_ = 0u;
    return true;
}

bool QcommonPreinitMachine::CompleteLocal(std::uint32_t token, bool success) noexcept
{
    const QcommonActionKind expected = stage_ == QcommonPreinitStage::Memory
        ? QcommonActionKind::InitializeMemory
        : stage_ == QcommonPreinitStage::Events
            ? QcommonActionKind::InitializeEvents
            : QcommonActionKind::RegisterCommands;
    if ((stage_ != QcommonPreinitStage::Memory &&
         stage_ != QcommonPreinitStage::Events &&
         stage_ != QcommonPreinitStage::Commands) ||
        !CompleteToken(token, expected))
    {
        return false;
    }
    if (!success)
    {
        Fail(stage_ == QcommonPreinitStage::Memory
            ? QcommonPreinitError::MemoryInitialization
            : stage_ == QcommonPreinitStage::Events
                ? QcommonPreinitError::EventInitialization
                : QcommonPreinitError::CommandInitialization);
        return true;
    }

    if (stage_ == QcommonPreinitStage::Memory)
    {
        arenaBytes_ = QCOMMON_STARTUP_ARENA_BYTES;
        stage_ = QcommonPreinitStage::Events;
    }
    else if (stage_ == QcommonPreinitStage::Events)
    {
        eventCapacity_ = QCOMMON_EVENT_CAPACITY;
        stage_ = QcommonPreinitStage::Commands;
    }
    else
    {
        commandDvarCount_ = QCOMMON_COMMAND_DVAR_COUNT;
        stage_ = QcommonPreinitStage::FilesystemStat;
    }
    return true;
}

bool QcommonPreinitMachine::CompleteStat(
    std::uint32_t token,
    bool success,
    std::uint32_t fileSize) noexcept
{
    if (stage_ != QcommonPreinitStage::FilesystemStat ||
        !CompleteToken(token, QcommonActionKind::StatFile))
    {
        return false;
    }
    if (!success)
    {
        Fail(QcommonPreinitError::FilesystemStat);
        return true;
    }
    if (fileSize < STARTUP_FILES[fileIndex_].probeBytes)
    {
        Fail(QcommonPreinitError::FileTooSmall);
        return true;
    }
    currentFileSize_ = fileSize;
    stage_ = QcommonPreinitStage::FilesystemRead;
    return true;
}

bool QcommonPreinitMachine::CompleteRead(
    std::uint32_t token,
    bool success,
    std::span<const std::uint8_t> bytes) noexcept
{
    if (stage_ != QcommonPreinitStage::FilesystemRead ||
        !CompleteToken(token, QcommonActionKind::ReadFile))
    {
        return false;
    }
    if (!success || bytes.size() != STARTUP_FILES[fileIndex_].probeBytes)
    {
        Fail(QcommonPreinitError::FilesystemRead);
        return true;
    }
    if (!ValidHeader(STARTUP_FILES[fileIndex_].kind, bytes))
    {
        Fail(QcommonPreinitError::HeaderMismatch);
        return true;
    }

    probeBytesRead_ += static_cast<std::uint32_t>(bytes.size());
    ++filesChecked_;
    ++fileIndex_;
    currentFileSize_ = 0u;
    stage_ = fileIndex_ == STARTUP_FILES.size()
        ? QcommonPreinitStage::PreDatabase
        : QcommonPreinitStage::FilesystemStat;
    return true;
}

QcommonPreinitSnapshot QcommonPreinitMachine::Snapshot() const noexcept
{
    return {
        stage_,
        error_,
        generation_,
        actionsIssued_,
        filesChecked_,
        static_cast<std::uint32_t>(STARTUP_FILES.size()),
        probeBytesRead_,
        arenaBytes_,
        eventCapacity_,
        commandDvarCount_,
        fileIndex_ < STARTUP_FILES.size() ? STARTUP_FILES[fileIndex_].path : nullptr,
        pendingToken_ != 0u,
    };
}

bool QcommonPreinitMachine::Running() const noexcept
{
    return stage_ == QcommonPreinitStage::Memory ||
        stage_ == QcommonPreinitStage::Events ||
        stage_ == QcommonPreinitStage::Commands ||
        stage_ == QcommonPreinitStage::FilesystemStat ||
        stage_ == QcommonPreinitStage::FilesystemRead;
}

bool QcommonPreinitMachine::Ready() const noexcept
{
    return stage_ == QcommonPreinitStage::PreDatabase;
}

void QcommonPreinitMachine::Fail(QcommonPreinitError error) noexcept
{
    pendingToken_ = 0u;
    error_ = error;
    stage_ = QcommonPreinitStage::Failed;
}
} // namespace kisak::web
