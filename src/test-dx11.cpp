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

#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>

#include <cstring>
#include <algorithm>

#include "com.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "shaders.h"

constexpr UINT CURSOR_TEX_SIZE = 256;

class DuplicationRenderer
{
    util::dll_func<HRESULT (IDXGIAdapter *,
                            D3D_DRIVER_TYPE,
                            HMODULE,
                            UINT,
                            const D3D_FEATURE_LEVEL *,
                            UINT,
                            UINT,
                            const DXGI_SWAP_CHAIN_DESC *,
                            IDXGISwapChain **,
                            ID3D11Device **,
                            D3D_FEATURE_LEVEL *,
                            ID3D11DeviceContext **)> m_d3dCreator { L"d3d11.dll", "D3D11CreateDeviceAndSwapChain" };

    util::dll_func<HRESULT(IDXGIOutput*,IUnknown*,IDXGIOutputDuplication**)> m_dd4seven_duplicate { L"dd4seven-api.dll", "DuplicateOutput" };

    com::ptr<ID3D11Device>           m_device;
    com::ptr<ID3D11DeviceContext>    m_context;
    com::ptr<IDXGISwapChain>         m_swap;
    com::ptr<ID3D11RenderTargetView> m_renderTarget;
    com::ptr<ID3D11PixelShader>      m_pshader;
    com::ptr<ID3D11VertexShader>     m_vshader;
    com::ptr<ID3D11InputLayout>      m_ilayout;
    com::ptr<ID3D11SamplerState>     m_sampler;
    com::ptr<ID3D11BlendState>       m_blendState;

    com::ptr<ID3D11Texture2D>          m_desktopTexture;
    com::ptr<ID3D11ShaderResourceView> m_desktopSrv;
    com::ptr<ID3D11Texture2D>          m_cursorTexture;
    com::ptr<ID3D11ShaderResourceView> m_cursorSrv;
    com::ptr<ID3D11Buffer>             m_desktopVBuffer;
    com::ptr<ID3D11Buffer>             m_cursorVBuffer;

    com::ptr<IDXGIOutputDuplication> m_duplication;

    bool                     m_frameAcquired = false;
    DXGI_OUTDUPL_FRAME_INFO  m_duplInfo;
    com::ptr<IDXGIResource>  m_duplDesktopImage;

    bool m_cursorVisible = true;
    LONG m_cursorX       = 0;
    LONG m_cursorY       = 0;
    int  m_desktopWidth  = 0;
    int  m_desktopHeight = 0;
    int  m_desktopX      = 0;
    int  m_desktopY      = 0;

    struct VERTEX { float x; float y; float z; float u; float v; };

    bool m_isGood { false };
    bool good() { return m_isGood; }

    bool setupDxgiAndD3DDevice(HWND hwnd)
    {
        HRESULT hr;

        if (!m_d3dCreator)
            return false;

        // We create the device and swap chain in one go
        DXGI_SWAP_CHAIN_DESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count  = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = hwnd;
        desc.Windowed = true;

        D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_9_1 };
        D3D_FEATURE_LEVEL receivedLevel;
        hr = m_d3dCreator(nullptr,
                          D3D_DRIVER_TYPE_HARDWARE,
                          nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                          requestedLevels, sizeof(requestedLevels)/sizeof(requestedLevels[0]),
                          D3D11_SDK_VERSION,
                          &desc,
                          com::out_arg(m_swap),
                          com::out_arg(m_device),
                          &receivedLevel,
                          com::out_arg(m_context)
                         );
        if FAILED(hr) {
            logger << "Failed to create device and swap chain :( " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        // make vsync possible
        auto dxgiDevice = m_device.query<IDXGIDevice1>();
        if (dxgiDevice)
            dxgiDevice->SetMaximumFrameLatency(1);

        return true;
    }

