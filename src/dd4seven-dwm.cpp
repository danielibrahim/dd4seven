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

#define INITGUID
#define CINTERFACE
#define COBJMACROS

#include "com.hpp"
#include "util.hpp"
#include "logger.hpp"

#include <d3d10_1.h>
#include <dxgi.h>
#include <dxgi_dwm.h>
#include <MinHook.h>

#include <list>
#include <algorithm>

std::ostream& operator<<(std::ostream& os, const RECT& r)
{
    os << "RECT[(" << r.left << ", " << r.top << "),(" << r.right << "," << r.bottom << ")]";

    return os;
}

bool operator==(const RECT &r1, const RECT &r2)
{
    return r1.left   == r2.left
        && r1.top    == r2.top
        && r1.right  == r2.right
        && r1.bottom == r2.bottom;
}
bool operator!=(const RECT &r1, const RECT &r2)
{
    return r1.left   != r2.left
        || r1.top    != r2.top
        || r1.right  != r2.right
        || r1.bottom != r2.bottom;
}


HINSTANCE g_instance;

/*********************************
 * ACTUAL FUNCTIONALITY
 *********************************/

//KEEP THIS IN SYNC WITH dd4seven-api.cpp
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

struct Capture
{
    IDXGISwapChainDWM *capturedChain { nullptr };
    com::ptr<ID3D10Texture2D> captureTarget;
    HANDLE imageMutex { nullptr };
    HANDLE imageEvent { nullptr };
    HANDLE keepAliveMutex { nullptr };
    HANDLE captureTargetHandle { nullptr }; //D3D pseudo-handle
    RECT   monitor { 0, 0, 0, 0 };

    Capture() = default;
    Capture(const Capture &other) = delete;
    Capture(Capture &&other)
    {
        std::swap(capturedChain, other.capturedChain);
        std::swap(captureTarget, other.captureTarget);
        std::swap(imageMutex, other.imageMutex);
        std::swap(imageEvent, other.imageEvent);
        std::swap(keepAliveMutex, other.keepAliveMutex);
        std::swap(captureTargetHandle, other.captureTargetHandle);
        std::swap(monitor, other.monitor);
    }

    ~Capture()
    {
        if (imageMutex)
            CloseHandle(imageMutex);
        if (imageEvent)
            CloseHandle(imageEvent);
        if (keepAliveMutex)
            CloseHandle(keepAliveMutex);
    }

    Capture& operator=(const Capture &other) = delete;
    Capture& operator=(Capture &&other)
    {
        std::swap(*this, other);

        return *this;
    }
};

std::list<Capture> g_capturing;

