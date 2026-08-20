#include "web/web_browser_bindings.h"

#include <ui/keycodes.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
std::map<std::uint32_t, std::string> bindings;
std::vector<std::pair<std::uint32_t, std::string>> writes;

const char *lookup(std::uint32_t key)
{
    const auto found = bindings.find(key);
    return found == bindings.end() ? nullptr : found->second.c_str();
}

void setter(std::uint32_t key, const char *command)
{
    writes.emplace_back(key, command);
    bindings[key] = command;
}
}

int main()
{
    std::size_t count = 0;
    const WebBrowserDefaultBinding *defaults = WebBrowserDefaultBindings(&count);
    constexpr std::array<WebBrowserDefaultBinding, 11> expected = {{
        {'w', "+forward"}, {'s', "+back"}, {'a', "+moveleft"},
        {'d', "+moveright"}, {K_SPACE, "+gostand"}, {K_SHIFT, "+sprint"},
        {K_MOUSE1, "+attack"}, {K_MOUSE2, "toggleads"}, {'r', "+reload"},
        {K_MWHEELUP, "weapnext"}, {K_MWHEELDOWN, "weapprev"},
    }};
    assert(count == expected.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        assert(defaults[i].key == expected[i].key);
        assert(std::strcmp(defaults[i].command, expected[i].command) == 0);
    }

    bindings.clear();
    writes.clear();
    assert(InstallWebBrowserDefaultBindings(lookup, setter) == count);
    assert(writes.size() == count);
    assert(bindings[static_cast<std::uint32_t>('r')] == "+reload");

    bindings.clear();
    bindings['r'] = "+custom_reload";
    bindings[0xCE] = "weaplast";
    bindings[0xCD] = "+custom_previous";
    writes.clear();
    assert(InstallWebBrowserDefaultBindings(lookup, setter) == count - 3);
    assert(bindings['r'] == "+custom_reload");
    assert(bindings[0xCE] == "weaplast");
    assert(bindings[0xCD] == "+custom_previous");
    assert(writes.size() == count - 3);

    bindings.clear();
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::string custom = "custom_binding_" + std::to_string(i);
        bindings[static_cast<std::uint32_t>(defaults[i].key)] = custom;
    }
    writes.clear();
    assert(InstallWebBrowserDefaultBindings(lookup, setter) == 0);
    assert(writes.empty());
    for (std::size_t i = 0; i < count; ++i)
        assert(bindings[static_cast<std::uint32_t>(defaults[i].key)] ==
            "custom_binding_" + std::to_string(i));
    return 0;
}
