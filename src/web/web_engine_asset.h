#pragma once

// Loads and consumes one bounded engine asset through the asynchronous browser
// filesystem service. All state transitions are advanced from the frame pump.
void WebEngineAsset_Start();
struct WebEngineMaterialImageBindingDesc
{
    const char *materialName;
    const char *imageName;
    const char *imagePath;
    unsigned int materialIdentity;
    unsigned int imageIdentity;
    unsigned int samplerState;
};
// Selects the exact IWI resolved by a validated retail Material/GfxImage
// dependency. The binding is copied and survives archive mounting; it never
// retains parser pointers or retail bytes.
bool WebEngineAsset_SetMaterialImageBinding(
    const char *materialName,
    const char *imageName,
    const char *imagePath,
    unsigned int materialIdentity,
    unsigned int imageIdentity,
    unsigned int samplerState = 0u);
bool WebEngineAsset_SetMaterialImageBindings(
    const WebEngineMaterialImageBindingDesc *bindings,
    unsigned int bindingCount);
// Prevents the legacy first-IWI probe after a rendered material was checked but
// did not yield a supported external color map. The current texture is kept.
void WebEngineAsset_RequireMaterialImageBinding();
void WebEngineAsset_ClearMaterialImageBinding();
// Callback-free view used only to choose the one IWD that contains the checked
// material image. The archive job copies it before doing asynchronous work.
const char *WebEngineAsset_MaterialImagePath() noexcept;
unsigned int WebEngineAsset_MaterialImageSlot() noexcept;
bool WebEngineAsset_CurrentBindingFinished() noexcept;
bool WebEngineAsset_AdvanceMaterialImageBinding();
// Returns false without discarding request ownership if the lower filesystem
// cannot synchronously acknowledge cancellation.
bool WebEngineAsset_Cancel();
void WebEngineAsset_Frame();
