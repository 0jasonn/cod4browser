#pragma once

// g_gravity is registered with a positive minimum. Keep the original
// nearest-integer assignment without depending on the host long-double ABI.
constexpr int G_RoundPlayerGravity(float gravity) noexcept
{
    return static_cast<int>(gravity + 0.5f);
}
