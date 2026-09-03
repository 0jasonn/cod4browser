#include <qcommon/qcommon_math.h>
#include <game/g_active_math.h>
#include <game/g_scr_main_math.h>
#include <universal/com_random.h>

#include <array>
#include <cassert>
#include <cmath>
#include <utility>

int main()
{
    const float quarterTurn[4]{0.0f, 0.0f, std::sqrt(0.5f), std::sqrt(0.5f)};
    float axis[3][3]{};
    Q_UnitQuatToAxis(quarterTurn, axis);
    const float expected[3][3]{{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            assert(std::fabs(axis[row][column] - expected[row][column]) < 0.000001f);

#if defined(__EMSCRIPTEN__)
    static_assert(sizeof(long double) == 16);
#elif defined(_WIN32)
    static_assert(sizeof(long double) == 8);
#endif

    assert(GScr_ParseFloatValue("12.5") == 12.5f);
    assert(GScr_ParseFloatValue("-0.125") == -0.125f);
    assert(GScr_ParseFloatValue("17.25 trailing") == 17.25f);

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

    constexpr std::array<std::pair<float, float>, 7> floorCases{{
        {-2.75f, -3.0f},
        {-1.0f, -1.0f},
        {-0.25f, -1.0f},
        {0.0f, 0.0f},
        {0.25f, 0.0f},
        {1.0f, 1.0f},
        {2.75f, 2.0f},
    }};
    for (const auto &[input, expected] : floorCases)
    {
        assert(GScr_FloorValue(input) == expected);
    }

    constexpr std::array<std::pair<float, float>, 7> ceilCases{{
        {-2.75f, -2.0f},
        {-1.0f, -1.0f},
        {-0.25f, 0.0f},
        {0.0f, 0.0f},
        {0.25f, 1.0f},
        {1.0f, 1.0f},
        {2.75f, 3.0f},
    }};
    for (const auto &[input, expected] : ceilCases)
    {
        assert(GScr_CeilValue(input) == expected);
    }

    return 0;
}
