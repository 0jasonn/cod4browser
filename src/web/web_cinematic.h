#pragma once

struct GfxImage;
struct GfxCmdBufInput;

// Canonical renderer code-image slots; the platform owns their texture data.
extern GfxCmdBufInput gfxCmdBufInput;
const GfxImage *WebCinematic_PlaneImage(unsigned plane);
bool WebCinematic_FullRange();
void WebCinematic_ReleaseImages();
