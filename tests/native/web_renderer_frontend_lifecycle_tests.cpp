#include <web/web_renderer_frontend_lifecycle.h>

#include <cstdlib>

namespace
{
void Require(bool condition)
{
    if (!condition) std::abort();
}
}

int main()
{
    WebRendererFrontendLifecycle lifecycle;
    int worldA = 1;
    int worldB = 2;

    Require(!lifecycle.PublishWorld(&worldA));
    Require(lifecycle.Initialize(true));
    Require(!lifecycle.Initialize(true));
    Require(lifecycle.PublishWorld(&worldA));
    Require(lifecycle.World() == &worldA && lifecycle.WorldReady());
    Require(!lifecycle.PublishWorld(&worldB));
    Require(lifecycle.BeginWorldUnload());
    Require(!lifecycle.CompleteWorldUnload(true, false));
    Require(lifecycle.CompleteWorldUnload(true, true));
    Require(lifecycle.World() == nullptr && !lifecycle.WorldReady());

    Require(lifecycle.PublishWorld(&worldB));
    Require(lifecycle.World() == &worldB);
    Require(lifecycle.BeginWorldUnload());
    Require(lifecycle.CompleteWorldUnload(true, true));
    Require(lifecycle.PublicationGeneration() == 2u);
    Require(lifecycle.UnloadGeneration() == 2u);
    Require(lifecycle.FullShutdown(true));
    Require(lifecycle.State() == WebRendererFrontendState::Shutdown);
    Require(!lifecycle.PublishWorld(&worldA));
    return 0;
}