    bool setupShaders()
    {
        HRESULT hr;

        // Load the shaders
        hr = m_device->CreatePixelShader(shader_compiled_PShader, sizeof(shader_compiled_PShader), nullptr, com::out_arg(m_pshader));
        if FAILED(hr) {
            logger << "Failed to create pixel shader :( " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        hr = m_device->CreateVertexShader(shader_compiled_VShader, sizeof(shader_compiled_VShader), nullptr, com::out_arg(m_vshader));
        if FAILED(hr) {
            logger << "Failed to create vertex shader :( " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        m_context->VSSetShader(m_vshader, nullptr, 0);
        m_context->PSSetShader(m_pshader, nullptr, 0);

        return true;
    }

    bool setupInputLayout()
    {
        HRESULT hr;

        // Setup the input layout
        D3D11_INPUT_ELEMENT_DESC ied[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        hr = m_device->CreateInputLayout(ied, 2, shader_compiled_VShader, sizeof(shader_compiled_VShader), com::out_arg(m_ilayout));
        if FAILED(hr) {
            logger << "Failed to create input layout " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        m_context->IASetInputLayout(m_ilayout);

        return true;
    }

    bool setupSamplers()
    {
        HRESULT hr;

        // Create and set a sampler
        D3D11_SAMPLER_DESC samplerdsc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
            .MipLODBias = 0.0f,
            .MaxAnisotropy = 1,
            .ComparisonFunc = D3D11_COMPARISON_ALWAYS,
            .BorderColor = { 0.0f, 0.0f, 0.0f, 1.0f },
            .MinLOD = 0.0f,
            .MaxLOD = D3D11_FLOAT32_MAX
        };
        hr = m_device->CreateSamplerState(&samplerdsc, com::out_arg(m_sampler));
        if FAILED(hr) {
            logger << "Failed to create sampler state: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }
        m_context->PSSetSamplers(0, 1, com::single_item_array(m_sampler));

        return true;
    }

    bool setupBlendState()
    {
        HRESULT hr;

        // setup the blend state
        D3D11_BLEND_DESC blenddsc;
        memset(&blenddsc, 0, sizeof(D3D11_BLEND_DESC));

        blenddsc.AlphaToCoverageEnable = FALSE;
        blenddsc.IndependentBlendEnable = FALSE;
        blenddsc.RenderTarget[0] = D3D11_RENDER_TARGET_BLEND_DESC({
            .BlendEnable = TRUE,
            .SrcBlend = D3D11_BLEND_SRC_ALPHA,
            .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
            .BlendOp = D3D11_BLEND_OP_ADD,
            .SrcBlendAlpha = D3D11_BLEND_ZERO,
            .DestBlendAlpha = D3D11_BLEND_ZERO,
            .BlendOpAlpha = D3D11_BLEND_OP_ADD,
            .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL
        });

        hr = m_device->CreateBlendState(&blenddsc, com::out_arg(m_blendState));
        if FAILED(hr) {
            logger << "Failed to create blend state: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }
        m_context->OMSetBlendState(m_blendState, nullptr, 0xFFFFFFFF);

        return true;
    }

    bool setupDesktopTextureAndVertices()
    {
        HRESULT hr;

        if (!m_duplication || !m_device)
            return false;

        DXGI_OUTDUPL_DESC dpldesc;
        m_duplication->GetDesc(&dpldesc);

        D3D11_TEXTURE2D_DESC texdsc = {
            .Width = dpldesc.ModeDesc.Width,
            .Height = dpldesc.ModeDesc.Height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {
                .Count = 1,
                .Quality = 0
            },
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = 0,
            .MiscFlags = 0
        };

        hr = m_device->CreateTexture2D(&texdsc, nullptr, com::out_arg(m_desktopTexture));
        if FAILED(hr) {
            logger << "Failed: CreateTexture2D: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        hr = m_device->CreateShaderResourceView(m_desktopTexture, nullptr, com::out_arg(m_desktopSrv));
        if FAILED(hr) {
            logger << "Failed: CreateShaderResourceView: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        // create vertex buffers
        VERTEX desktopVertices[] = {
            //  X  |   Y  |  Z  |  U  |  V   |
            { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f }, // RIGHT BOTTOM
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f }, // LEFT BOTTOM
            { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  1.0f,  1.0f, 0.0f, 1.0f, 0.0f }, // RIGHT TOP
            {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f }  // RIGHT BOTTOM
        };
        D3D11_BUFFER_DESC desktopVBufferDesc = {
            .ByteWidth = sizeof(desktopVertices),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = 0,
            .MiscFlags = 0,
            .StructureByteStride = sizeof(desktopVertices[0])
        };
        D3D11_SUBRESOURCE_DATA desktopVBufferData = {
            .pSysMem = desktopVertices,
            .SysMemPitch = 0,
            .SysMemSlicePitch = 0
        };
        hr = m_device->CreateBuffer(&desktopVBufferDesc, &desktopVBufferData, com::out_arg(m_desktopVBuffer));
        if FAILED(hr) {
            logger << "FAILED: CreateBuffer (desktopVBuffer): " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        return true;
    }

    bool setupCursorTextureAndVertices()
    {
        HRESULT hr;

        D3D11_TEXTURE2D_DESC texdsc = {
            .Width = CURSOR_TEX_SIZE,
            .Height = CURSOR_TEX_SIZE,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {
                .Count = 1,
                .Quality = 0
            },
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = 0
        };

        hr = m_device->CreateTexture2D(&texdsc, nullptr, com::out_arg(m_cursorTexture));
        if FAILED(hr) {
            logger << "Failed:CreateTexture2D: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }


        hr = m_device->CreateShaderResourceView(m_cursorTexture, nullptr, com::out_arg(m_cursorSrv));
        if FAILED(hr) {
            logger << "Fail: CreateShaderResourceView: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        // the cursor needs a vertex buffer, too!
        VERTEX vertices[6] = {
            //  X  |   Y  |  Z  |  U  |  V   |
            {  0.0f,  0.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  0.0f,  0.0f, 0.0f, 1.0f, 1.0f }, // RIGHT BOTTOM
            {  0.0f,  0.0f, 0.0f, 0.0f, 1.0f }, // LEFT BOTTOM
            {  0.0f,  0.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  0.0f,  0.0f, 0.0f, 1.0f, 0.0f }, // RIGHT TOP
            {  0.0f,  0.0f, 0.0f, 1.0f, 1.0f }  // RIGHT BOTTOM
        };
        D3D11_BUFFER_DESC vbufferDesc = {
            .ByteWidth = sizeof(vertices),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = 0,
            .StructureByteStride = sizeof(vertices[0])
        };
        D3D11_SUBRESOURCE_DATA vbufferData = {
            .pSysMem = vertices,
            .SysMemPitch = 0,
            .SysMemSlicePitch = 0
        };
        hr = m_device->CreateBuffer(&vbufferDesc, &vbufferData, com::out_arg(m_cursorVBuffer));
        if FAILED(hr) {
            logger << "FAILED: CreateBuffer (cursorVBuffer): " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        return true;
    }

    void updateCursorPosition()
    {
        if (!m_cursorVBuffer)
            return;

        float left   = -1.0f + 2.0f*static_cast<float>(m_cursorX)/static_cast<float>(m_desktopWidth);
        float top    =  1.0f - 2.0f*static_cast<float>(m_cursorY)/static_cast<float>(m_desktopHeight);
        float right  =  left + 2.0f*static_cast<float>(CURSOR_TEX_SIZE)/static_cast<float>(m_desktopWidth);
        float bottom =  top  - 2.0f*static_cast<float>(CURSOR_TEX_SIZE)/static_cast<float>(m_desktopHeight);
        float uleft   = 0.0f;
        float vtop    = 0.0f;
        float uright  = 1.0f;
        float vbottom = 1.0f;

        // now write this into the vertex buffer
        VERTEX *vertices = nullptr;

        D3D11_MAPPED_SUBRESOURCE map;

        HRESULT hr = m_context->Map(m_cursorVBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        if FAILED(hr) {
            logger << "FAILED: ID3D11Buffer::Map: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        vertices = (VERTEX*)map.pData;

        //  X   |   Y   |  Z  |   U   |  V     |
        vertices[0] = { left,  top,    0.0f, uleft,  vtop    }; // LEFT TOP
        vertices[1] = { right, bottom, 0.0f, uright, vbottom }; // RIGHT BOTTOM
        vertices[2] = { left,  bottom, 0.0f, uleft,  vbottom }; // LEFT BOTTOM
        vertices[3] = { left,  top,    0.0f, uleft,  vtop    }; // LEFT TOP
        vertices[4] = { right, top,    0.0f, uright, vtop    }; // RIGHT TOP
        vertices[5] = { right, bottom, 0.0f, uright, vbottom }; // RIGHT BOTTOM

        m_context->Unmap(m_cursorVBuffer, 0);
    }

    void
    acquireFrame()
    {
        HRESULT hr;

        if (!m_duplication || !m_device)
            return;

        hr = m_duplication->AcquireNextFrame(100, &m_duplInfo, com::out_arg(m_duplDesktopImage));

        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return; // This can happen if the screen is idle or whatever, it's not really fatal enough to log

        if (!(m_frameAcquired = SUCCEEDED(hr)))
            logger << "Failed: AcquireNextFrame: " << util::hresult_to_utf8(hr) << std::endl;

        // if the access has been lost, we might get away with just recreating it again
        if (FAILED(hr) && hr == DXGI_ERROR_ACCESS_LOST) {
            logger << "Recreating the IDXGIOutputDuplication interface because of DXGI_ERROR_ACCESS_LOST=" << hr << std::endl;

            //TODO: Wait for two seconds

            reset(m_desktopX, m_desktopY, m_desktopWidth, m_desktopHeight);
        }
    }

    void updateDesktop()
    {
        if (!m_desktopTexture || !m_frameAcquired || !m_device)
            return;

        if (!m_duplInfo.LastPresentTime.QuadPart || !m_duplDesktopImage)
            return;

        auto d3dresource = m_duplDesktopImage.query<ID3D11Resource>();
        m_context->CopyResource(m_desktopTexture, d3dresource);
    }

    void
    updateCursor()
    {
        HRESULT hr;

        if (!m_cursorTexture || !m_frameAcquired)
            return;

        if (!m_duplInfo.LastMouseUpdateTime.QuadPart)
            return;

        if ((m_cursorVisible = m_duplInfo.PointerPosition.Visible)) {
            m_cursorX = m_duplInfo.PointerPosition.Position.x;
            m_cursorY = m_duplInfo.PointerPosition.Position.y;
        }

        if (!m_duplInfo.PointerShapeBufferSize)
            return;

        std::unique_ptr<uint8_t[]> buffer(new uint8_t[m_duplInfo.PointerShapeBufferSize]);
        std::memset(buffer.get(), 0, m_duplInfo.PointerShapeBufferSize);

        DXGI_OUTDUPL_POINTER_SHAPE_INFO pointer;
        UINT dummy;
        hr = m_duplication->GetFramePointerShape(m_duplInfo.PointerShapeBufferSize, buffer.get(), &dummy, &pointer);
        if FAILED(hr) {
            logger << "Failed: GetFramePointerShape: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        // we can now update the pointer shape
        D3D11_MAPPED_SUBRESOURCE info;
        hr = m_context->Map(m_cursorTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &info);
        if FAILED(hr) {
            logger << "Failed: ID3D10Texture2D::Map: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        // first, make it black and transparent
        memset(info.pData, 0, 4 * CURSOR_TEX_SIZE * CURSOR_TEX_SIZE);

        // then, fill it with the new cursor
        if (pointer.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
            for (UINT row = 0; row < std::min(pointer.Height, CURSOR_TEX_SIZE); ++row) {
                std::memcpy((char*)info.pData + row*info.RowPitch, buffer.get() + row*pointer.Pitch, std::min(pointer.Width, CURSOR_TEX_SIZE)*4);
            }
        } else if (pointer.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
            //FIXME: We don't want to read the desktop image back into the CPU, so we apply the mask
            // onto a black background. This is not correct.
            //FIXME: I haven't found a way yet to trigger this codepath at runtime.
            for (UINT row = 0; row < std::min(pointer.Height, CURSOR_TEX_SIZE); ++row) {
                for (UINT col = 0; col < std::min(pointer.Width, CURSOR_TEX_SIZE); ++col) {
                    uint8_t *target = &reinterpret_cast<uint8_t*>(info.pData)[row*info.RowPitch + col*4];
                    uint8_t *source = &buffer[row*pointer.Pitch + col*4];

                    // the mask value doesn't matter because
                    //  mask==0     => Use source RGB values
                    //  mask==0xFF  => Use source RGB XOR target RGB = source RGB if the target is black
                    target[0] = source[0];
                    target[1] = source[1];
                    target[2] = source[2];
                    target[3] = 0xFF;
                }
            }
        } else {
            //FIXME: We don't want to read the desktop image back into the CPU, so we pretend to
            //       apply the AND mask onto a black surface. This is incorrect, but doesn't look too bad.

            uint8_t *and_map = buffer.get();
            uint8_t *xor_map = and_map + pointer.Pitch*pointer.Height/2;

            for (UINT row = 0; row < std::min(pointer.Height/2, CURSOR_TEX_SIZE); ++row)
            {
                uint8_t *and_row = &and_map[row * pointer.Pitch];
                uint8_t *xor_row = &xor_map[row * pointer.Pitch];

                for (UINT col = 0; col < std::min(pointer.Width, CURSOR_TEX_SIZE); ++col) {
                    uint8_t *target = &reinterpret_cast<uint8_t*>(info.pData)[row*info.RowPitch + col*4];

                    uint8_t alpha = util::get_pixel_from_row<1>(and_row, col) ? 0 : 0xFF;
                    uint8_t rgb   = util::get_pixel_from_row<1>(xor_row, col) ? 0xFF : 0;

                    target[0] = rgb;
                    target[1] = rgb;
                    target[2] = rgb;
                    target[3] = alpha;
                }
            }
        }

        m_context->Unmap(m_cursorTexture, 0);
    }

    void
    releaseFrame()
    {
        if (m_frameAcquired && m_duplication)
            m_duplication->ReleaseFrame();

        m_frameAcquired = false;
    }

public:
    DuplicationRenderer(HWND hwnd, int x, int y, int w, int h)
    {
        m_desktopWidth  = w;
        m_desktopHeight = h;

        if (!setupDxgiAndD3DDevice(hwnd))
            return;

        if (!setupShaders())
            return;

        if (!setupInputLayout())
            return;

        if (!setupSamplers())
            return;

        if (!setupBlendState())
            return;

        // sets render target and viewport
        RECT cr;
        GetClientRect(hwnd, &cr);
        this->resize(cr);

        m_isGood = this->reset(x, y, w, h);
    }

    void resize(const RECT& cr)
    {
        HRESULT hr;

        if (!m_device)
            return;

        // reset view
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_renderTarget.reset();

        hr = m_swap->ResizeBuffers(0, cr.right - cr.left, cr.bottom - cr.top, DXGI_FORMAT_UNKNOWN, 0);
        if FAILED(hr) {
            logger << "Failed to resize buffers :( " << util::hresult_to_utf8(hr) << std::endl;
        }

        // reset render target
        com::ptr<ID3D11Texture2D> backBuffer;
        hr = m_swap->GetBuffer(0, IID_PPV_ARGS(com::out_arg(backBuffer)));
        if FAILED(hr) {
            logger << "Failed to get back buffer :( " << util::hresult_to_utf8(hr) << std::endl;
        }

        hr = m_device->CreateRenderTargetView(backBuffer, nullptr, com::out_arg(m_renderTarget));
        if FAILED(hr) {
            logger << "Failed to create new render target view :( " << util::hresult_to_utf8(hr) << std::endl;
        }

        m_context->OMSetRenderTargets(1, com::single_item_array(m_renderTarget), nullptr);

        // Create and set a viewport
        D3D11_VIEWPORT viewport = {
            .TopLeftX = 0,
            .TopLeftY = 0,
            .Width    = static_cast<FLOAT>(cr.right - cr.left),
            .Height   = static_cast<FLOAT>(cr.bottom - cr.top),
            .MinDepth = 0,
            .MaxDepth = 0
        };
        m_context->RSSetViewports(1, &viewport);
    }

    bool reset(int x, int y, int w, int h)
    {
        m_desktopWidth  = w;
        m_desktopHeight = h;
        m_desktopX      = x;
        m_desktopY      = y;

        logger << "Resetting renderer to screen x="<<x<<" y="<<y<<" w="<<w<<" h="<<h << std::endl;

        m_duplication.reset();
        m_duplDesktopImage.reset();
        m_frameAcquired = false;

        // find the matching output
        com::ptr<IDXGIDevice>  dev;
        com::ptr<IDXGIAdapter> adp;

        dev = m_device.query<IDXGIDevice>();
        dev->GetAdapter(com::out_arg(adp));

        com::ptr<IDXGIOutput> output;
        for (UINT i = 0; SUCCEEDED(adp->EnumOutputs(i, com::out_arg(output))); ++i) {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);

            if (desc.AttachedToDesktop
                && desc.DesktopCoordinates.left == x
                && desc.DesktopCoordinates.top  == y
                && desc.DesktopCoordinates.right == x + w
                && desc.DesktopCoordinates.bottom == y + h)
            {
                logger << "Attempting to duplicate display " << i << std::endl;

                if (util::check_windows_version<std::equal_to<DWORD>>(6, 1)) {
                    // Win7
                    if (!m_dd4seven_duplicate) {
                        logger << "DuplicationSource: Missing compatible dd4seven-api.dll :(" << std::endl;
                        break;
                    }

                    HRESULT hr = m_dd4seven_duplicate(output, m_device, com::out_arg(m_duplication));
                    if FAILED(hr)
                        logger << "Attempted to duplicate display " << i << " but: " << util::hresult_to_utf8(hr) << std::endl;

                    break;
                } else {
                    // Win8
                    com::ptr<IDXGIOutput1> output1 = output.query<IDXGIOutput1>();

                    if (!output1)
                        continue;

                    HRESULT hr = output1->DuplicateOutput(m_device, com::out_arg(m_duplication));
                    if FAILED(hr)
                        logger << "Attempted to duplicate display " << i << " but: " << util::hresult_to_utf8(hr) << std::endl;

                    break;
                }
            }
        }

        if (!m_duplication) {
            logger << "WARNING: Couldn't find display: x="<<x<<" y="<<y<<" w="<<w<<" h="<<h<<std::endl;

            //TODO: Try again in two seconds

            return false;
        }

        setupDesktopTextureAndVertices();
        setupCursorTextureAndVertices();

        return true;
    }

    void render() {
        if (!m_device || !m_renderTarget || !m_context)
            return;

        // acquire and copy desktop texture
        acquireFrame();

        updateDesktop();
        updateCursor();

        updateCursorPosition();

        releaseFrame();

        // draw the scene
        float gray[4] = { 0.5, 0.5, 0.5, 1.0 };
        m_context->ClearRenderTargetView(m_renderTarget, gray);

        UINT stride = sizeof(VERTEX);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, com::single_item_array(m_desktopVBuffer), &stride, &offset);
        m_context->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->PSSetShaderResources(0, 1, com::single_item_array(m_desktopSrv));
        m_context->Draw(6, 0);

        if (m_cursorVisible) {
            m_context->IASetVertexBuffers(0, 1, com::single_item_array(m_cursorVBuffer), &stride, &offset);
            m_context->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->PSSetShaderResources(0, 1, com::single_item_array(m_cursorSrv));
            m_context->Draw(6, 0);
        }

        m_swap->Present(1, 0);
    }

    ~DuplicationRenderer()
    {
        m_context->ClearState();
    }
};

DuplicationRenderer *g_renderer = nullptr;

int x = 0, y = 0, w = 1024, h = 768;


static BOOL CALLBACK MyEnumMonitorProc(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM data)
{
    (void)monitor;
    (void)hdc;
    (void)data;

    x = rect->left;
    y = rect->top;
    w = rect->right - rect->left;
    h = rect->bottom - rect->top;

    return data ? FALSE : TRUE;
}

static LRESULT CALLBACK MyWindowProc(HWND hwnd, UINT msgid, WPARAM wp, LPARAM lp)
{
    static LPARAM lastMonitorChosen = 0;

    if (msgid == WM_SIZE) {
        RECT cr;
        GetClientRect(hwnd, &cr);

        g_renderer->resize(cr);
    } else if (msgid == WM_TIMER) {
        // choose another monitor
        EnumDisplayMonitors(NULL, NULL, MyEnumMonitorProc, (lastMonitorChosen = !lastMonitorChosen));

        fprintf(stderr, "changing monitor to %d,%d,%d,%d\n", x, y, w, h);

        if (g_renderer)
            g_renderer->reset(x, y, w, h);
    } else if (msgid == WM_CREATE) {
        SetTimer(hwnd, 0, 5000, NULL);

        delete g_renderer;
        g_renderer = new DuplicationRenderer(hwnd, x, y, w, h);
    } else if (msgid == WM_CLOSE) {
        DestroyWindow(hwnd);
        return TRUE;
    } else if (msgid == WM_DESTROY) {
        delete g_renderer;
        g_renderer = nullptr;
    } else if (msgid == WM_PAINT) {
        if (g_renderer)
            g_renderer->render();
    }

    return DefWindowProc(hwnd, msgid, wp, lp);
}

int main(int, char **)
{
    // register our class if possible, if not, skip it
    WNDCLASSEX wcex;
    memset(&wcex, 0, sizeof(wcex));
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MyWindowProc;
    wcex.hInstance = NULL;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"DesktopViewMainWindow";

    if (!RegisterClassEx(&wcex)) {
        logger << "Failed: RegisterClassEx: " << GetLastError() << std::endl;
        // we just might have been unlucky enough to register the class twice, so don't bail out yet.
        //return 0;
    }

    // Create the window
    HWND mywindow = CreateWindow(L"DesktopViewMainWindow",
                                 L"Injector display",
                                 WS_OVERLAPPEDWINDOW, /* TODO: change to child window */
                                 50, 50, 400, 400,
                                 NULL, NULL,
                                 NULL, NULL);

    ShowWindow(mywindow, SW_SHOW);


    MSG msg;
    std::memset(&msg, 0, sizeof(MSG));
    uint64_t last = 0;

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            if (g_renderer)
                g_renderer->render();

            // As a safety net against broken vsync, we cap the FPS at 100
            uint64_t previous = last;
            uint64_t now = last = util::milliseconds_now();
            if ((now - previous) < 10)
                Sleep(10 - static_cast<DWORD>(now - previous));

            //TODO: display fps?
            //logger << "FPS: " << 1000.0f/static_cast<float>(now - previous) << std::endl;
        }
    }

    return 0;
}
