#include <qcommon/qcommon_math.h>
#include <game/g_active_math.h>
#include <universal/com_random.h>

#include <array>
#include <cassert>
#include <utility>

int main()
{
    static_assert(Q_RandomToUnitFloat(0u, 32767u) == 0.0f);
    static_assert(Q_RandomToUnitFloat(16384u, 32767u) == 0.5f);
    static_assert(Q_RandomToUnitFloat(32767u, 32767u) ==
        32767.0f / 32768.0f);
    static_assert(Q_RandomToUnitFloat(2147483647u, 2147483647u) ==
        32767.0f / 32768.0f);
    static_assert(G_RoundPlayerGravity(1.0f) == 1);
    static_assert(G_RoundPlayerGravity(799.49f) == 799);
    static_assert(G_RoundPlayerGravity(799.5f) == 800);
    static_assert(G_RoundPlayerGravity(800.0f) == 800);

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
