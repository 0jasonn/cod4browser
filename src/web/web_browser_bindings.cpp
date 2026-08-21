#include "web_browser_bindings.h"

#include <ui/keycodes.h>

namespace
{
constexpr WebBrowserDefaultBinding DEFAULT_BINDINGS[] = {
    {'w', "+forward"},
    {'s', "+back"},
    {'a', "+moveleft"},
    {'d', "+moveright"},
    {'f', "+activate"},
    {K_SPACE, "+gostand"},
    {K_SHIFT, "+sprint"},
    {K_MOUSE1, "+attack"},
    {K_MOUSE2, "toggleads"},
    {'r', "+reload"},
    {K_MWHEELUP, "weapnext"},
    {K_MWHEELDOWN, "weapprev"},
};
}
const WebBrowserDefaultBinding *WebBrowserDefaultBindings(std::size_t *count)
{
    if (count)
        *count = sizeof(DEFAULT_BINDINGS) / sizeof(DEFAULT_BINDINGS[0]);
    return DEFAULT_BINDINGS;
}

std::uint32_t InstallWebBrowserDefaultBindings(
    WebBrowserBindingLookup lookup,
    WebBrowserBindingSetter setter)
{
    if (!lookup || !setter)
        return 0;

    std::size_t count = 0;
    const WebBrowserDefaultBinding *bindings = WebBrowserDefaultBindings(&count);
    std::uint32_t installed = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const WebBrowserDefaultBinding &binding = bindings[i];
        const char *current = lookup(static_cast<std::uint32_t>(binding.key));
        if (current && *current)
            continue;
        setter(static_cast<std::uint32_t>(binding.key), binding.command);
        ++installed;
    }
    return installed;
}