LRESULT __stdcall CommunicationWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COPYDATA) {
        COPYDATASTRUCT *copy = (COPYDATASTRUCT*)lp;

        if (copy->cbData != sizeof(CaptureRequest)) {
            logger << "Illegal data: expected size " << sizeof(CaptureRequest) << " got " << copy->cbData << std::endl;
            return FALSE;
        }

        // Copy it, to make sure the alignment is correct
        CaptureRequest req;
        Capture cap;
        std::memcpy(&req, copy->lpData, sizeof(CaptureRequest));

        // Sanitize sero-terminated strings
        req.imageEvent[55] = 0;
        req.imageMutex[55] = 0;
        req.keepAliveMutex[55] = 0;

        // Open the synchronization primitives
        cap.imageMutex = CreateMutex(nullptr, FALSE, req.imageMutex);
        if (!cap.imageMutex) {
            logger << "Couldn't create image mutex " << util::wcsdup_to_utf8(req.imageMutex) << std::endl;
            return FALSE;
        }

        cap.keepAliveMutex = CreateMutex(nullptr, FALSE, req.keepAliveMutex);
        if (!cap.keepAliveMutex) {
            logger << "Couldn't create keep-alive mutex " << util::wcsdup_to_utf8(req.keepAliveMutex) << std::endl;
            return FALSE;
        }

        cap.imageEvent = CreateEvent(nullptr, FALSE, FALSE, req.imageEvent);
        if (!cap.imageEvent) {
            logger << "Couldn't create image event " << util::wcsdup_to_utf8(req.imageEvent) << std::endl;
            return FALSE;
        }

        // Copy the monitor and texture handle
        cap.monitor = req.monitor;
        cap.captureTargetHandle = (HANDLE)ULongToPtr(req.captureTarget);

        logger << "Registering capture on " << cap.monitor << std::endl;

        // Save the new capture
        g_capturing.push_back(std::move(cap));

        return TRUE;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

HWND InitializeWindow()
{
    // register a window class for our window
    WNDCLASSEX wcex;
    std::memset(&wcex, 0, sizeof(wcex));
    wcex.cbSize        = sizeof(wcex);
    wcex.lpfnWndProc   = CommunicationWindowProc;
    wcex.hInstance     = g_instance;
    wcex.lpszClassName = L"dd4seven-window-4B3A8226-9F55-4E9E-A276-9DE174B36166";

    if (!RegisterClassEx(&wcex)) {
        logger << "Failed: RegisterClassEx: " << GetLastError() << std::endl;
        return (HWND)0;
    }

    HWND ourwin = CreateWindow(wcex.lpszClassName,
                               L"dd4seven-window-4B3A8226-9F55-4E9E-A276-9DE174B36166",
                               0,
                               0, 0, 0, 0,
                               HWND_MESSAGE,
                               NULL,
                               g_instance,
                               nullptr);
    if (!ourwin) {
        logger << "Failed: CreateWindow: " << GetLastError() << std::endl;
        return (HWND)0;
    }

    logger << "Created communication window: " << ourwin << std::endl;

    return ourwin;
}

// Copies the back buffer into the given target
void CopyBackBuffer(IDXGISwapChainDWM *swap, ID3D10Resource *target)
{
    HRESULT hr;
    com::ptr<ID3D10Device>   device;
    com::ptr<ID3D10Resource> backBuffer;

    DXGI_SWAP_CHAIN_DESC swpdsc;

    hr = IDXGISwapChainDWM_GetDevice(swap, IID_ID3D10Device, com::out_arg_void(device));
    if FAILED(hr) {
        logger << "Failed to retrieve device from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    hr = IDXGISwapChainDWM_GetBuffer(swap, 0, IID_ID3D10Resource, com::out_arg_void(backBuffer));
    if FAILED(hr) {
        logger << "Failed to retrieve back buffer from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    hr = IDXGISwapChainDWM_GetDesc(swap, &swpdsc);
    if FAILED(hr) {
        logger << "Failed to retrieve description of swap chain: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    if (swpdsc.SampleDesc.Count > 1) {
        ID3D10Device_ResolveSubresource(device, target, 0, backBuffer, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    } else {
        ID3D10Device_CopyResource(device, target, backBuffer);
    }
}

void TrySetupCapturing(IDXGISwapChainDWM *swap, Capture &cap)
{
    HRESULT hr;
    com::ptr<IDXGIOutput>  output;
    com::ptr<ID3D10Device> device;
    DXGI_OUTPUT_DESC desc;

    hr = IDXGISwapChainDWM_GetContainingOutput(swap, com::out_arg(output));
    if FAILED(hr) {
        logger << "Failed to retrieve output from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    hr = IDXGIOutput_GetDesc(output, &desc);
    if FAILED(hr) {
        logger << "Failed to retrieve description from output: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    if (!desc.AttachedToDesktop)
        return;

    if (desc.DesktopCoordinates != cap.monitor)
        return; // Not our swap chain :(

    hr = IDXGISwapChainDWM_GetDevice(swap, IID_ID3D10Device, com::out_arg_void(device));
    if FAILED(hr) {
        logger << "Failed to retrieve device from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    hr = ID3D10Device_OpenSharedResource(device, cap.captureTargetHandle, IID_ID3D10Texture2D, com::out_arg_void(cap.captureTarget));
    if FAILED(hr) {
        logger << "Failed to open shared texture: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    // we're done! set the swap chain to mark this
    cap.capturedChain = swap;
}


void BeforePresent(IDXGISwapChainDWM *swap)
{
    // Create window on first call
    static HWND window = 0;
    if (!window)
        window = InitializeWindow();

    // Ghetto message loop
    MSG msg;
    ZeroMemory(&msg, sizeof(MSG));
    while (PeekMessage(&msg, window, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Iterate over all capture tasks, and process the one that are alive
    for (auto it = g_capturing.begin(); it != g_capturing.end();) {
        Capture &cap = *it;

        // Check keep-alive mutex, remove if necessary
        if (WaitForSingleObject(cap.keepAliveMutex, 0) != WAIT_TIMEOUT) {
            // If we get this, the remote client is dead.

            logger << "Remote client left: " << cap.monitor << std::endl;

            // remove the capture
            it = g_capturing.erase(it);
        } else {
            // the remote client still lives, on to the next one
            ++it;
        }

        if (cap.capturedChain == swap) {
            if (WaitForSingleObject(cap.imageMutex, 0) != WAIT_OBJECT_0)
                continue; // the image is locked, skip it

            // Copy image
            CopyBackBuffer(swap, (ID3D10Resource*)cap.captureTarget.get());

            // Send event
            SetEvent(cap.imageEvent);

            // Unlock image
            ReleaseMutex(cap.imageMutex);
        } else if (!cap.capturedChain) {
            TrySetupCapturing(swap, cap);
        }
    }
}

/*********************************
 * DXGI HOOKING
 *********************************/
bool g_presentHooked = false;
HRESULT (__stdcall *g_truePresent)(IDXGISwapChainDWM* swap, UINT sync_interval, UINT flags);

HRESULT __stdcall OverriddenPresent(IDXGISwapChainDWM *swap, UINT sync_interval, UINT flags)
{
    BeforePresent(swap);

    return g_truePresent(swap, sync_interval, flags);
}

bool g_createSwapChainHooked = false;
HRESULT(__stdcall *g_trueCreateSwapChain)(IDXGIFactoryDWM *factory,
                                          IUnknown *pDevice,
                                          DXGI_SWAP_CHAIN_DESC *pDesc,
                                          IDXGIOutput *pOutput,
                                          IDXGISwapChainDWM **ppSwapChainDWM);
HRESULT __stdcall OurCreateSwapChain(IDXGIFactoryDWM *factory,
                                     IUnknown *pDevice,
                                     DXGI_SWAP_CHAIN_DESC *pDesc,
                                     IDXGIOutput *pOutput,
                                     IDXGISwapChainDWM **ppSwapChainDWM)
{
    if (!ppSwapChainDWM)
        return E_INVALIDARG;

    logger << "Hook: CreateSwapChain" << std::endl;

    HRESULT hr;
    hr = g_trueCreateSwapChain(factory, pDevice, pDesc, pOutput, ppSwapChainDWM);

    if (SUCCEEDED(hr) && !g_presentHooked) {
        // now hook the present function
        MH_STATUS status;

        status = MH_CreateHook((void*)(*ppSwapChainDWM)->lpVtbl->Present, (void*)OverriddenPresent, (void**)&g_truePresent);
        if (status) {
            logger << "MH_CreateHook() returned status " << status << std::endl;
            return hr;
        }

        status = MH_EnableHook((void*)(*ppSwapChainDWM)->lpVtbl->Present);
        if (status) {
            logger << "MH_EnableHook() returned status " << status << std::endl;
            return hr;
        }

        g_presentHooked = true;
    }

    return hr;
}

bool g_createDXGIFactoryHooked = false;
HRESULT (__stdcall *g_trueCreateDXGIFactory)(REFIID iid, void **iface);
HRESULT __stdcall OurCreateDXGIFactory(REFIID iid, void **iface)
{
    if (!iface)
        return E_INVALIDARG;

    logger << "Hook: CreateDXGIFactory" << std::endl;

    HRESULT hr = g_trueCreateDXGIFactory(iid, iface);
    if (SUCCEEDED(hr) && !g_createSwapChainHooked) {
        com::ptr<IDXGIFactoryDWM> factory;
        if SUCCEEDED(IUnknown_QueryInterface(reinterpret_cast<IUnknown*>(*iface), IID_IDXGIFactoryDWM, com::out_arg_void(factory))) {
            // now hook the CreateSwapChain function
            MH_STATUS status;

            status = MH_CreateHook((void*)factory->lpVtbl->CreateSwapChain, (void*)OurCreateSwapChain, (void**)&g_trueCreateSwapChain);
            if (status) {
                logger << "MH_CreateHook() returned status " << status << std::endl;
                return hr;
            }

            status = MH_EnableHook((void*)factory->lpVtbl->CreateSwapChain);
            if (status) {
                logger << "MH_EnableHook() returned status " << status << std::endl;
                return hr;
            }

            g_createSwapChainHooked = true;
        } else {
            logger << "Secret DWM interface not present? o.O" << std::endl;
        }
    }

    return hr;
}

bool HookIt()
{
    util::dll_func<HRESULT (REFIID iid, void **iface)> createDxgiFactory { L"dxgi.dll", "CreateDXGIFactory" };

    // Hook CreateDXGIFactory
    MH_STATUS status;

    status = MH_Initialize();
    if (status) {
        logger << "MH_Initialize() returned status " << status << std::endl;
        return false;
    }

    status = MH_CreateHook((void*)createDxgiFactory.raw_func_ptr(), (void*)OurCreateDXGIFactory, (void**)&g_trueCreateDXGIFactory);
    if (status) {
        logger << "MH_CreateHook() returned status " << status << std::endl;
        return false;
    }

    status = MH_EnableHook((void*)createDxgiFactory.raw_func_ptr());
    if (status) {
        logger << "MH_EnableHook() returned status " << status << std::endl;
        return false;
    }

    g_createDXGIFactoryHooked = true;
    return true;
}

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

            // "I don't wanna go to school
            //  I just wanna break the rules"
            //
            // Don't do anything scary in DllMain.
            // Raymond Chen will hate me.

            HMODULE hDxgi = GetModuleHandleA("dxgi.dll");
            HMODULE hDwm  = GetModuleHandleA("dwm.exe");
            if (!hDxgi || !hDwm)
                return FALSE;

            logger << "Found DXGI.DLL inside DWM.EXE" << std::endl;

            if (!HookIt()) {
                logger << "Hoooking failed :(" << std::endl;
                return FALSE;
            }

            break;
        }
        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}
