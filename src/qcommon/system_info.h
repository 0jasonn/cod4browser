#pragma once

struct __declspec(align(8)) SysInfo
{
    long double cpuGHz;
    long double configureGHz;
    int logicalCpuCount;
    int physicalCpuCount;
    int sysMB;
    char gpuDescription[512];
    bool SSE;
    char cpuVendor[13];
    char cpuName[49];
};

extern SysInfo sys_info;
