#include <gfx_d3d/r_savegame_image_win32.h>
#include <cassert>
#include <cstdio>
#include <vector>

int main()
{
    // A hidden window supplies the native D3D device; no game assets or UI.
    HWND window = CreateWindowExA(0, "STATIC", "Kisak JPEG test", WS_POPUP,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(window);
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    assert(d3d);
    D3DPRESENT_PARAMETERS present{};
    present.Windowed = TRUE;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.BackBufferWidth = present.BackBufferHeight = 64;
    present.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9 *device = nullptr;
    assert(SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present, &device)));
    IDirect3DTexture9 *source = nullptr;
    assert(SUCCEEDED(device->CreateTexture(512, 512, 1, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, &source, nullptr)));
    D3DLOCKED_RECT pixels{};
    assert(SUCCEEDED(source->LockRect(0, &pixels, nullptr, 0)));
    for (int y = 0; y < 512; ++y)
    {
        auto *row = reinterpret_cast<DWORD *>(static_cast<char *>(pixels.pBits) + y * pixels.Pitch);
        for (int x = 0; x < 512; ++x) row[x] = y < 256 ? 0xffff0000 : 0xff00ff00;
    }
    source->UnlockRect(0);
    ID3DXBuffer *encoded = nullptr;
    assert(SUCCEEDED(D3DXSaveTextureToFileInMemory(&encoded, D3DXIFF_JPG, source, nullptr)));
    std::vector<std::uint8_t> jpeg(static_cast<std::uint8_t *>(encoded->GetBufferPointer()),
        static_cast<std::uint8_t *>(encoded->GetBufferPointer()) + encoded->GetBufferSize());
    encoded->Release();
    source->Release();
    assert(R_IsSaveGameJpeg(jpeg));
    assert(!R_DecodeSaveGameTexture(nullptr, jpeg));
    for (int pass = 0; pass < 8; ++pass)
    {
        IDirect3DTexture9 *decoded = R_DecodeSaveGameTexture(device, jpeg);
        assert(decoded);
        assert(SUCCEEDED(decoded->LockRect(0, &pixels, nullptr, D3DLOCK_READONLY)));
        for (int y : {128, 384})
        {
            const auto pixel = reinterpret_cast<const DWORD *>(
                static_cast<const char *>(pixels.pBits) + y * pixels.Pitch)[128];
            const unsigned red = (pixel >> 16) & 255, green = (pixel >> 8) & 255;
            assert(y < 256 ? red > 250 && green < 3 : green > 250 && red < 3);
            assert((pixel >> 24) == 255);
        }
        decoded->UnlockRect(0);
        assert(decoded->Release() == 0);
        // Exercise repeated managed-resource creation across device reset.
        assert(SUCCEEDED(device->Reset(&present)));
    }
    for (std::size_t size : {0u, 2u, 20u})
        assert(!R_DecodeSaveGameTexture(device, std::span(jpeg).first(size)));
    // Keep a valid frame header but remove the entropy stream.
    auto scan = jpeg;
    for (std::size_t at = 2; at + 1 < scan.size(); ++at)
        if (scan[at] == 0xff && scan[at + 1] == 0xda) { scan.resize(at + 4); break; }
    assert(!R_DecodeSaveGameTexture(device, scan));
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    std::puts("native JPEG: 8 color/orientation/reset cycles and invalid inputs passed");
}
