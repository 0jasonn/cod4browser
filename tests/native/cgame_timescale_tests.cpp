#include <qcommon/cmd.h>

#include <array>
#include <cassert>
#include <cstdio>

void __cdecl CG_SlowServerCommand(int localClientNum);

CmdArgs cmd_args{};
static std::array<const char *, 4> arguments{};
static int capturedClient;
static int capturedDuration;
static double capturedStart;
static double capturedEnd;

const char *__cdecl Cmd_Argv(int index)
{
    return arguments.at(index);
}

void __cdecl CG_AlterTimescale(int client, int duration, double start, double end)
{
    capturedClient = client;
    capturedDuration = duration;
    capturedStart = start;
    capturedEnd = end;
}

int main()
{
    // Synthetic canonical server commands; no retail data. In Wasm a long
    // double is 16 bytes, so aliasing its low bytes as a double corrupts 1.0.
    struct Case { const char *start; const char *end; double expectedStart; double expectedEnd; };
    for (const Case &scales : {Case{"1", "1", 1.0, 1.0},
             Case{"1", "0.25", 1.0, 0.25}, Case{"0.25", "1", 0.25, 1.0},
             Case{"0.5", "2", 0.5, 2.0}})
    {
        arguments = {"slow", "750", scales.start, scales.end};
        CG_SlowServerCommand(0);
        assert(capturedClient == 0 && capturedDuration == 750);
        assert(capturedStart == scales.expectedStart);
        assert(capturedEnd == scales.expectedEnd);
    }
    std::puts("cgame timescale: normal, slow, restore, and speed-up commands passed");
}
