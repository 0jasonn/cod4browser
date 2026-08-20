#pragma once

#include <cstddef>
#include <cstdint>

struct WebBrowserDefaultBinding
{
    int key;
    const char *command;
};

using WebBrowserBindingLookup = const char *(*)(std::uint32_t key);
using WebBrowserBindingSetter = void (*)(std::uint32_t key, const char *command);

const WebBrowserDefaultBinding *WebBrowserDefaultBindings(std::size_t *count);
std::uint32_t InstallWebBrowserDefaultBindings(
    WebBrowserBindingLookup lookup,
    WebBrowserBindingSetter setter);
