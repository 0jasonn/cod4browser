#include <qcommon/qcommon_math.h>

#include <array>
#include <cassert>
#include <utility>

int main()
{
    constexpr std::array<std::pair<float, int>, 14> cases{{
        {-3.5f, -4},
        {-2.5f, -2},
        {-1.6f, -2},
        {-1.5f, -2},
        {-0.5f, 0},
        {-0.4f, 0},
        {0.0f, 0},
        {0.4f, 0},
        {0.5f, 0},
        {1.5f, 2},
        {1.6f, 2},
        {2.5f, 2},
        {3.5f, 4},
        {12345.25f, 12345},
    }};

    for (const auto &[input, expected] : cases)
    {
        assert(SnapFloatToInt(input) == expected);
        assert(SnapFloat(input) == static_cast<float>(expected));
    }

    return 0;
}
