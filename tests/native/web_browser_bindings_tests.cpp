#include "web/web_browser_bindings.h"

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
    assert(count == 11);
    assert(defaults[8].key == 'r' && std::strcmp(defaults[8].command, "+reload") == 0);
    assert(defaults[9].key == 0xCE && std::strcmp(defaults[9].command, "weapnext") == 0);
    assert(defaults[10].key == 0xCD && std::strcmp(defaults[10].command, "weapprev") == 0);

    bindings.clear();
    writes.clear();
    assert(InstallWebBrowserDefaultBindings(lookup, setter) == count);
    assert(writes.size() == count);
    assert(bindings[static_cast<std::uint32_t>('r')] == "+reload");

    bindings.clear();
    bindings['r'] = "+custom_reload";
    bindings[0xCE] = "weaplast";
    writes.clear();
    assert(InstallWebBrowserDefaultBindings(lookup, setter) == count - 2);
    assert(bindings['r'] == "+custom_reload");
    assert(bindings[0xCE] == "weaplast");
    assert(writes.size() == count - 2);

    for (const WebBrowserDefaultBinding &binding :
        std::vector<WebBrowserDefaultBinding>(defaults, defaults + count))
    {
        bindings[static_cast<std::uint32_t>(binding.key)] = binding.command;
    }
    writes.clear();
    assert(InstallWebBrowserDefaultBindings(lookup, setter) == 0);
    assert(writes.empty());
    return 0;
}
