// Copyright (C) 2015 Jonas Kümmerlin <rgcjonas@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "dd4seven-api.hpp"
#include "com.hpp"
#include "util.hpp"
#include "logger.hpp"

#include <atomic>
#include <iostream>
#include <d3d10.h>
#include <d3d11.h>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <memory>

static std::size_t calculate_bitmap_size_mono(HBITMAP bmp)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc)
        return -1;

    struct {
        BITMAPINFOHEADER h;
        RGBQUAD          colors[2];
    } bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.h.biSize = sizeof(bmi.h);

    if (!GetDIBits(hdc, bmp, 0, 0, nullptr, (BITMAPINFO*)&bmi, DIB_RGB_COLORS))
        return 0;

    auto w = bmi.h.biWidth;
    auto h = std::abs(bmi.h.biHeight);

    auto bpl = ((w-1)/32 + 1)*4; // bytes per line
    auto size = bpl * h;

    DeleteDC(hdc);

    return std::size_t(size);
}

// Saves a top-down monochrome dib to *out
static bool export_bitmap_to_mono(HBITMAP bmp, uint8_t *pixels, LONG *pWidth = nullptr, LONG *pHeight = nullptr, LONG *pStride = nullptr)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc)
        return -1;

    struct {
        BITMAPINFOHEADER h;
        RGBQUAD          colors[2];
    } bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.h.biSize = sizeof(bmi.h);

    if (!GetDIBits(hdc, bmp, 0, 0, nullptr, (BITMAPINFO*)&bmi, DIB_RGB_COLORS))
        return false;

    bmi.h.biBitCount = 1;
    bmi.h.biHeight   = -1 * std::abs(bmi.h.biHeight);

    if (!GetDIBits(hdc, bmp, 0, UINT(std::abs(bmi.h.biHeight)), pixels, (BITMAPINFO*)&bmi, DIB_RGB_COLORS))
        return false;

    if (pWidth)  *pWidth  = bmi.h.biWidth;
    if (pHeight) *pHeight = std::abs(bmi.h.biHeight);
    if (pStride) *pStride = ((bmi.h.biWidth-1)/32 + 1)*4;

    DeleteDC(hdc);

    return true;
}

static std::size_t calculate_bitmap_size_rgb32(HBITMAP bmp)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc)
        return -1;

    BITMAPINFOHEADER bih;
    ZeroMemory(&bih, sizeof(bih));
    bih.biSize = sizeof(bih);

    if (!GetDIBits(hdc, bmp, 0, 0, nullptr, (BITMAPINFO*)&bih, DIB_RGB_COLORS))
        return 0;

    auto w = bih.biWidth;
    auto h = std::abs(bih.biHeight);

    auto bpl = w * 4; // bytes per line
    auto size = bpl * h;

    DeleteDC(hdc);

    return std::size_t(size);
}

// Saves a top-down rgb32 dib to *out
static bool export_bitmap_to_rgb32(HBITMAP bmp, uint8_t *pixels, LONG *pWidth = nullptr, LONG *pHeight = nullptr, LONG *pStride = nullptr)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc)
        return -1;

    BITMAPINFOHEADER bih;
    ZeroMemory(&bih, sizeof(bih));
    bih.biSize = sizeof(bih);

    if (!GetDIBits(hdc, bmp, 0, 0, nullptr, (BITMAPINFO*)&bih, DIB_RGB_COLORS))
        return false;

    bih.biBitCount = 32;
    bih.biHeight   = -1 * std::abs(bih.biHeight);

    logger << "Exporting to: " << pixels << std::endl;

    if (!GetDIBits(hdc, bmp, 0, UINT(std::abs(bih.biHeight)), pixels, (BITMAPINFO*)&bih, DIB_RGB_COLORS))
        return false;

    if (pWidth)  *pWidth  = bih.biWidth;
    if (pHeight) *pHeight = std::abs(bih.biHeight);
    if (pStride) *pStride = 4*bih.biWidth;

    DeleteDC(hdc);

    return true;
}

