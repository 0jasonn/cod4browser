#pragma once
#include "r_savegame_image.h"
#include <d3dx9tex.h>

// D3DX is already a native renderer dependency. Keep its decoder and GPU
// resource format behind this Windows renderer boundary.
inline IDirect3DTexture9 *R_DecodeSaveGameTexture(IDirect3DDevice9 *device,
    std::span<const std::uint8_t> jpeg)
{
    if (!device || !R_IsSaveGameJpeg(jpeg)) return nullptr;
    IDirect3DTexture9 *texture = nullptr;
    const HRESULT result = D3DXCreateTextureFromFileInMemoryEx(device, jpeg.data(),
        static_cast<UINT>(jpeg.size()), SAVEGAME_IMAGE_SIZE, SAVEGAME_IMAGE_SIZE,
        1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE,
        D3DX_FILTER_NONE, 0, nullptr, nullptr, &texture);
    D3DSURFACE_DESC desc{};
    if (FAILED(result) || !texture || FAILED(texture->GetLevelDesc(0, &desc)) ||
        desc.Width != SAVEGAME_IMAGE_SIZE || desc.Height != SAVEGAME_IMAGE_SIZE ||
        desc.Format != D3DFMT_A8R8G8B8 || texture->GetLevelCount() != 1)
    {
        if (texture) texture->Release();
        return nullptr;
    }
    return texture;
}
