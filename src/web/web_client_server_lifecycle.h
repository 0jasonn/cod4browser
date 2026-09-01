#pragma once

#include <cstddef>
#include <cstdint>

bool Web_TakePendingCanonicalCommand(char *command, std::size_t capacity);
std::uint32_t Web_RendererEntityCount() noexcept;
