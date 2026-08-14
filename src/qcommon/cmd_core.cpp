#include <universal/q_shared.h>

#include <qcommon/cmd.h>
#include <qcommon/common_api.h>
#include <universal/dvar.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace
{
constexpr int LOCAL_CLIENT_COUNT = 1;
constexpr int COMMAND_BUFFER_SIZE = 65536;
constexpr int MAX_COMMAND_LINE = 4095;

cmd_function_s *g_cmdFunctions = nullptr;
cmd_function_s g_cmdListCommand{};
cmd_function_s g_waitCommand{};
CmdArgsPrivate g_cmdArgsPrivate{};
CmdArgsPrivate g_svCmdArgsPrivate{};
CmdText g_cmdText[LOCAL_CLIENT_COUNT]{};
uint8_t g_cmdTextBuffer[LOCAL_CLIENT_COUNT][COMMAND_BUFFER_SIZE]{};
bool g_insideCbufExecute[LOCAL_CLIENT_COUNT]{};
int g_cmdWait = 0;

unsigned char AsciiLower(unsigned char value)
{
    if (value >= 'A' && value <= 'Z')
    {
        return static_cast<unsigned char>(value + ('a' - 'A'));
    }
    return value;
}

bool CommandNamesEqual(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs)
    {
        return lhs == rhs;
    }

    while (*lhs && *rhs)
    {
        if (AsciiLower(static_cast<unsigned char>(*lhs)) !=
            AsciiLower(static_cast<unsigned char>(*rhs)))
        {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

void ResetArgs(CmdArgs *args, CmdArgsPrivate *privateArgs)
{
    std::memset(args, 0, sizeof(*args));
    std::memset(privateArgs, 0, sizeof(*privateArgs));
    args->nesting = -1;
}

bool ValidLocalClient(int localClientNum)
{
    if (localClientNum >= 0 && localClientNum < LOCAL_CLIENT_COUNT)
    {
        return true;
    }
    Com_Printf(0, "command buffer: invalid local client %d\n", localClientNum);
    return false;
}

bool AppendTokenCharacter(CmdArgsPrivate *privateArgs, char value)
{
    constexpr int capacity = sizeof(privateArgs->textPool);
    if (privateArgs->totalUsedTextPool >= capacity - 1)
    {
        return false;
    }
    privateArgs->textPool[privateArgs->totalUsedTextPool++] = value;
    return true;
}

void FinishToken(CmdArgsPrivate *privateArgs)
{
    constexpr int capacity = sizeof(privateArgs->textPool);
    const int terminator = std::min(privateArgs->totalUsedTextPool, capacity - 1);
    privateArgs->textPool[terminator] = '\0';
    if (privateArgs->totalUsedTextPool < capacity)
    {
        ++privateArgs->totalUsedTextPool;
    }
}

bool QuoteIsEscaped(const uint8_t *start, const uint8_t *quote)
{
    std::size_t slashCount = 0;
    while (quote > start && quote[-1] == '\\')
    {
        ++slashCount;
        --quote;
    }
    return (slashCount & 1u) != 0;
}

std::size_t FindCommandEnd(const uint8_t *text, std::size_t length)
{
    bool quoted = false;
    for (std::size_t index = 0; index < length; ++index)
    {
        if (text[index] == '"' && !QuoteIsEscaped(text, text + index))
        {
            quoted = !quoted;
        }
        if ((!quoted && text[index] == ';') || text[index] == '\n' || text[index] == '\r')
        {
            return index;
        }
    }
    return length;
}

void ExecuteSegment(
    int localClientNum,
    int controllerIndex,
    const uint8_t *segment,
    std::size_t segmentLength)
{
    char command[MAX_COMMAND_LINE + 1]{};
    const std::size_t copyLength = std::min<std::size_t>(segmentLength, MAX_COMMAND_LINE);
    if (segmentLength > MAX_COMMAND_LINE)
    {
        Com_Printf(
            0,
            "command line exceeded %d bytes and was truncated\n",
            MAX_COMMAND_LINE);
    }
    std::memcpy(command, segment, copyLength);
    command[copyLength] = '\0';
    Cmd_ExecuteSingleCommand(localClientNum, controllerIndex, command);
}
} // namespace

CmdArgs cmd_args{};
CmdArgs sv_cmd_args{};

const char *Cmd_Argv(int argIndex)
{
    if (cmd_args.nesting < 0 || cmd_args.nesting >= CMD_MAX_NESTING || argIndex < 0 ||
        argIndex >= cmd_args.argc[cmd_args.nesting])
    {
        return "";
    }
    return cmd_args.argv[cmd_args.nesting][argIndex];
}

int Cmd_Argc()
{
    if (cmd_args.nesting < 0 || cmd_args.nesting >= CMD_MAX_NESTING)
    {
        return 0;
    }
    return cmd_args.argc[cmd_args.nesting];
}

int SV_Cmd_Argc()
{
    if (sv_cmd_args.nesting < 0 || sv_cmd_args.nesting >= CMD_MAX_NESTING)
    {
        return 0;
    }
    return sv_cmd_args.argc[sv_cmd_args.nesting];
}

const char *SV_Cmd_Argv(int argIndex)
{
    if (sv_cmd_args.nesting < 0 || sv_cmd_args.nesting >= CMD_MAX_NESTING || argIndex < 0 ||
        argIndex >= sv_cmd_args.argc[sv_cmd_args.nesting])
    {
        return "";
    }
    return sv_cmd_args.argv[sv_cmd_args.nesting][argIndex];
}

int Cmd_LocalClientNum()
{
    if (cmd_args.nesting < 0 || cmd_args.nesting >= CMD_MAX_NESTING)
    {
        return 0;
    }
    return cmd_args.localClientNum[cmd_args.nesting];
}

void Cmd_Wait_f()
{
    int frames = Cmd_Argc() == 2 ? std::atoi(Cmd_Argv(1)) : 1;
    g_cmdWait = std::max(frames, 1);
}

void Cmd_List_f()
{
    int count = 0;
    for (cmd_function_s *command = g_cmdFunctions; command; command = command->next)
    {
        Com_Printf(0, "%s\n", command->name);
        ++count;
    }
    Com_Printf(0, "%d commands\n", count);
}

void Cmd_Init()
{
    ResetArgs(&cmd_args, &g_cmdArgsPrivate);
    ResetArgs(&sv_cmd_args, &g_svCmdArgsPrivate);
    g_cmdWait = 0;
    Cmd_AddCommandInternal("cmdlist", Cmd_List_f, &g_cmdListCommand);
    Cmd_AddCommandInternal("wait", Cmd_Wait_f, &g_waitCommand);
}

void Cmd_AddCommandInternal(
    const char *cmdName,
    void(__cdecl *function)(),
    cmd_function_s *allocatedCommand)
{
    if (!cmdName || !*cmdName || !allocatedCommand)
    {
        Com_Printf(0, "Cmd_AddCommand: invalid command registration\n");
        return;
    }

    if (cmd_function_s *existing = Cmd_FindCommand(cmdName))
    {
        if (existing != allocatedCommand)
        {
            Com_Printf(0, "Cmd_AddCommand: %s already defined\n", cmdName);
        }
        else if (function)
        {
            existing->function = function;
        }
        return;
    }

    allocatedCommand->name = cmdName;
    allocatedCommand->function = function;
    allocatedCommand->autoCompleteDir = nullptr;
    allocatedCommand->autoCompleteExt = nullptr;
    allocatedCommand->next = g_cmdFunctions;
    g_cmdFunctions = allocatedCommand;
}

cmd_function_s *Cmd_FindCommand(const char *cmdName)
{
    for (cmd_function_s *command = g_cmdFunctions; command; command = command->next)
    {
        if (CommandNamesEqual(cmdName, command->name))
        {
            return command;
        }
    }
    return nullptr;
}

void Cmd_RemoveCommand(const char *cmdName)
{
    cmd_function_s **link = &g_cmdFunctions;
    while (*link)
    {
        if (CommandNamesEqual(cmdName, (*link)->name))
        {
            cmd_function_s *removed = *link;
            *link = removed->next;
            removed->next = nullptr;
            return;
        }
        link = &(*link)->next;
    }
}

void Cmd_Shutdown()
{
    g_cmdFunctions = nullptr;
    g_cmdListCommand = {};
    g_waitCommand = {};
    ResetArgs(&cmd_args, &g_cmdArgsPrivate);
    ResetArgs(&sv_cmd_args, &g_svCmdArgsPrivate);
    g_cmdWait = 0;
}

void Cmd_ForEach(void(__cdecl *callback)(const char *))
{
    if (!callback)
    {
        return;
    }
    for (cmd_function_s *command = g_cmdFunctions; command; command = command->next)
    {
        callback(command->name);
    }
}

void Cmd_ComErrorCleanup()
{
    ResetArgs(&cmd_args, &g_cmdArgsPrivate);
    ResetArgs(&sv_cmd_args, &g_svCmdArgsPrivate);
    g_cmdWait = 0;
}

void Cbuf_Init()
{
    for (int client = 0; client < LOCAL_CLIENT_COUNT; ++client)
    {
        g_cmdText[client].data = g_cmdTextBuffer[client];
        g_cmdText[client].maxsize = COMMAND_BUFFER_SIZE;
        g_cmdText[client].cmdsize = 0;
        g_insideCbufExecute[client] = false;
    }
}

void Cbuf_AddText(int localClientNum, const char *text)
{
    if (!ValidLocalClient(localClientNum) || !text)
    {
        return;
    }

    CmdText &buffer = g_cmdText[localClientNum];
    const std::size_t length = std::strlen(text);
    if (length >= static_cast<std::size_t>(buffer.maxsize - buffer.cmdsize))
    {
        Com_Printf(0, "Cbuf_AddText: overflow\n");
        return;
    }

    std::memcpy(buffer.data + buffer.cmdsize, text, length);
    buffer.cmdsize += static_cast<int>(length);
    buffer.data[buffer.cmdsize] = '\0';
}

void Cbuf_InsertText(int localClientNum, const char *text)
{
    if (!ValidLocalClient(localClientNum) || !text)
    {
        return;
    }

    CmdText &buffer = g_cmdText[localClientNum];
    const std::size_t length = std::strlen(text);
    const std::size_t insertedLength = length + 1;
    if (insertedLength > static_cast<std::size_t>(buffer.maxsize - buffer.cmdsize))
    {
        Com_Printf(0, "Cbuf_InsertText: overflow\n");
        return;
    }

    std::memmove(
        buffer.data + insertedLength,
        buffer.data,
        static_cast<std::size_t>(buffer.cmdsize));
    std::memcpy(buffer.data, text, length);
    buffer.data[length] = '\n';
    buffer.cmdsize += static_cast<int>(insertedLength);
    if (buffer.cmdsize < buffer.maxsize)
    {
        buffer.data[buffer.cmdsize] = '\0';
    }
}

void Cbuf_ExecuteBuffer(int localClientNum, int controllerIndex, const char *buffer)
{
    if (!ValidLocalClient(localClientNum) || !buffer)
    {
        return;
    }

    const uint8_t *cursor = reinterpret_cast<const uint8_t *>(buffer);
    std::size_t remaining = std::strlen(buffer);
    while (remaining)
    {
        const std::size_t commandLength = FindCommandEnd(cursor, remaining);
        ExecuteSegment(localClientNum, controllerIndex, cursor, commandLength);
        const std::size_t consumed = commandLength + (commandLength < remaining ? 1 : 0);
        cursor += consumed;
        remaining -= consumed;
    }
}

void Cbuf_Execute(int localClientNum, int controllerIndex)
{
    if (!ValidLocalClient(localClientNum))
    {
        return;
    }
    if (g_insideCbufExecute[localClientNum])
    {
        Com_Printf(0, "Nesting Cbuf_Execute() is not allowed.\n");
        return;
    }

    g_insideCbufExecute[localClientNum] = true;
    Cbuf_ExecuteInternal(localClientNum, controllerIndex);
    g_insideCbufExecute[localClientNum] = false;
}

void Cbuf_ExecuteInternal(int localClientNum, int controllerIndex)
{
    if (!ValidLocalClient(localClientNum))
    {
        return;
    }

    CmdText &buffer = g_cmdText[localClientNum];
    while (buffer.cmdsize > 0)
    {
        if (g_cmdWait > 0)
        {
            --g_cmdWait;
            break;
        }

        const std::size_t commandLength =
            FindCommandEnd(buffer.data, static_cast<std::size_t>(buffer.cmdsize));
        const std::size_t consumed =
            commandLength + (commandLength < static_cast<std::size_t>(buffer.cmdsize) ? 1 : 0);

        uint8_t command[MAX_COMMAND_LINE + 1]{};
        const std::size_t copyLength = std::min<std::size_t>(commandLength, MAX_COMMAND_LINE);
        if (commandLength > MAX_COMMAND_LINE)
        {
            Com_Printf(
                0,
                "command line exceeded %d bytes and was truncated\n",
                MAX_COMMAND_LINE);
        }
        std::memcpy(command, buffer.data, copyLength);
        command[copyLength] = '\0';

        buffer.cmdsize -= static_cast<int>(consumed);
        std::memmove(
            buffer.data,
            buffer.data + consumed,
            static_cast<std::size_t>(buffer.cmdsize));
        if (buffer.cmdsize < buffer.maxsize)
        {
            buffer.data[buffer.cmdsize] = '\0';
        }

        Cmd_ExecuteSingleCommand(
            localClientNum,
            controllerIndex,
            reinterpret_cast<char *>(command));
    }
}

void Cmd_TokenizeStringWithLimit(char *text, int maxTokens)
{
    Cmd_TokenizeStringKernel(text, maxTokens, &cmd_args, &g_cmdArgsPrivate);
}

void Cmd_TokenizeStringKernel(
    char *text,
    int maxTokens,
    CmdArgs *args,
    CmdArgsPrivate *privateArgs)
{
    if (!text || !args || !privateArgs || maxTokens <= 0)
    {
        return;
    }
    if (args->nesting + 1 >= CMD_MAX_NESTING)
    {
        Com_Printf(0, "Cmd_TokenizeString: nesting limit exceeded\n");
        return;
    }

    ++args->nesting;
    const int nesting = args->nesting;
    privateArgs->usedTextPool[nesting] = -privateArgs->totalUsedTextPool;
    args->localClientNum[nesting] = -1;
    args->controllerIndex[nesting] = 0;
    args->argv[nesting] = &privateArgs->argvPool[privateArgs->totalUsedArgvPool];

    const int availableTokens =
        static_cast<int>(std::size(privateArgs->argvPool)) - privateArgs->totalUsedArgvPool;
    args->argc[nesting] = Cmd_TokenizeStringInternal(
        text,
        std::min(maxTokens, availableTokens),
        args->argv[nesting],
        privateArgs);
    privateArgs->totalUsedArgvPool += args->argc[nesting];
    privateArgs->usedTextPool[nesting] += privateArgs->totalUsedTextPool;
}

int Cmd_TokenizeStringInternal(
    char *input,
    int maxTokens,
    const char **argv,
    CmdArgsPrivate *privateArgs)
{
    if (!input || !argv || !privateArgs || maxTokens <= 0)
    {
        return 0;
    }

    int argc = 0;
    const unsigned char *text = reinterpret_cast<const unsigned char *>(input);
    while (*text && argc < maxTokens)
    {
        while (*text && Cmd_IsWhiteSpaceChar(*text))
        {
            ++text;
        }
        if (!*text || (text[0] == '/' && text[1] == '/'))
        {
            break;
        }
        if (text[0] == '/' && text[1] == '*')
        {
            text += 2;
            while (*text && !(text[0] == '*' && text[1] == '/'))
            {
                ++text;
            }
            if (*text)
            {
                text += 2;
                continue;
            }
            break;
        }

        if (privateArgs->totalUsedTextPool >=
            static_cast<int>(sizeof(privateArgs->textPool)) - 1)
        {
            Com_Printf(0, "Cmd_TokenizeString: text pool exhausted\n");
            break;
        }

        argv[argc++] = &privateArgs->textPool[privateArgs->totalUsedTextPool];
        bool truncated = false;
        if (argc == maxTokens)
        {
            // Preserve the legacy limited-token behavior: the last token owns
            // the untouched remainder of the command line, including spaces.
            while (*text)
            {
                truncated |= !AppendTokenCharacter(privateArgs, static_cast<char>(*text++));
            }
            FinishToken(privateArgs);
            if (truncated)
            {
                Com_Printf(0, "Cmd_TokenizeString: token text was truncated\n");
            }
            break;
        }

        if (*text == '"')
        {
            ++text;
            while (*text && *text != '"')
            {
                if (*text == '\\' && text[1] == '"')
                {
                    ++text;
                }
                truncated |= !AppendTokenCharacter(privateArgs, static_cast<char>(*text++));
            }
            if (*text == '"')
            {
                ++text;
            }
        }
        else
        {
            while (*text && !Cmd_IsWhiteSpaceChar(*text) &&
                !(text[0] == '/' && (text[1] == '/' || text[1] == '*')))
            {
                truncated |= !AppendTokenCharacter(privateArgs, static_cast<char>(*text++));
            }
        }
        FinishToken(privateArgs);
        if (truncated)
        {
            Com_Printf(0, "Cmd_TokenizeString: token text was truncated\n");
        }
    }
    return argc;
}

bool Cmd_IsWhiteSpaceChar(uint8_t letter)
{
    return letter != 20 && letter != 21 && letter != 22 && letter <= 0x20u;
}

void AssertCmdArgsConsistency(const CmdArgs *args, const CmdArgsPrivate *privateArgs)
{
    if (!args || !privateArgs)
    {
        return;
    }

    int usedArgv = 0;
    int usedText = 0;
    for (int nesting = 0; nesting <= args->nesting && nesting < CMD_MAX_NESTING; ++nesting)
    {
        usedArgv += args->argc[nesting];
        usedText += privateArgs->usedTextPool[nesting];
    }
    if (usedArgv != privateArgs->totalUsedArgvPool ||
        usedText != privateArgs->totalUsedTextPool)
    {
        Com_Printf(0, "command argument pool consistency check failed\n");
    }
}

void Cmd_TokenizeString(char *text)
{
    Cmd_TokenizeStringWithLimit(
        text,
        static_cast<int>(std::size(g_cmdArgsPrivate.argvPool)) -
            g_cmdArgsPrivate.totalUsedArgvPool);
}

void Cmd_EndTokenizedStringKernel(CmdArgs *args, CmdArgsPrivate *privateArgs)
{
    if (!args || !privateArgs || args->nesting < 0 || args->nesting >= CMD_MAX_NESTING)
    {
        return;
    }

    const int nesting = args->nesting;
    privateArgs->totalUsedArgvPool -= args->argc[nesting];
    privateArgs->totalUsedTextPool -= privateArgs->usedTextPool[nesting];
    args->argc[nesting] = 0;
    args->argv[nesting] = nullptr;
    --args->nesting;
}

void Cmd_EndTokenizedString()
{
    Cmd_EndTokenizedStringKernel(&cmd_args, &g_cmdArgsPrivate);
}

void SV_Cmd_TokenizeString(char *text)
{
    Cmd_TokenizeStringKernel(
        text,
        static_cast<int>(std::size(g_svCmdArgsPrivate.argvPool)) -
            g_svCmdArgsPrivate.totalUsedArgvPool,
        &sv_cmd_args,
        &g_svCmdArgsPrivate);
}

void SV_Cmd_EndTokenizedString()
{
    Cmd_EndTokenizedStringKernel(&sv_cmd_args, &g_svCmdArgsPrivate);
}

void Cmd_ExecuteSingleCommand(int localClientNum, int controllerIndex, char *text)
{
    if (!text)
    {
        return;
    }

    const int previousNesting = cmd_args.nesting;
    Cmd_TokenizeString(text);
    if (cmd_args.nesting == previousNesting)
    {
        return;
    }
    if (Cmd_Argc() == 0)
    {
        Cmd_EndTokenizedString();
        return;
    }

    cmd_args.localClientNum[cmd_args.nesting] = localClientNum;
    cmd_args.controllerIndex[cmd_args.nesting] = controllerIndex;
    const char *name = Cmd_Argv(0);

    bool handled = false;
    for (cmd_function_s *command = g_cmdFunctions; command; command = command->next)
    {
        if (CommandNamesEqual(name, command->name))
        {
            if (command->function)
            {
                command->function();
            }
            handled = true;
            break;
        }
    }
    if (!handled)
    {
        handled = Dvar_Command() != 0;
    }
    if (!handled)
    {
        Com_Printf(0, "Unknown command: %s\n", name);
    }

    Cmd_EndTokenizedString();
}
