#pragma once

#include <cstdint>

enum class WebRendererFrontendState : std::uint8_t
{
    Cold,
    Ready,
    WorldPublished,
    WorldUnloading,
    Shutdown,
};

class WebRendererFrontendLifecycle
{
public:
    bool Initialize(bool contextLive) noexcept
    {
        if (state_ != WebRendererFrontendState::Cold || !contextLive) return false;
        state_ = WebRendererFrontendState::Ready;
        return true;
    }

    bool PublishWorld(const void *world) noexcept
    {
        if (state_ != WebRendererFrontendState::Ready || !world) return false;
        world_ = world;
        state_ = WebRendererFrontendState::WorldPublished;
        ++publicationGeneration_;
        return true;
    }

    bool BeginWorldUnload() noexcept
    {
        if (state_ != WebRendererFrontendState::WorldPublished || !world_) return false;
        state_ = WebRendererFrontendState::WorldUnloading;
        return true;
    }

    bool CompleteWorldUnload(bool backendReleased, bool contextLive) noexcept
    {
        if (state_ != WebRendererFrontendState::WorldUnloading ||
            !backendReleased || !contextLive) return false;
        world_ = nullptr;
        state_ = WebRendererFrontendState::Ready;
        ++unloadGeneration_;
        return true;
    }

    bool FullShutdown(bool contextReleased) noexcept
    {
        if ((state_ != WebRendererFrontendState::Ready &&
             state_ != WebRendererFrontendState::Cold) || !contextReleased) return false;
        world_ = nullptr;
        state_ = WebRendererFrontendState::Shutdown;
        return true;
    }

    WebRendererFrontendState State() const noexcept { return state_; }
    const void *World() const noexcept { return world_; }
    bool WorldReady() const noexcept
    {
        return state_ == WebRendererFrontendState::WorldPublished;
    }
    std::uint32_t PublicationGeneration() const noexcept
    {
        return publicationGeneration_;
    }
    std::uint32_t UnloadGeneration() const noexcept { return unloadGeneration_; }

private:
    WebRendererFrontendState state_ = WebRendererFrontendState::Cold;
    const void *world_ = nullptr;
    std::uint32_t publicationGeneration_ = 0u;
    std::uint32_t unloadGeneration_ = 0u;
};
