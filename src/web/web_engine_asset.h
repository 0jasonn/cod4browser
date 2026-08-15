#pragma once

// Loads and consumes one bounded engine asset through the asynchronous browser
// filesystem service. All state transitions are advanced from the frame pump.
void WebEngineAsset_Start();
// Selects the exact IWI resolved by a validated retail Material/GfxImage
// dependency. The binding is copied and survives archive mounting; it never
// retains parser pointers or retail bytes.
bool WebEngineAsset_SetMaterialImageBinding(
    const char *materialName,
    const char *imageName,
    const char *imagePath,
    unsigned int materialIdentity,
    unsigned int imageIdentity);
void WebEngineAsset_ClearMaterialImageBinding();
// Returns false without discarding request ownership if the lower filesystem
// cannot synchronously acknowledge cancellation.
bool WebEngineAsset_Cancel();
void WebEngineAsset_Frame();
