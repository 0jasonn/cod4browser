#pragma once

#include <cstdint>

#define SCR_FUNC_TABLE_SIZE 1024

struct scrCompilePub_t
{
    int value_count;
    int far_function_count;
    std::uint32_t loadedscripts;
    std::uint32_t scripts;
    std::uint32_t builtinFunc;
    std::uint32_t builtinMeth;
    short canonicalStrings[65536];
    const char *in_ptr;
    const char *parseBuf;
    bool script_loading;
    bool allowedBreakpoint;
    int developer_statement;
    unsigned char *opcodePos;
    std::uint32_t programLen;
    int func_table_size;
    int func_table[SCR_FUNC_TABLE_SIZE];
};

extern scrCompilePub_t scrCompilePub;