//KEEP THIS IN SYNC WITH dd4seven-dwm.cpp
#pragma pack(push,1)
struct CaptureRequest
{
    RECT     monitor;
    wchar_t  imageMutex[56];
    wchar_t  imageEvent[56];
    wchar_t  keepAliveMutex[56];
    uint32_t captureTarget; //D3D pseudo-handle
};
#pragma pack(pop)

class DD4SevenOutputDuplication : public IDXGIOutputDuplication, public com::obj_impl_base
{
protected:
    void *_queryInterface(REFIID iid)
    {
        return com::query_impl<IDXGIOutputDuplication, IDXGIObject>::on(this, iid);
    }
public:
    /*** IDXGIObject methods ***/
    HRESULT STDMETHODCALLTYPE SetPrivateData(
        REFGUID guid,
        UINT data_size,
        const void *data) override
    {
        (void)guid;
        (void)data_size;
        (void)data;

        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
        REFGUID guid,
        const IUnknown *object) override
    {
        (void)guid;
        (void)object;

        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetPrivateData(
        REFGUID guid,
        UINT *data_size,
        void *data) override
    {
        (void)guid;
        (void)data_size;
        (void)data;

        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetParent(
        REFIID riid,
        void **parent) override
    {
        (void)riid;
        (void)parent;

        return E_NOTIMPL;
    }

    /*** IDXGIOutputDuplication methods ***/
    void STDMETHODCALLTYPE GetDesc(
        DXGI_OUTDUPL_DESC *pDesc) override
    {
        if (!pDesc)
            return;

        //TODO: Ask the DWM for the real values
        pDesc->ModeDesc.Width = UINT(m_monitor.right - m_monitor.left);
        pDesc->ModeDesc.Height = UINT(m_monitor.bottom - m_monitor.top);
        pDesc->ModeDesc.RefreshRate.Numerator = 60; /*FIXME: assume 60.0Hz */
        pDesc->ModeDesc.RefreshRate.Denominator = 1;
        pDesc->ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        pDesc->ModeDesc.ScanlineOrdering = /*FIXME*/DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        pDesc->ModeDesc.Scaling = /*FIXME*/DXGI_MODE_SCALING_UNSPECIFIED;
        pDesc->Rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
        pDesc->DesktopImageInSystemMemory = FALSE;
    }

    HRESULT STDMETHODCALLTYPE AcquireNextFrame(
        UINT TimeoutInMilliseconds,
        DXGI_OUTDUPL_FRAME_INFO *pFrameInfo,
        IDXGIResource **ppDesktopResource) override
    {
        if (!m_isGood)
            return DXGI_ERROR_ACCESS_LOST;

        if (m_desktopImageAcquired)
            return DXGI_ERROR_INVALID_CALL;

        if (!pFrameInfo || !ppDesktopResource)
            return E_INVALIDARG;

        // Wait for a new image from the DWM
        //FIXME: Also wait for mouse movements
        HANDLE objects[2] = { m_imageEvent, m_imageMutex };
        switch (WaitForMultipleObjects(2, objects, TRUE, TimeoutInMilliseconds)) {
            case WAIT_TIMEOUT:
                m_timeoutMsecs += TimeoutInMilliseconds;
                if (m_timeoutMsecs > 5000)
                    return DXGI_ERROR_ACCESS_LOST;

                return DXGI_ERROR_WAIT_TIMEOUT;
            case WAIT_OBJECT_0:
                m_timeoutMsecs = 0;

                // The DWM prepared an image for us
                QueryPerformanceCounter(&pFrameInfo->LastPresentTime);
                pFrameInfo->AccumulatedFrames = /*FIXME*/ 1;
                pFrameInfo->RectsCoalesced = FALSE;
                pFrameInfo->ProtectedContentMaskedOut = FALSE;

                m_desktopImageAcquired = true;
                *ppDesktopResource = m_desktopImage.get();
                (*ppDesktopResource)->AddRef();

                // The mouse might have been changed
                CURSORINFO info;
                info.cbSize = sizeof(CURSORINFO);
                if (GetCursorInfo(&info)) {
                    pFrameInfo->LastMouseUpdateTime = pFrameInfo->LastPresentTime;
                    pFrameInfo->PointerPosition.Visible = (info.flags == CURSOR_SHOWING);

                    // Has the cursor been changed?
                    if (info.hCursor != m_lastCursor) {
                        clearCursorInfo();
                        m_lastCursor = info.hCursor;
                        GetIconInfo(m_lastCursor, &m_cursorInfo);
                    }

                    pFrameInfo->PointerPosition.Position.x = info.ptScreenPos.x - m_cursorInfo.xHotspot - m_monitor.left;
                    pFrameInfo->PointerPosition.Position.y = info.ptScreenPos.y - m_cursorInfo.yHotspot - m_monitor.top;

                    // We are required to estimate the space needed for the bitmaps here
                    if (m_cursorInfo.hbmColor) {
                        // This is a colored cursor, which will always be represented as BGRA bitmap
                        pFrameInfo->PointerShapeBufferSize = UINT(calculate_bitmap_size_rgb32(m_cursorInfo.hbmColor));
                    } else {
                        // This is a monochrome cursor, and we export this fact to the caller
                        pFrameInfo->PointerShapeBufferSize = UINT(calculate_bitmap_size_mono(m_cursorInfo.hbmMask));
                    }
                } else {
                    pFrameInfo->LastMouseUpdateTime.QuadPart = 0;
                }

                // The total metadata size is the pointer size + space for the change/move rects
                // We only have one change rect and no move rect
                pFrameInfo->TotalMetadataBufferSize = pFrameInfo->PointerShapeBufferSize + sizeof(RECT);

                return S_OK;

            case WAIT_ABANDONED_0:
                // We should just die here
                m_isGood = false;
                return DXGI_ERROR_ACCESS_LOST;

            case WAIT_FAILED:
            default:
                logger << "WaitForMultipleObjects failed: " << util::hresult_to_utf8(HRESULT_FROM_WIN32(GetLastError())) << std::endl;
                return E_FAIL;
        }
    }

    HRESULT STDMETHODCALLTYPE GetFrameDirtyRects(
        UINT DirtyRectsBufferSize,
        RECT *pDirtyRectsBuffer,
        UINT *pDirtyRectsBufferSizeRequired) override
    {
        /* TODO: Research whether we could do better */

        if (!pDirtyRectsBufferSizeRequired)
            return E_INVALIDARG;

        *pDirtyRectsBufferSizeRequired = 1;
        if (DirtyRectsBufferSize < 1)
            return DXGI_ERROR_MORE_DATA;

        if (!pDirtyRectsBuffer)
            return E_INVALIDARG;

        pDirtyRectsBuffer->left = 0;
        pDirtyRectsBuffer->top  = 0;
        pDirtyRectsBuffer->right = m_monitor.right - m_monitor.left;
        pDirtyRectsBuffer->bottom = m_monitor.bottom - m_monitor.top;

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFrameMoveRects(
        UINT MoveRectsBufferSize,
        DXGI_OUTDUPL_MOVE_RECT *pMoveRectBuffer,
        UINT *pMoveRectsBufferSizeRequired) override
    {
        (void)MoveRectsBufferSize;
        (void)pMoveRectBuffer;

        if (!pMoveRectsBufferSizeRequired)
            return E_INVALIDARG;

        *pMoveRectsBufferSizeRequired = 0;

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFramePointerShape(
        UINT PointerShapeBufferSize,
        void *pPointerShapeBuffer,
        UINT *pPointerShapeBufferSizeRequired,
        DXGI_OUTDUPL_POINTER_SHAPE_INFO *pPointerShapeInfo) override
    {
        if (!pPointerShapeBuffer || !pPointerShapeBufferSizeRequired || !pPointerShapeInfo)
            return E_INVALIDARG;

        pPointerShapeInfo->HotSpot.x = m_cursorInfo.xHotspot;
        pPointerShapeInfo->HotSpot.y = m_cursorInfo.yHotspot;

        if (m_cursorInfo.hbmColor) {
            // The cursor is a color cursor
            // We will create a argb32 bitmap
            *pPointerShapeBufferSizeRequired = UINT(calculate_bitmap_size_rgb32(m_cursorInfo.hbmColor));
            if (PointerShapeBufferSize < *pPointerShapeBufferSizeRequired)
                return DXGI_ERROR_MORE_DATA;

            // If we're lucky, the color bitmap contains alpha data
            bool foundAlpha = false;
            LONG width  = 0;
            LONG height = 0;
            LONG stride = 0;
            logger << "pointer shape buffer: " << pPointerShapeBuffer << std::endl;
            if (!export_bitmap_to_rgb32(m_cursorInfo.hbmColor, (uint8_t *)pPointerShapeBuffer, &width, &height, &stride))
                return E_FAIL;

            // try to find an alpha value
            uint8_t *bitmap = (uint8_t*)pPointerShapeBuffer;
            for (LONG y = 0; y < height; ++y) {
                for (LONG x = 0; x < width; ++x) {
                    uint8_t *pixel = &bitmap[y*stride + x*4];

                    if (pixel[3]) {
                        foundAlpha = true;
                        break;
                    }
                }
            }

            if (!foundAlpha) {
                // If there is no alpha, we have to consider the mask
                std::unique_ptr<uint8_t[]> and_mask(new (std::nothrow) uint8_t[calculate_bitmap_size_mono(m_cursorInfo.hbmMask)]);
                if (!and_mask)
                    return E_OUTOFMEMORY;

                LONG andWidth  = 0;
                LONG andHeight = 0;
                LONG andStride = 0;
                if (!export_bitmap_to_mono(m_cursorInfo.hbmMask, and_mask.get(), &andWidth, &andHeight, &andStride))
                    return E_FAIL;

                for (LONG y = 0; y < andHeight; ++y) {
                    uint8_t *source_row = &and_mask[y*andStride];

                    for (LONG x = 0; x < andWidth; ++x) {
                        uint8_t *target_pixel = &bitmap[y*stride + x*4];
                        uint8_t alpha = util::get_pixel_from_row<1>(source_row, x) ? 0 : 0xFF;

                        target_pixel[3] = alpha;
                    }
                }
            }

            pPointerShapeInfo->Height = UINT(height);
            pPointerShapeInfo->Width  = UINT(width);
            pPointerShapeInfo->Pitch  = UINT(stride);
            pPointerShapeInfo->Type   = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR;

            // The bitmap is complete!
            return S_OK;
        } else {
            // The cursor is a monochrome one
            // This case is actually easier because we can just pass the DIB
            // on to the caller which then has to decode it.
            LONG width  = 0;
            LONG height = 0;
            LONG stride = 0;

            *pPointerShapeBufferSizeRequired = UINT(calculate_bitmap_size_mono(m_cursorInfo.hbmMask));
            if (*pPointerShapeBufferSizeRequired > PointerShapeBufferSize)
                return DXGI_ERROR_MORE_DATA;

            if (!export_bitmap_to_mono(m_cursorInfo.hbmMask, reinterpret_cast<uint8_t*>(pPointerShapeBuffer), &width, &height, &stride))
                return E_FAIL;

            pPointerShapeInfo->Height = UINT(height);
            pPointerShapeInfo->Width  = UINT(width);
            pPointerShapeInfo->Pitch  = UINT(stride);
            pPointerShapeInfo->Type   = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;

            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE MapDesktopSurface(
        DXGI_MAPPED_RECT *pLockedRect) override
    {
        (void)pLockedRect;

        return DXGI_ERROR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE UnMapDesktopSurface() override
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    HRESULT STDMETHODCALLTYPE ReleaseFrame() override
    {
        if (!m_desktopImageAcquired)
            return DXGI_ERROR_INVALID_CALL;

        m_desktopImageAcquired = false;
        ReleaseMutex(m_imageMutex);

        return S_OK;
    }

    /*** Our own methods ***/
    bool good() { return m_isGood; }

    DD4SevenOutputDuplication(IUnknown *device, IDXGIOutput *output)
    {
        HRESULT hr;

        // Read the output coordinates
        DXGI_OUTPUT_DESC desc;
        hr = output->GetDesc(&desc);
        if FAILED(hr) {
            logger << "Failed: IDXGIOutput::GetDesc: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        if (!desc.AttachedToDesktop) {
            logger << "Output must be attached to the desktop!" << std::endl;
            return;
        }
        m_monitor = desc.DesktopCoordinates;

        // Create the desktop texture
        com::ptr<ID3D10Device> device10;
        com::ptr<ID3D11Device> device11;
        if SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(com::out_arg(device10)))) {
            // we have a d3d10 device, create the texture
            com::ptr<ID3D10Texture2D> texture;

            D3D10_TEXTURE2D_DESC texdsc = {
                .Width = UINT(m_monitor.right - m_monitor.left),
                .Height = UINT(m_monitor.bottom - m_monitor.top),
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
                .SampleDesc = {
                    .Count = 1,
                    .Quality = 0
                },
                .Usage = D3D10_USAGE_DEFAULT,
                .BindFlags = D3D10_BIND_RENDER_TARGET|D3D10_BIND_SHADER_RESOURCE,
                .CPUAccessFlags = 0,
                .MiscFlags = D3D10_RESOURCE_MISC_SHARED
            };

            hr = device10->CreateTexture2D(&texdsc, nullptr, com::out_arg(texture));

            if FAILED(hr) {
                logger << "Failed: CreateTexture2D: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            // Pass texture handle to the injected side
            auto res = texture.query<IDXGIResource>();
            if (!res) {
                logger << "Failed: QueryInterface<IDXGIResource>: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            hr = res->GetSharedHandle(&m_sharedHandle);
            if FAILED(hr) {
                logger << "Failed: GetSharedHandle: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            m_desktopImage = texture.query<IDXGIResource>(); // should always succeed
        } else if SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(com::out_arg(device11)))) {
            // we have a d3d11 device, create the texture
            com::ptr<ID3D11Texture2D> texture;

            D3D11_TEXTURE2D_DESC texdsc = {
                .Width = UINT(m_monitor.right - m_monitor.left),
                .Height = UINT(m_monitor.bottom - m_monitor.top),
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
                .SampleDesc = {
                    .Count = 1,
                    .Quality = 0
                },
                .Usage = D3D11_USAGE_DEFAULT,
                .BindFlags = D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,
                .CPUAccessFlags = 0,
                .MiscFlags = D3D11_RESOURCE_MISC_SHARED
            };

            hr = device11->CreateTexture2D(&texdsc, nullptr, com::out_arg(texture));

            if FAILED(hr) {
                logger << "Failed: CreateTexture2D: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            // Pass texture handle to the injected side
            auto res = texture.query<IDXGIResource>();
            if (!res) {
                logger << "Failed: QueryInterface<IDXGIResource>: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            hr = res->GetSharedHandle(&m_sharedHandle);
            if FAILED(hr) {
                logger << "Failed: GetSharedHandle: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            m_desktopImage = texture.query<IDXGIResource>(); // should always succeed
        } else {
            logger << "WARNING: Invalid device passed :(" << std::endl;
            return;
        }

        // Set up synchronization primitives
        GUID guid;
        if FAILED(CoCreateGuid(&guid))
            return;

        _snwprintf(m_imageEventName, 56, L"dd4seven-event-%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
                   guid.Data1, guid.Data2, guid.Data3,
                   guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                   guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        _snwprintf(m_imageMutexName, 56, L"dd4seven-mutex-%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
                   guid.Data1, guid.Data2, guid.Data3,
                   guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                   guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        _snwprintf(m_keepAliveMutexName, 56, L"dd4seven-kamtx-%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
                   guid.Data1, guid.Data2, guid.Data3,
                   guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                   guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

        m_imageEvent = CreateEvent(nullptr, FALSE, FALSE, m_imageEventName);
        m_imageMutex = CreateMutex(nullptr, FALSE, m_imageMutexName);
        m_keepAliveMutex = CreateMutex(nullptr, FALSE, m_keepAliveMutexName);

        if (!m_imageEvent || !m_imageMutex || !m_keepAliveMutex)
            return;

        WaitForSingleObject(m_keepAliveMutex, INFINITE);

        // Send texture and synchronization to the DWM
        const wchar_t *dwmWinId = L"dd4seven-window-4B3A8226-9F55-4E9E-A276-9DE174B36166";
        HWND dwm = FindWindowEx(HWND_MESSAGE, NULL, dwmWinId, dwmWinId);
        if (!dwm) { // no DWM!? sacrilege!
            logger << "DWM Not present :(" << std::endl;
            return;
        }

        CaptureRequest req;
        req.monitor = m_monitor;
        std::wcsncpy(req.imageEvent, m_imageEventName, 56);
        std::wcsncpy(req.imageMutex, m_imageMutexName, 56);
        std::wcsncpy(req.keepAliveMutex, m_keepAliveMutexName, 56);
        req.captureTarget = (uint32_t)PtrToUlong(m_sharedHandle);

        COPYDATASTRUCT copy = {
            .dwData = 0,
            .cbData = sizeof(CaptureRequest),
            .lpData = &req
        };

        if (!SendMessageTimeout(dwm, WM_COPYDATA, 0, (LPARAM)&copy, SMTO_BLOCK, 1000, nullptr)) {
            logger << "FAILED: SendMessageTimeout: " << util::hresult_to_utf8(HRESULT_FROM_WIN32(GetLastError())) << std::endl;
            return;
        }

        m_isGood = true;
    }

    ~DD4SevenOutputDuplication()
    {
        clearCursorInfo();

        if (m_keepAliveMutex) ReleaseMutex(m_keepAliveMutex);

        if (m_imageEvent)     CloseHandle(m_imageEvent);
        if (m_imageMutex)     CloseHandle(m_imageMutex);
        if (m_keepAliveMutex) CloseHandle(m_keepAliveMutex);
    }

private:

    void clearCursorInfo()
    {
        DeleteObject(m_cursorInfo.hbmColor);
        DeleteObject(m_cursorInfo.hbmMask);
        std::memset(&m_cursorInfo, 0, sizeof(ICONINFO));
    }

    bool    m_isGood { false };
    RECT    m_monitor { 0, 0, 0, 0 };

    // Mouse cursor
    HCURSOR  m_lastCursor { nullptr };
    ICONINFO m_cursorInfo { 0, 0, 0, 0, 0 };

    // Desktop Image
    com::ptr<IDXGIResource> m_desktopImage;
    HANDLE                  m_sharedHandle { nullptr };
    bool    m_desktopImageAcquired = false;

    // Synchronization
    HANDLE  m_imageEvent { nullptr };
    HANDLE  m_imageMutex { nullptr };
    HANDLE  m_keepAliveMutex { nullptr };
    wchar_t m_imageEventName[56]; // "dd4seven-event-" + 36char GUID
    wchar_t m_imageMutexName[56]; // "dd4seven-mutex-" + 36char GUID
    wchar_t m_keepAliveMutexName[56]; // "dd4seven-kamtx-" + 36char GUID

    unsigned long m_timeoutMsecs { 0 };
};

HRESULT
__stdcall
DuplicateOutput(IDXGIOutput *output, IUnknown *device, IDXGIOutputDuplication **duplication)
{
    if (!output || !device || !duplication)
        return E_INVALIDARG;

    auto dupl = com::make_object<DD4SevenOutputDuplication>(device, output);
    if (dupl->good()) {
        *duplication = dupl.release();

        return S_OK;
    } else {
        *duplication = nullptr;
        
        logger << "Failed to create duplication interface :(" << std::endl;

        //FIXME: or should we return DXGI_ERROR_UNSUPPORTED ???
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
}

HINSTANCE g_instance = nullptr;

BOOLEAN WINAPI DllMain(HINSTANCE hDllHandle,
                       DWORD     nReason,
                       LPVOID    Reserved)
{
    g_instance = hDllHandle;
    (void)Reserved;

    switch (nReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hDllHandle);
            break;
        }
        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}
