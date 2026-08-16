#pragma once

// Loads and consumes one bounded engine asset through the asynchronous browser
// filesystem service. All state transitions are advanced from the frame pump.
void WebEngineAsset_Start();
// Returns false without discarding request ownership if the lower filesystem
// cannot synchronously acknowledge cancellation.
bool WebEngineAsset_Cancel();
void WebEngineAsset_Frame();
