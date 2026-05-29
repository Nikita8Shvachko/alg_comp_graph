#include "framework.h"
#include "hw4.h"
#include "DDSTextureLoader.h"
#include "EmbeddedDds.h"

#include <windowsx.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

constexpr size_t kMaxLoadString = 100;

WCHAR szTitle[kMaxLoadString];
WCHAR szWindowClass[kMaxLoadString];

namespace
{
    constexpr UINT kClientWidth = 1280;
    constexpr UINT kClientHeight = 720;
    constexpr FLOAT kBackColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    constexpr UINT kCubeIndexCount = 36;
    constexpr float kNearPlane = 100.0f;
    constexpr float kFarPlane = 0.1f;
    constexpr float kVerticalFovDegrees = 60.0f;
    constexpr float kFrustumCullInset = 1.95f; // >0 narrows effective culling frustum only
    constexpr float kRotationSpeed = 1.0f;
    constexpr float kCameraRotateSpeed = 1.75f;
    constexpr float kCameraZoomSpeed = 2.0f;
    constexpr float kMouseRotationSensitivity = 0.01f;
    constexpr float kMouseWheelZoomStep = 0.5f;
    constexpr int kMaxLights = 10;
    constexpr UINT kMaxInstances = 100;
    constexpr UINT kDefaultInstanceCount = 50;

    struct TextureVertex
    {
        float position[3];
        float tangent[3];
        float normal[3];
        float uv[2];
    };

    struct Light
    {
        DirectX::XMFLOAT4 pos;
        DirectX::XMFLOAT4 color;
    };

    struct SkyVertex
    {
        float position[3];
    };

    struct ModelBuffer
    {
        DirectX::XMFLOAT4X4 model;
        DirectX::XMFLOAT4 material;
    };

    struct SceneBuffer
    {
        DirectX::XMFLOAT4X4 vp;
        DirectX::XMFLOAT4 cameraPos;
        DirectX::XMFLOAT4 ambientColor;
        DirectX::XMINT4 lightCount;
        Light lights[kMaxLights];
    };

    struct SkySceneBuffer
    {
        DirectX::XMFLOAT4X4 vp;
        DirectX::XMFLOAT4 cameraPos;
    };

    struct SkyGeomBuffer
    {
        DirectX::XMFLOAT4X4 model;
        DirectX::XMFLOAT4 size;
    };

    struct TransparentColorBuffer
    {
        DirectX::XMFLOAT4 color;
    };

    struct TransparentObject
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
        float distanceToCamera;
    };

    struct GeomBufferGpuData
    {
        DirectX::XMFLOAT4X4 model;
        DirectX::XMFLOAT4X4 normalMatrix;
        DirectX::XMFLOAT4 params; // x = shininess, y = rotationSpeed, z = textureId, w = useNormalMap
    };

    struct InstanceCpuData
    {
        DirectX::XMFLOAT4X4 modelCpu; // non-transposed, CPU-side transform for culling
        GeomBufferGpuData gpu;        // transposed GPU payload (must match HLSL layout exactly)
    };

    struct VisibleIndexData
    {
        DirectX::XMUINT4 packed;
    };

    struct AABB
    {
        DirectX::XMFLOAT3 min;
        DirectX::XMFLOAT3 max;
    };

    struct PostProcessParams
    {
        DirectX::XMFLOAT4 brightnessContrast; // x = brightness, y = contrast
    };

    constexpr TextureVertex kTexturedCubeVertices[] =
    {
        // +Z
        { { -0.5f, -0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 0.0f } },
        // -Z
        { { -0.5f, -0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 0.0f } },
        // -X
        { { -0.5f, -0.5f, -0.5f }, {  0.0f,  0.0f,  1.0f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f } },
        { { -0.5f, -0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f } },
        { { -0.5f,  0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f, -0.5f }, {  0.0f,  0.0f,  1.0f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f } },
        // +X
        { {  0.5f, -0.5f,  0.5f }, {  0.0f,  0.0f, -1.0f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f } },
        { {  0.5f,  0.5f,  0.5f }, {  0.0f,  0.0f, -1.0f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f } },
        // +Y
        { { -0.5f,  0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 1.0f } },
        { {  0.5f,  0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f } },
        // -Y
        { { -0.5f, -0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 1.0f } },
        { {  0.5f, -0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f } },
        { { -0.5f, -0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f } },
    };

    constexpr SkyVertex kSkyboxVertices[] =
    {
        { { -0.5f, -0.5f,  0.5f } }, { {  0.5f, -0.5f,  0.5f } }, { {  0.5f,  0.5f,  0.5f } }, { { -0.5f,  0.5f,  0.5f } },
        { { -0.5f, -0.5f, -0.5f } }, { {  0.5f, -0.5f, -0.5f } }, { {  0.5f,  0.5f, -0.5f } }, { { -0.5f,  0.5f, -0.5f } },
        { { -0.5f, -0.5f, -0.5f } }, { { -0.5f, -0.5f,  0.5f } }, { { -0.5f,  0.5f,  0.5f } }, { { -0.5f,  0.5f, -0.5f } },
        { {  0.5f, -0.5f,  0.5f } }, { {  0.5f, -0.5f, -0.5f } }, { {  0.5f,  0.5f, -0.5f } }, { {  0.5f,  0.5f,  0.5f } },
        { { -0.5f,  0.5f,  0.5f } }, { {  0.5f,  0.5f,  0.5f } }, { {  0.5f,  0.5f, -0.5f } }, { { -0.5f,  0.5f, -0.5f } },
        { { -0.5f, -0.5f, -0.5f } }, { {  0.5f, -0.5f, -0.5f } }, { {  0.5f, -0.5f,  0.5f } }, { { -0.5f, -0.5f,  0.5f } },
    };

    constexpr std::uint16_t kCubeIndices[] =
    {
        0, 1, 2, 0, 2, 3,       // +Z
        4, 6, 5, 4, 7, 6,       // -Z
        8, 9, 10, 8, 10, 11,    // -X
        12, 13, 14, 12, 14, 15, // +X
        16, 17, 19, 17, 18, 19, // +Y  
        20, 21, 23, 21, 22, 23, // -Y 
    };

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr)
        {
            object->Release();
            object = nullptr;
        }
    }

    template <typename T>
    constexpr const T& ClampValue(const T& value, const T& minValue, const T& maxValue)
    {
        return (value < minValue) ? minValue : ((value > maxValue) ? maxValue : value);
    }

    HINSTANCE g_hInstance = nullptr;
    HWND g_hWnd = nullptr;

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_deviceContext = nullptr;
    IDXGISwapChain1* g_swapChain = nullptr;
    ID3D11RenderTargetView* g_backBufferRTV = nullptr;
    ID3D11Texture2D* g_depthStencilTexture = nullptr;
    ID3D11DepthStencilView* g_depthStencilView = nullptr;
    ID3D11Buffer* g_vertexBuffer = nullptr;
    ID3D11Buffer* g_skyVertexBuffer = nullptr;
    ID3D11Buffer* g_indexBuffer = nullptr;
    ID3D11Buffer* g_modelBuffer = nullptr;
    ID3D11Buffer* g_sceneBuffer = nullptr;
    ID3D11Buffer* g_skySceneBuffer = nullptr;
    ID3D11Buffer* g_skyGeomBuffer = nullptr;
    ID3D11VertexShader* g_vertexShader = nullptr;
    ID3D11PixelShader* g_pixelShader = nullptr;
    ID3D11PixelShader* g_transparentPixelShader = nullptr;
    ID3D11VertexShader* g_skyVertexShader = nullptr;
    ID3D11PixelShader* g_skyPixelShader = nullptr;
    ID3D11InputLayout* g_inputLayout = nullptr;
    ID3D11InputLayout* g_skyInputLayout = nullptr;
    ID3D11RasterizerState* g_rasterizerState = nullptr;
    ID3D11RasterizerState* g_skyRasterizerState = nullptr;
    ID3D11RasterizerState* g_postProcessRasterizerState = nullptr;
    ID3D11DepthStencilState* g_depthStencilStateOpaque = nullptr;
    ID3D11DepthStencilState* g_depthStencilStateSkybox = nullptr;
    ID3D11DepthStencilState* g_depthStencilStateTransparent = nullptr;
    ID3D11BlendState* g_transparentBlendState = nullptr;
    ID3D11ShaderResourceView* g_cubeTextureSRV = nullptr;
    ID3D11ShaderResourceView* g_normalMapSRV = nullptr;
    ID3D11ShaderResourceView* g_leatherNormalMapSRV = nullptr;
    ID3D11ShaderResourceView* g_skyboxCubemapSRV = nullptr;
    ID3D11Resource* g_cubeTextureResource = nullptr;
    ID3D11Resource* g_normalMapResource = nullptr;
    ID3D11Resource* g_leatherNormalMapResource = nullptr;
    ID3D11Resource* g_skyboxCubemapResource = nullptr;
    ID3D11SamplerState* g_samplerState = nullptr;
    ID3D11Buffer* g_transparentColorBuffer = nullptr;
    ID3D11Buffer* g_geomBufferInst = nullptr;
    ID3D11Buffer* g_visibleIndicesBuffer = nullptr;
    ID3D11Buffer* g_postProcessParamsBuffer = nullptr;
    ID3D11Texture2D* g_offscreenTexture = nullptr;
    ID3D11RenderTargetView* g_offscreenRTV = nullptr;
    ID3D11ShaderResourceView* g_offscreenSRV = nullptr;
    ID3D11Texture2D* g_textureArray = nullptr;
    ID3D11ShaderResourceView* g_textureArraySRV = nullptr;
    ID3D11VertexShader* g_postProcessVS = nullptr;
    ID3D11PixelShader* g_postProcessPS = nullptr;
    D3D11_VIEWPORT g_viewport = {};
    std::vector<InstanceCpuData> g_instancesData;
    std::vector<UINT> g_visibleIndices;
    std::vector<VisibleIndexData> g_visibleIndexData;
    int g_postProcessMode = 2; // 1=off, 2=soft, 3=extreme
    UINT g_lastVisibleCount = 0;

    std::chrono::steady_clock::time_point g_startTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point g_previousFrameTime = g_startTime;
    float g_cameraYaw = 0.0f;
    float g_cameraPitch = DirectX::XMConvertToRadians(20.0f);
    float g_cameraDistance = 4.0f;
    DirectX::XMFLOAT3 g_cameraWorldPosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    bool g_isMouseDragging = false;
    POINT g_lastMousePosition = {};

    std::wstring GetExecutableDirectory()
    {
        wchar_t modulePath[MAX_PATH] = {};
        const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
        if (pathLength == 0 || pathLength == ARRAYSIZE(modulePath))
        {
            return L"";
        }

        std::wstring path(modulePath, pathLength);
        const size_t separatorPos = path.find_last_of(L"\\/");
        if (separatorPos == std::wstring::npos)
        {
            return L"";
        }

        return path.substr(0, separatorPos + 1);
    }

    std::wstring GetCurrentWorkingDirectoryW()
    {
        wchar_t buffer[MAX_PATH] = {};
        const DWORD length = GetCurrentDirectoryW(MAX_PATH, buffer);
        if (length == 0 || length >= MAX_PATH)
        {
            return L"";
        }

        std::wstring path(buffer, length);
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        {
            path.push_back(L'\\');
        }

        return path;
    }

    bool FileExistsW(const std::wstring& path)
    {
        if (path.empty())
        {
            return false;
        }

        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES
            && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    constexpr std::uint32_t PackRGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        return static_cast<std::uint32_t>(r)
            | (static_cast<std::uint32_t>(g) << 8)
            | (static_cast<std::uint32_t>(b) << 16)
            | (static_cast<std::uint32_t>(a) << 24);
    }

    void LogTextureSource(const wchar_t* kind, const std::wstring& source)
    {
        if (kind == nullptr)
        {
            return;
        }

        wchar_t message[512] = {};
        const wchar_t* resolvedSource = source.empty() ? L"(unknown)" : source.c_str();
        swprintf_s(message, ARRAYSIZE(message), L"[hw4] %ls source: %ls\n", kind, resolvedSource);
        OutputDebugStringW(message);
    }

    bool CreateFallbackCubeTexture2D(ID3D11Device* device, ID3D11Resource** outResource, ID3D11ShaderResourceView** outSrv)
    {
        if (device == nullptr || outResource == nullptr || outSrv == nullptr)
        {
            return false;
        }

        *outResource = nullptr;
        *outSrv = nullptr;

        constexpr UINT kW = 4;
        constexpr UINT kH = 4;
        std::uint32_t pixels[kW * kH] = {};
        for (UINT y = 0; y < kH; ++y)
        {
            for (UINT x = 0; x < kW; ++x)
            {
                const bool checker = ((x + y) & 1u) != 0;
                pixels[y * kW + x] = checker ? PackRGBA(180, 120, 90, 255) : PackRGBA(90, 140, 180, 255);
            }
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = kW;
        desc.Height = kH;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels;
        sub.SysMemPitch = kW * sizeof(std::uint32_t);

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &sub, &tex);
        if (FAILED(hr) || tex == nullptr)
        {
            return false;
        }

        hr = device->CreateShaderResourceView(tex, nullptr, outSrv);
        if (FAILED(hr))
        {
            SafeRelease(tex);
            return false;
        }

        *outResource = tex;
        return true;
    }

    bool CreateFallbackNormalTexture2D(ID3D11Device* device, ID3D11Resource** outResource, ID3D11ShaderResourceView** outSrv)
    {
        if (device == nullptr || outResource == nullptr || outSrv == nullptr)
        {
            return false;
        }

        *outResource = nullptr;
        *outSrv = nullptr;

        constexpr UINT kW = 4;
        constexpr UINT kH = 4;
        std::uint32_t pixels[kW * kH] = {};
        for (UINT i = 0; i < (kW * kH); ++i)
        {
            pixels[i] = PackRGBA(128, 128, 255, 255); // flat +Z normal in tangent space
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = kW;
        desc.Height = kH;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels;
        sub.SysMemPitch = kW * sizeof(std::uint32_t);

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &sub, &tex);
        if (FAILED(hr) || tex == nullptr)
        {
            return false;
        }

        hr = device->CreateShaderResourceView(tex, nullptr, outSrv);
        if (FAILED(hr))
        {
            SafeRelease(tex);
            return false;
        }

        *outResource = tex;
        return true;
    }

    bool CreateFallbackCubemapTexture(ID3D11Device* device, ID3D11Resource** outResource, ID3D11ShaderResourceView** outSrv)
    {
        if (device == nullptr || outResource == nullptr || outSrv == nullptr)
        {
            return false;
        }

        *outResource = nullptr;
        *outSrv = nullptr;

        constexpr UINT kFaceSize = 64;
        std::vector<std::uint32_t> facePixels(6u * kFaceSize * kFaceSize);
        const std::uint8_t faceBase[6][3] =
        {
            { 220, 80, 80 },   // +X
            { 80, 220, 80 },   // -X
            { 80, 80, 220 },   // +Y
            { 220, 220, 80 },  // -Y
            { 220, 80, 220 },  // +Z
            { 80, 220, 220 },  // -Z
        };

        for (UINT face = 0; face < 6; ++face)
        {
            for (UINT y = 0; y < kFaceSize; ++y)
            {
                for (UINT x = 0; x < kFaceSize; ++x)
                {
                    const std::uint8_t rBase = faceBase[face][0];
                    const std::uint8_t gBase = faceBase[face][1];
                    const std::uint8_t bBase = faceBase[face][2];
                    const std::uint8_t xFactor = static_cast<std::uint8_t>((x * 120u) / (kFaceSize - 1u));
                    const std::uint8_t yFactor = static_cast<std::uint8_t>((y * 120u) / (kFaceSize - 1u));
                    const bool grid = (x % 8u == 0u) || (y % 8u == 0u);

                    std::uint8_t r = static_cast<std::uint8_t>((std::min)(255u, static_cast<unsigned int>(rBase) + xFactor));
                    std::uint8_t g = static_cast<std::uint8_t>((std::min)(255u, static_cast<unsigned int>(gBase) + yFactor));
                    std::uint8_t b = static_cast<std::uint8_t>((std::min)(255u, static_cast<unsigned int>(bBase) + ((xFactor + yFactor) / 2u)));

                    if (grid)
                    {
                        r = static_cast<std::uint8_t>(255u - r / 2u);
                        g = static_cast<std::uint8_t>(255u - g / 2u);
                        b = static_cast<std::uint8_t>(255u - b / 2u);
                    }

                    const size_t pixelIndex = static_cast<size_t>(face) * kFaceSize * kFaceSize
                        + static_cast<size_t>(y) * kFaceSize
                        + static_cast<size_t>(x);
                    facePixels[pixelIndex] = PackRGBA(r, g, b, 255);
                }
            }
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = kFaceSize;
        desc.Height = kFaceSize;
        desc.MipLevels = 1;
        desc.ArraySize = 6;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        D3D11_SUBRESOURCE_DATA init[6] = {};
        for (int i = 0; i < 6; ++i)
        {
            init[i].pSysMem = facePixels.data() + static_cast<size_t>(i) * kFaceSize * kFaceSize;
            init[i].SysMemPitch = kFaceSize * sizeof(std::uint32_t);
        }

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, init, &tex);
        if (FAILED(hr) || tex == nullptr)
        {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;
        srvDesc.TextureCube.MostDetailedMip = 0;

        hr = device->CreateShaderResourceView(tex, &srvDesc, outSrv);
        if (FAILED(hr))
        {
            SafeRelease(tex);
            return false;
        }

        *outResource = tex;
        return true;
    }

    bool CompileShaderFromFile(const wchar_t* fileName, const char* target, ID3DBlob** shaderBlob)
    {
        if (shaderBlob == nullptr)
        {
            return false;
        }

        *shaderBlob = nullptr;

        const std::wstring executableDirectory = GetExecutableDirectory();
        const std::wstring candidatePaths[] =
        {
            fileName,
            executableDirectory + fileName,
            executableDirectory + L"..\\..\\" + fileName,
            executableDirectory + L"..\\..\\..\\" + fileName,
        };

        UINT shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        shaderFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        HRESULT hr = E_FAIL;
        ID3DBlob* errorBlob = nullptr;
        std::wstring compiledPath;

        for (const std::wstring& candidatePath : candidatePaths)
        {
            SafeRelease(errorBlob);
            hr = D3DCompileFromFile(
                candidatePath.c_str(),
                nullptr,
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "main",
                target,
                shaderFlags,
                0,
                shaderBlob,
                &errorBlob);

            if (SUCCEEDED(hr))
            {
                SafeRelease(errorBlob);
                return true;
            }

            compiledPath = candidatePath;

            if (errorBlob != nullptr)
            {
                break;
            }
        }

        if (errorBlob != nullptr)
        {
            MessageBoxA(
                g_hWnd,
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                "Shader compilation failed",
                MB_ICONERROR | MB_OK);
        }
        else
        {
            std::wstring message = L"Не удалось открыть файл шейдера:\n";
            message += compiledPath.empty() ? std::wstring(fileName) : compiledPath;
            MessageBoxW(g_hWnd, message.c_str(), szTitle, MB_ICONERROR | MB_OK);
        }

        SafeRelease(errorBlob);
        SafeRelease(*shaderBlob);
        return false;
    }

    bool CreateCubeGeometry()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.ByteWidth = static_cast<UINT>(sizeof(kTexturedCubeVertices));
        vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vertexData = {};
        vertexData.pSysMem = kTexturedCubeVertices;

        HRESULT hr = g_device->CreateBuffer(&vertexBufferDesc, &vertexData, &g_vertexBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC skyVbDesc = {};
        skyVbDesc.ByteWidth = static_cast<UINT>(sizeof(kSkyboxVertices));
        skyVbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        skyVbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA skyVbData = {};
        skyVbData.pSysMem = kSkyboxVertices;

        hr = g_device->CreateBuffer(&skyVbDesc, &skyVbData, &g_skyVertexBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC indexBufferDesc = {};
        indexBufferDesc.ByteWidth = static_cast<UINT>(sizeof(kCubeIndices));
        indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA indexData = {};
        indexData.pSysMem = kCubeIndices;

        hr = g_device->CreateBuffer(&indexBufferDesc, &indexData, &g_indexBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    bool CreateConstantBuffers()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        D3D11_BUFFER_DESC modelBufferDesc = {};
        modelBufferDesc.ByteWidth = sizeof(ModelBuffer);
        modelBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        modelBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        HRESULT hr = g_device->CreateBuffer(&modelBufferDesc, nullptr, &g_modelBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC sceneBufferDesc = {};
        sceneBufferDesc.ByteWidth = sizeof(SceneBuffer);
        sceneBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        sceneBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        sceneBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = g_device->CreateBuffer(&sceneBufferDesc, nullptr, &g_sceneBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC skySceneDesc = {};
        skySceneDesc.ByteWidth = sizeof(SkySceneBuffer);
        skySceneDesc.Usage = D3D11_USAGE_DYNAMIC;
        skySceneDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        skySceneDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = g_device->CreateBuffer(&skySceneDesc, nullptr, &g_skySceneBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC skyGeomDesc = {};
        skyGeomDesc.ByteWidth = sizeof(SkyGeomBuffer);
        skyGeomDesc.Usage = D3D11_USAGE_DEFAULT;
        skyGeomDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = g_device->CreateBuffer(&skyGeomDesc, nullptr, &g_skyGeomBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC transparentColorDesc = {};
        transparentColorDesc.ByteWidth = sizeof(TransparentColorBuffer);
        transparentColorDesc.Usage = D3D11_USAGE_DEFAULT;
        transparentColorDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = g_device->CreateBuffer(&transparentColorDesc, nullptr, &g_transparentColorBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC instDesc = {};
        instDesc.ByteWidth = sizeof(GeomBufferGpuData) * kMaxInstances;
        instDesc.Usage = D3D11_USAGE_DEFAULT;
        instDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = g_device->CreateBuffer(&instDesc, nullptr, &g_geomBufferInst);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC visDesc = {};
        visDesc.ByteWidth = sizeof(VisibleIndexData) * kMaxInstances;
        visDesc.Usage = D3D11_USAGE_DYNAMIC;
        visDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        visDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = g_device->CreateBuffer(&visDesc, nullptr, &g_visibleIndicesBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_BUFFER_DESC ppDesc = {};
        ppDesc.ByteWidth = sizeof(PostProcessParams);
        ppDesc.Usage = D3D11_USAGE_DEFAULT;
        ppDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(g_device->CreateBuffer(&ppDesc, nullptr, &g_postProcessParamsBuffer));
    }

    void UploadSkyGeomConstants()
    {
        if (g_deviceContext == nullptr || g_skyGeomBuffer == nullptr)
        {
            return;
        }

        SkyGeomBuffer skyGeom = {};
        DirectX::XMStoreFloat4x4(&skyGeom.model, DirectX::XMMatrixIdentity());
        skyGeom.size = DirectX::XMFLOAT4(100.0f, 0.0f, 0.0f, 0.0f);
        g_deviceContext->UpdateSubresource(g_skyGeomBuffer, 0, nullptr, &skyGeom, 0, 0);
    }

    void InitializeInstances()
    {
        g_instancesData.clear();
        g_instancesData.reserve(kDefaultInstanceCount);
        for (UINT i = 0; i < kDefaultInstanceCount; ++i)
        {
            const float x = (static_cast<float>(i) - (static_cast<float>(kDefaultInstanceCount) - 1.0f) * 0.5f) * 1.4f;
            const float z = ((i % 2u) == 0u) ? -0.9f : 0.9f;
            const DirectX::XMMATRIX model = DirectX::XMMatrixTranslation(x, 0.0f, z);
            const DirectX::XMMATRIX normal = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, model));

            InstanceCpuData data = {};
            DirectX::XMStoreFloat4x4(&data.modelCpu, model);
            DirectX::XMStoreFloat4x4(&data.gpu.model, DirectX::XMMatrixTranspose(model));
            DirectX::XMStoreFloat4x4(&data.gpu.normalMatrix, DirectX::XMMatrixTranspose(normal));
            const float textureId = static_cast<float>(i % 2); // 0=brick, 1=leather
            const float useNormalMap = 1.0f;
            data.gpu.params = DirectX::XMFLOAT4(16.0f + static_cast<float>(i % 8), 0.0f, textureId, useNormalMap);
            g_instancesData.push_back(data);
        }
    }

    AABB ComputeAABB(const DirectX::XMFLOAT4X4& model)
    {
        static const DirectX::XMFLOAT3 corners[8] =
        {
            { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f },
            { -0.5f, -0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f }
        };

        DirectX::XMFLOAT3 minCorner(FLT_MAX, FLT_MAX, FLT_MAX);
        DirectX::XMFLOAT3 maxCorner(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        const DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&model);
        for (const auto& corner : corners)
        {
            const DirectX::XMVECTOR worldCorner = DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&corner), m);
            DirectX::XMFLOAT3 wc = {};
            DirectX::XMStoreFloat3(&wc, worldCorner);
            minCorner.x = (std::min)(minCorner.x, wc.x);
            minCorner.y = (std::min)(minCorner.y, wc.y);
            minCorner.z = (std::min)(minCorner.z, wc.z);
            maxCorner.x = (std::max)(maxCorner.x, wc.x);
            maxCorner.y = (std::max)(maxCorner.y, wc.y);
            maxCorner.z = (std::max)(maxCorner.z, wc.z);
        }
        return { minCorner, maxCorner };
    }

    void BuildFrustumPlanes(const DirectX::XMMATRIX& vp, DirectX::XMFLOAT4 planes[6])
    {
        DirectX::XMFLOAT4X4 m = {};
        DirectX::XMStoreFloat4x4(&m, vp);
        planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
        planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
        planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
        planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
        planes[4] = { m._13, m._23, m._33, m._43 };
        planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };

        for (int i = 0; i < 6; ++i)
        {
            const float len = std::sqrt(
                planes[i].x * planes[i].x +
                planes[i].y * planes[i].y +
                planes[i].z * planes[i].z);
            if (len > 1e-6f)
            {
                const float invLen = 1.0f / len;
                planes[i].x *= invLen;
                planes[i].y *= invLen;
                planes[i].z *= invLen;
                planes[i].w *= invLen;
            }
        }
    }

    bool IsAABBInsideFrustum(const DirectX::XMFLOAT4 planes[6], const AABB& aabb)
    {
        for (int i = 0; i < 6; ++i)
        {
            const DirectX::XMFLOAT4& pl = planes[i];
            DirectX::XMFLOAT3 p = {};
            p.x = (pl.x >= 0.0f) ? aabb.max.x : aabb.min.x;
            p.y = (pl.y >= 0.0f) ? aabb.max.y : aabb.min.y;
            p.z = (pl.z >= 0.0f) ? aabb.max.z : aabb.min.z;
            const float dist = pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w;
            if (dist < kFrustumCullInset)
            {
                return false;
            }
        }
        return true;
    }

    bool CreateOffscreenResources(UINT width, UINT height)
    {
        SafeRelease(g_offscreenSRV);
        SafeRelease(g_offscreenRTV);
        SafeRelease(g_offscreenTexture);

        if (g_device == nullptr || width == 0 || height == 0)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &g_offscreenTexture);
        if (FAILED(hr))
        {
            return false;
        }
        hr = g_device->CreateRenderTargetView(g_offscreenTexture, nullptr, &g_offscreenRTV);
        if (FAILED(hr))
        {
            return false;
        }
        return SUCCEEDED(g_device->CreateShaderResourceView(g_offscreenTexture, nullptr, &g_offscreenSRV));
    }

    bool CreateShadersAndInputLayout()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        ID3DBlob* vertexShaderBlob = nullptr;
        ID3DBlob* pixelShaderBlob = nullptr;

        if (!CompileShaderFromFile(L"Triangle.vs", "vs_5_0", &vertexShaderBlob))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        HRESULT hr = g_device->CreateVertexShader(
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            nullptr,
            &g_vertexShader);
        if (FAILED(hr))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        if (!CompileShaderFromFile(L"Triangle.ps", "ps_5_0", &pixelShaderBlob))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        hr = g_device->CreatePixelShader(
            pixelShaderBlob->GetBufferPointer(),
            pixelShaderBlob->GetBufferSize(),
            nullptr,
            &g_pixelShader);
        if (FAILED(hr))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC inputElements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = g_device->CreateInputLayout(
            inputElements,
            ARRAYSIZE(inputElements),
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            &g_inputLayout);

        SafeRelease(vertexShaderBlob);
        SafeRelease(pixelShaderBlob);

        if (!SUCCEEDED(hr))
        {
            return false;
        }

        ID3DBlob* transparentPixelBlob = nullptr;
        if (!CompileShaderFromFile(L"Transparent.ps", "ps_5_0", &transparentPixelBlob))
        {
            SafeRelease(transparentPixelBlob);
            return false;
        }

        hr = g_device->CreatePixelShader(
            transparentPixelBlob->GetBufferPointer(),
            transparentPixelBlob->GetBufferSize(),
            nullptr,
            &g_transparentPixelShader);
        SafeRelease(transparentPixelBlob);

        return SUCCEEDED(hr);
    }

    bool CreateSkyboxShadersAndInputLayout()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        ID3DBlob* vertexShaderBlob = nullptr;
        ID3DBlob* pixelShaderBlob = nullptr;

        if (!CompileShaderFromFile(L"Skybox.vs", "vs_5_0", &vertexShaderBlob))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        HRESULT hr = g_device->CreateVertexShader(
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            nullptr,
            &g_skyVertexShader);
        if (FAILED(hr))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        if (!CompileShaderFromFile(L"Skybox.ps", "ps_5_0", &pixelShaderBlob))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        hr = g_device->CreatePixelShader(
            pixelShaderBlob->GetBufferPointer(),
            pixelShaderBlob->GetBufferSize(),
            nullptr,
            &g_skyPixelShader);
        if (FAILED(hr))
        {
            SafeRelease(vertexShaderBlob);
            SafeRelease(pixelShaderBlob);
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC skyInputElements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = g_device->CreateInputLayout(
            skyInputElements,
            ARRAYSIZE(skyInputElements),
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            &g_skyInputLayout);

        SafeRelease(vertexShaderBlob);
        SafeRelease(pixelShaderBlob);

        return SUCCEEDED(hr);
    }

    bool CreatePostProcessShaders()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        if (!CompileShaderFromFile(L"PostProcess.vs", "vs_5_0", &vsBlob))
        {
            return false;
        }
        HRESULT hr = g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_postProcessVS);
        if (FAILED(hr))
        {
            SafeRelease(vsBlob);
            return false;
        }

        if (!CompileShaderFromFile(L"PostProcess.ps", "ps_5_0", &psBlob))
        {
            SafeRelease(vsBlob);
            return false;
        }
        hr = g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_postProcessPS);
        SafeRelease(vsBlob);
        SafeRelease(psBlob);
        return SUCCEEDED(hr);
    }

    bool CreateDiffuseTextureArray(const std::wstring& baseDir, const std::wstring& cwd)
    {
        auto TryLoadDds = [](ID3D11Device* device, ID3D11DeviceContext* context, const std::vector<std::wstring>& paths, ID3D11Resource** outRes, ID3D11ShaderResourceView** outSrv) -> bool
        {
            for (const std::wstring& path : paths)
            {
                if (!FileExistsW(path))
                {
                    continue;
                }
                if (SUCCEEDED(DirectX::CreateDDSTextureFromFile(device, context, path.c_str(), outRes, outSrv, 0, nullptr)))
                {
                    return true;
                }
                SafeRelease(*outSrv);
                SafeRelease(*outRes);
            }
            return false;
        };

        ID3D11Resource* tex0Res = nullptr;
        ID3D11ShaderResourceView* tex0Srv = nullptr;
        ID3D11Resource* tex1Res = nullptr;
        ID3D11ShaderResourceView* tex1Srv = nullptr;

        const std::vector<std::wstring> tex0Paths =
        {
            baseDir + L"brick_textures\\Bricks076C_4K-PNG_Color.dds",
            cwd + L"brick_textures\\Bricks076C_4K-PNG_Color.dds",
            baseDir + L"..\\..\\brick_textures\\Bricks076C_4K-PNG_Color.dds",
            baseDir + L"..\\..\\..\\brick_textures\\Bricks076C_4K-PNG_Color.dds",
        };
        const std::vector<std::wstring> tex1Paths =
        {
            baseDir + L"leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
            cwd + L"leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
            baseDir + L"..\\..\\leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
            baseDir + L"..\\..\\..\\leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
        };

        if (!TryLoadDds(g_device, g_deviceContext, tex0Paths, &tex0Res, &tex0Srv))
        {
            if (g_cubeTextureResource != nullptr)
            {
                g_cubeTextureResource->AddRef();
                tex0Res = g_cubeTextureResource;
            }
            else
            {
                return false;
            }
        }
        const bool loadedSecond = TryLoadDds(g_device, g_deviceContext, tex1Paths, &tex1Res, &tex1Srv);

        ID3D11Texture2D* tex0 = nullptr;
        ID3D11Texture2D* tex1 = nullptr;
        if (FAILED(tex0Res->QueryInterface(IID_PPV_ARGS(&tex0))))
        {
            SafeRelease(tex0Srv); SafeRelease(tex0Res); SafeRelease(tex1Srv); SafeRelease(tex1Res);
            return false;
        }
        if (!loadedSecond || tex1Res == nullptr || FAILED(tex1Res->QueryInterface(IID_PPV_ARGS(&tex1))))
        {
            tex1 = tex0;
            tex1->AddRef();
        }

        D3D11_TEXTURE2D_DESC d0 = {};
        D3D11_TEXTURE2D_DESC d1 = {};
        tex0->GetDesc(&d0);
        tex1->GetDesc(&d1);
        if (d0.Width != d1.Width || d0.Height != d1.Height || d0.Format != d1.Format || d0.MipLevels != d1.MipLevels)
        {
            SafeRelease(tex1);
            tex1 = tex0;
            tex1->AddRef();
        }

        D3D11_TEXTURE2D_DESC arrDesc = d0;
        arrDesc.ArraySize = 2;
        arrDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        arrDesc.Usage = D3D11_USAGE_DEFAULT;
        arrDesc.CPUAccessFlags = 0;
        arrDesc.MiscFlags = 0;
        HRESULT hr = g_device->CreateTexture2D(&arrDesc, nullptr, &g_textureArray);
        if (FAILED(hr))
        {
            SafeRelease(tex0); SafeRelease(tex1); SafeRelease(tex0Srv); SafeRelease(tex0Res); SafeRelease(tex1Srv); SafeRelease(tex1Res);
            return false;
        }

        for (UINT mip = 0; mip < arrDesc.MipLevels; ++mip)
        {
            g_deviceContext->CopySubresourceRegion(g_textureArray, D3D11CalcSubresource(mip, 0, arrDesc.MipLevels), 0, 0, 0, tex0, mip, nullptr);
            g_deviceContext->CopySubresourceRegion(g_textureArray, D3D11CalcSubresource(mip, 1, arrDesc.MipLevels), 0, 0, 0, tex1, mip, nullptr);
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = arrDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.ArraySize = 2;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.MipLevels = arrDesc.MipLevels;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        hr = g_device->CreateShaderResourceView(g_textureArray, &srvDesc, &g_textureArraySRV);

        SafeRelease(tex0); SafeRelease(tex1);
        SafeRelease(tex0Srv); SafeRelease(tex0Res);
        SafeRelease(tex1Srv); SafeRelease(tex1Res);
        return SUCCEEDED(hr);
    }

    bool CreateSamplerState()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.MaxAnisotropy = 16;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

        return SUCCEEDED(g_device->CreateSamplerState(&sampDesc, &g_samplerState));
    }

    bool LoadDdsTextures()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        const std::wstring cwd = GetCurrentWorkingDirectoryW();
        const std::wstring dir = GetExecutableDirectory();
        const std::wstring cubePaths[] =
        {
            dir + L"brick_textures\\Bricks076C_4K-PNG_Color.dds",
            cwd + L"brick_textures\\Bricks076C_4K-PNG_Color.dds",
            dir + L"..\\..\\brick_textures\\Bricks076C_4K-PNG_Color.dds",
            dir + L"..\\..\\..\\brick_textures\\Bricks076C_4K-PNG_Color.dds",
            dir + L"leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
            cwd + L"leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
            dir + L"..\\..\\leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
            dir + L"..\\..\\..\\leather_texture_ball\\Leather034A_1K-JPG_Color.dds",
            dir + L"Texture.dds",
            dir + L"textures\\Texture.dds",
            cwd + L"Texture.dds",
            cwd + L"textures\\Texture.dds",
            dir + L"..\\..\\textures\\Texture.dds",
            dir + L"..\\..\\..\\textures\\Texture.dds",
        };
        const std::wstring normalMapPaths[] =
        {
            dir + L"brick_textures\\Bricks076C_4K-PNG_NormalDX.dds",
            cwd + L"brick_textures\\Bricks076C_4K-PNG_NormalDX.dds",
            dir + L"..\\..\\brick_textures\\Bricks076C_4K-PNG_NormalDX.dds",
            dir + L"..\\..\\..\\brick_textures\\Bricks076C_4K-PNG_NormalDX.dds",
            dir + L"leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
            cwd + L"leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
            dir + L"..\\..\\leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
            dir + L"..\\..\\..\\leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
            dir + L"brick_normal.dds",
            dir + L"textures\\brick_normal.dds",
            cwd + L"brick_normal.dds",
            cwd + L"textures\\brick_normal.dds",
            dir + L"..\\..\\textures\\brick_normal.dds",
            dir + L"..\\..\\..\\textures\\brick_normal.dds",
        };
        const std::wstring leatherNormalMapPaths[] =
        {
            dir + L"leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
            cwd + L"leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
            dir + L"..\\..\\leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
            dir + L"..\\..\\..\\leather_texture_ball\\Leather034A_1K-JPG_NormalDX.dds",
        };
        const std::wstring skyPaths[] =
        {
            dir + L"textures\\IndoorEnvironmentHDRI013_4K_TONEMAPPED.dds",
            cwd + L"textures\\IndoorEnvironmentHDRI013_4K_TONEMAPPED.dds",
            dir + L"..\\..\\textures\\IndoorEnvironmentHDRI013_4K_TONEMAPPED.dds",
            dir + L"..\\..\\..\\textures\\IndoorEnvironmentHDRI013_4K_TONEMAPPED.dds",
            dir + L"SkyboxPhotoCubemap_Uffizi.dds",
            dir + L"textures\\SkyboxPhotoCubemap.dds",
            cwd + L"SkyboxPhotoCubemap.dds",
            cwd + L"textures\\SkyboxPhotoCubemap.dds",
            dir + L"..\\..\\textures\\SkyboxPhotoCubemap.dds",
            dir + L"..\\..\\..\\textures\\SkyboxPhotoCubemap.dds",
            dir + L"SkyboxCubemap.dds",
            dir + L"textures\\SkyboxCubemap.dds",
            cwd + L"SkyboxCubemap.dds",
            cwd + L"textures\\SkyboxCubemap.dds",
            dir + L"..\\..\\textures\\SkyboxCubemap.dds",
            dir + L"..\\..\\..\\textures\\SkyboxCubemap.dds",
        };

        HRESULT hr = E_FAIL;
        std::wstring cubeSource;
        for (const std::wstring& path : cubePaths)
        {
            if (!FileExistsW(path))
            {
                continue;
            }

            hr = DirectX::CreateDDSTextureFromFile(
                g_device,
                g_deviceContext,
                path.c_str(),
                &g_cubeTextureResource,
                &g_cubeTextureSRV,
                0,
                nullptr);
            if (SUCCEEDED(hr))
            {
                cubeSource = L"file: " + path;
                break;
            }
            SafeRelease(g_cubeTextureSRV);
            SafeRelease(g_cubeTextureResource);
        }

        if (FAILED(hr))
        {
            hr = DirectX::CreateDDSTextureFromMemory(
                g_device,
                g_deviceContext,
                EmbeddedDds::kTexture2DDds,
                EmbeddedDds::kTexture2DDdsSize,
                &g_cubeTextureResource,
                &g_cubeTextureSRV,
                0,
                nullptr);
            if (SUCCEEDED(hr))
            {
                cubeSource = L"embedded DDS";
            }
        }

        if (FAILED(hr))
        {
            if (!CreateFallbackCubeTexture2D(g_device, &g_cubeTextureResource, &g_cubeTextureSRV))
            {
                wchar_t msg[384] = {};
                swprintf_s(
                    msg,
                    ARRAYSIZE(msg),
                    L"Cube texture: DDS and procedural fallback failed (last HRESULT 0x%08X).",
                    static_cast<unsigned int>(hr));
                MessageBoxW(g_hWnd, msg, szTitle, MB_ICONERROR | MB_OK);
                return false;
            }

            cubeSource = L"procedural fallback 2D (4x4 checker)";
        }

        hr = E_FAIL;
        std::wstring normalMapSource;
        for (const std::wstring& path : normalMapPaths)
        {
            if (!FileExistsW(path))
            {
                continue;
            }

            hr = DirectX::CreateDDSTextureFromFile(
                g_device,
                g_deviceContext,
                path.c_str(),
                &g_normalMapResource,
                &g_normalMapSRV,
                0,
                nullptr);
            if (SUCCEEDED(hr))
            {
                normalMapSource = L"file: " + path;
                break;
            }
            SafeRelease(g_normalMapSRV);
            SafeRelease(g_normalMapResource);
        }

        if (FAILED(hr))
        {
            if (!CreateFallbackNormalTexture2D(g_device, &g_normalMapResource, &g_normalMapSRV))
            {
                wchar_t msg[384] = {};
                swprintf_s(
                    msg,
                    ARRAYSIZE(msg),
                    L"Normal map: DDS and flat fallback failed (last HRESULT 0x%08X).",
                    static_cast<unsigned int>(hr));
                MessageBoxW(g_hWnd, msg, szTitle, MB_ICONERROR | MB_OK);
                return false;
            }

            normalMapSource = L"procedural fallback normal map (flat +Z)";
        }

        hr = E_FAIL;
        std::wstring leatherNormalMapSource;
        for (const std::wstring& path : leatherNormalMapPaths)
        {
            if (!FileExistsW(path))
            {
                continue;
            }

            hr = DirectX::CreateDDSTextureFromFile(
                g_device,
                g_deviceContext,
                path.c_str(),
                &g_leatherNormalMapResource,
                &g_leatherNormalMapSRV,
                0,
                nullptr);
            if (SUCCEEDED(hr))
            {
                leatherNormalMapSource = L"file: " + path;
                break;
            }
            SafeRelease(g_leatherNormalMapSRV);
            SafeRelease(g_leatherNormalMapResource);
        }

        if (FAILED(hr))
        {
            if (!CreateFallbackNormalTexture2D(g_device, &g_leatherNormalMapResource, &g_leatherNormalMapSRV))
            {
                wchar_t msg[384] = {};
                swprintf_s(
                    msg,
                    ARRAYSIZE(msg),
                    L"Leather normal map: DDS and flat fallback failed (last HRESULT 0x%08X).",
                    static_cast<unsigned int>(hr));
                MessageBoxW(g_hWnd, msg, szTitle, MB_ICONERROR | MB_OK);
                return false;
            }

            leatherNormalMapSource = L"procedural fallback normal map (flat +Z)";
        }

        hr = E_FAIL;
        std::wstring skySource;
        for (const std::wstring& path : skyPaths)
        {
            if (!FileExistsW(path))
            {
                continue;
            }

            hr = DirectX::CreateDDSTextureFromFile(
                g_device,
                g_deviceContext,
                path.c_str(),
                &g_skyboxCubemapResource,
                &g_skyboxCubemapSRV,
                0,
                nullptr);
            if (SUCCEEDED(hr))
            {
                skySource = L"file: " + path;
                break;
            }
            SafeRelease(g_skyboxCubemapSRV);
            SafeRelease(g_skyboxCubemapResource);
        }

        if (FAILED(hr))
        {
            hr = DirectX::CreateDDSTextureFromMemory(
                g_device,
                g_deviceContext,
                EmbeddedDds::kSkyboxCubemapDds,
                EmbeddedDds::kSkyboxCubemapDdsSize,
                &g_skyboxCubemapResource,
                &g_skyboxCubemapSRV,
                0,
                nullptr);
            if (SUCCEEDED(hr))
            {
                skySource = L"embedded DDS";
            }
        }

        if (FAILED(hr))
        {
            if (!CreateFallbackCubemapTexture(g_device, &g_skyboxCubemapResource, &g_skyboxCubemapSRV))
            {
                wchar_t msg[384] = {};
                swprintf_s(
                    msg,
                    ARRAYSIZE(msg),
                    L"Skybox cubemap: DDS and procedural fallback failed (last HRESULT 0x%08X).",
                    static_cast<unsigned int>(hr));
                MessageBoxW(g_hWnd, msg, szTitle, MB_ICONERROR | MB_OK);
                return false;
            }

            skySource = L"procedural fallback cubemap (64x64 patterned faces)";
        }

        LogTextureSource(L"Cube texture", cubeSource);
        LogTextureSource(L"Normal map", normalMapSource);
        LogTextureSource(L"Leather normal map", leatherNormalMapSource);
        LogTextureSource(L"Skybox cubemap", skySource);
        return true;
    }

    bool CreateDepthStencilStates()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC opaqueDesc = {};
        opaqueDesc.DepthEnable = TRUE;
        opaqueDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        opaqueDesc.DepthFunc = D3D11_COMPARISON_GREATER;
        opaqueDesc.StencilEnable = FALSE;

        HRESULT hr = g_device->CreateDepthStencilState(&opaqueDesc, &g_depthStencilStateOpaque);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC skyDesc = {};
        skyDesc.DepthEnable = TRUE;
        skyDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        skyDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        skyDesc.StencilEnable = FALSE;
        hr = g_device->CreateDepthStencilState(&skyDesc, &g_depthStencilStateSkybox);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC transparentDesc = {};
        transparentDesc.DepthEnable = TRUE;
        transparentDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        transparentDesc.DepthFunc = D3D11_COMPARISON_GREATER;
        transparentDesc.StencilEnable = FALSE;
        return SUCCEEDED(g_device->CreateDepthStencilState(&transparentDesc, &g_depthStencilStateTransparent));
    }

    bool CreateTransparentBlendState()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        return SUCCEEDED(g_device->CreateBlendState(&blendDesc, &g_transparentBlendState));
    }

    bool CreateRasterizerState()
    {
        if (g_device == nullptr)
        {
            return false;
        }

        D3D11_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_BACK;
        rasterizerDesc.FrontCounterClockwise = FALSE;
        rasterizerDesc.DepthClipEnable = TRUE;

        HRESULT hr = g_device->CreateRasterizerState(&rasterizerDesc, &g_rasterizerState);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_RASTERIZER_DESC skyRasterDesc = {};
        skyRasterDesc.FillMode = D3D11_FILL_SOLID;
        skyRasterDesc.CullMode = D3D11_CULL_FRONT;
        skyRasterDesc.FrontCounterClockwise = FALSE;
        skyRasterDesc.DepthClipEnable = TRUE;
        hr = g_device->CreateRasterizerState(&skyRasterDesc, &g_skyRasterizerState);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_RASTERIZER_DESC postDesc = {};
        postDesc.FillMode = D3D11_FILL_SOLID;
        postDesc.CullMode = D3D11_CULL_NONE;
        postDesc.FrontCounterClockwise = FALSE;
        postDesc.DepthClipEnable = FALSE;
        return SUCCEEDED(g_device->CreateRasterizerState(&postDesc, &g_postProcessRasterizerState));
    }

    void ReleaseSceneResources()
    {
        SafeRelease(g_postProcessParamsBuffer);
        SafeRelease(g_visibleIndicesBuffer);
        SafeRelease(g_geomBufferInst);
        SafeRelease(g_textureArraySRV);
        SafeRelease(g_textureArray);
        SafeRelease(g_postProcessPS);
        SafeRelease(g_postProcessVS);
        SafeRelease(g_transparentColorBuffer);
        SafeRelease(g_transparentBlendState);
        SafeRelease(g_depthStencilStateTransparent);
        SafeRelease(g_samplerState);
        SafeRelease(g_cubeTextureSRV);
        SafeRelease(g_cubeTextureResource);
        SafeRelease(g_normalMapSRV);
        SafeRelease(g_normalMapResource);
        SafeRelease(g_leatherNormalMapSRV);
        SafeRelease(g_leatherNormalMapResource);
        SafeRelease(g_skyboxCubemapSRV);
        SafeRelease(g_skyboxCubemapResource);
        SafeRelease(g_depthStencilStateSkybox);
        SafeRelease(g_depthStencilStateOpaque);
        SafeRelease(g_postProcessRasterizerState);
        SafeRelease(g_skyRasterizerState);
        SafeRelease(g_rasterizerState);
        SafeRelease(g_skyInputLayout);
        SafeRelease(g_inputLayout);
        SafeRelease(g_skyPixelShader);
        SafeRelease(g_skyVertexShader);
        SafeRelease(g_transparentPixelShader);
        SafeRelease(g_pixelShader);
        SafeRelease(g_vertexShader);
        SafeRelease(g_skyGeomBuffer);
        SafeRelease(g_skySceneBuffer);
        SafeRelease(g_sceneBuffer);
        SafeRelease(g_modelBuffer);
        SafeRelease(g_indexBuffer);
        SafeRelease(g_skyVertexBuffer);
        SafeRelease(g_vertexBuffer);
    }

    IDXGIAdapter1* SelectHardwareAdapter()
    {
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        {
            return nullptr;
        }

        IDXGIAdapter1* selectedAdapter = nullptr;
        for (UINT adapterIndex = 0; ; ++adapterIndex)
        {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 desc = {};
            adapter->GetDesc1(&desc);

            const bool isSoftwareAdapter = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            const bool isBasicRenderDriver =
                wcscmp(desc.Description, L"Microsoft Basic Render Driver") == 0;

            if (!isSoftwareAdapter && !isBasicRenderDriver)
            {
                selectedAdapter = adapter;
                break;
            }

            SafeRelease(adapter);
        }

        SafeRelease(factory);
        return selectedAdapter;
    }

    void ReleaseBackBufferResources()
    {
        if (g_deviceContext != nullptr)
        {
            g_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        }

        SafeRelease(g_depthStencilView);
        SafeRelease(g_depthStencilTexture);
        SafeRelease(g_offscreenSRV);
        SafeRelease(g_offscreenRTV);
        SafeRelease(g_offscreenTexture);
        SafeRelease(g_backBufferRTV);
    }

    bool CreateBackBufferResources()
    {
        if (g_device == nullptr || g_swapChain == nullptr)
        {
            return false;
        }

        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            return false;
        }

        hr = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_backBufferRTV);
        SafeRelease(backBuffer);
        if (FAILED(hr))
        {
            return false;
        }

        RECT clientRect = {};
        GetClientRect(g_hWnd, &clientRect);

        const UINT width = static_cast<UINT>(clientRect.right - clientRect.left);
        const UINT height = static_cast<UINT>(clientRect.bottom - clientRect.top);
        if (width == 0 || height == 0)
        {
            return false;
        }

        g_viewport.TopLeftX = 0.0f;
        g_viewport.TopLeftY = 0.0f;
        g_viewport.Width = static_cast<FLOAT>(width);
        g_viewport.Height = static_cast<FLOAT>(height);
        g_viewport.MinDepth = 0.0f;
        g_viewport.MaxDepth = 1.0f;

        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        hr = g_device->CreateTexture2D(&depthDesc, nullptr, &g_depthStencilTexture);
        if (FAILED(hr))
        {
            return false;
        }

        hr = g_device->CreateDepthStencilView(g_depthStencilTexture, nullptr, &g_depthStencilView);
        if (FAILED(hr))
        {
            return false;
        }

        return CreateOffscreenResources(width, height);
    }

    bool ResizeBackBuffer(UINT width, UINT height)
    {
        if (g_swapChain == nullptr || width == 0 || height == 0)
        {
            return true;
        }

        ReleaseBackBufferResources();

        HRESULT hr = g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            return false;
        }

        return CreateBackBufferResources();
    }

    void UpdateCamera(float deltaSeconds)
    {
        const float angleStep = kCameraRotateSpeed * deltaSeconds;
        const float zoomStep = kCameraZoomSpeed * deltaSeconds;

        if ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0)
        {
            g_cameraYaw -= angleStep;
        }
        if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0)
        {
            g_cameraYaw += angleStep;
        }
        if ((GetAsyncKeyState(VK_UP) & 0x8000) != 0)
        {
            g_cameraDistance -= zoomStep;
        }
        if ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0)
        {
            g_cameraDistance += zoomStep;
        }

        g_cameraYaw = std::fmod(g_cameraYaw, DirectX::XM_2PI);
        g_cameraPitch = std::fmod(g_cameraPitch, DirectX::XM_2PI);
        g_cameraDistance = ClampValue(g_cameraDistance, 2.0f, 10.0f);
    }

    bool UpdateConstantBuffers(float elapsedSeconds)
    {
        if (g_deviceContext == nullptr || g_modelBuffer == nullptr || g_sceneBuffer == nullptr
            || g_skySceneBuffer == nullptr)
        {
            return false;
        }

        const float aspectRatio = (g_viewport.Height > 0.0f) ? (g_viewport.Width / g_viewport.Height) : 1.0f;
        const DirectX::XMMATRIX cameraRotation = DirectX::XMMatrixRotationRollPitchYaw(g_cameraPitch, g_cameraYaw, 0.0f);
        const DirectX::XMVECTOR eye = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(0.0f, 0.0f, g_cameraDistance, 1.0f),
            cameraRotation);
        const DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(
            DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
            cameraRotation);
        const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
            eye,
            DirectX::XMVectorZero(),
            up);
        const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(kVerticalFovDegrees),
            aspectRatio,
            kNearPlane,
            kFarPlane);

        const DirectX::XMMATRIX vp = DirectX::XMMatrixMultiply(view, projection);
        const DirectX::XMMATRIX vpT = DirectX::XMMatrixTranspose(vp);

        SceneBuffer sceneBuffer = {};
        DirectX::XMStoreFloat4x4(&sceneBuffer.vp, vpT);
        DirectX::XMStoreFloat4(&sceneBuffer.cameraPos, eye);
        sceneBuffer.ambientColor = DirectX::XMFLOAT4(0.09f, 0.09f, 0.12f, 1.0f);
        sceneBuffer.lightCount = DirectX::XMINT4(1, 0, 0, 0);
        // One broad, soft light: slightly above cubes and farther to the right.
        sceneBuffer.lights[0].pos = DirectX::XMFLOAT4(-3.8f, 3.8f, 0.6f, 0.0f);
        sceneBuffer.lights[0].color = DirectX::XMFLOAT4(18.4f, 16.8f, 16.4f, 1.0f);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = g_deviceContext->Map(g_sceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            return false;
        }

        std::memcpy(mapped.pData, &sceneBuffer, sizeof(sceneBuffer));
        g_deviceContext->Unmap(g_sceneBuffer, 0);

        if (g_geomBufferInst != nullptr && g_visibleIndicesBuffer != nullptr)
        {
            for (UINT i = 0; i < g_instancesData.size(); ++i)
            {
                const float x = (static_cast<float>(i) - (static_cast<float>(kDefaultInstanceCount) - 1.0f) * 0.5f) * 1.4f;
                const float z = ((i % 2u) == 0u) ? -0.9f : 0.9f;
                const DirectX::XMMATRIX model =
                    DirectX::XMMatrixRotationY(elapsedSeconds * (0.15f + 0.02f * static_cast<float>(i % 5))) *
                    DirectX::XMMatrixTranslation(x, 0.0f, z);
                const DirectX::XMMATRIX normal = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, model));
                DirectX::XMStoreFloat4x4(&g_instancesData[i].modelCpu, model);
                DirectX::XMStoreFloat4x4(&g_instancesData[i].gpu.model, DirectX::XMMatrixTranspose(model));
                DirectX::XMStoreFloat4x4(&g_instancesData[i].gpu.normalMatrix, DirectX::XMMatrixTranspose(normal));
            }

            std::vector<GeomBufferGpuData> gpuInstanceData(kMaxInstances);
            for (UINT i = 0; i < static_cast<UINT>(g_instancesData.size()) && i < kMaxInstances; ++i)
            {
                gpuInstanceData[i] = g_instancesData[i].gpu;
            }
            g_deviceContext->UpdateSubresource(g_geomBufferInst, 0, nullptr, gpuInstanceData.data(), 0, 0);

            DirectX::XMFLOAT4 frustumPlanes[6] = {};
            BuildFrustumPlanes(vp, frustumPlanes);
            g_visibleIndices.clear();
            g_visibleIndexData.clear();
            for (UINT i = 0; i < g_instancesData.size(); ++i)
            {
                const AABB aabb = ComputeAABB(g_instancesData[i].modelCpu);
                if (IsAABBInsideFrustum(frustumPlanes, aabb))
                {
                    g_visibleIndices.push_back(i);
                }
            }

            g_visibleIndexData.resize((std::max)(g_visibleIndices.size(), static_cast<size_t>(1)));
            for (size_t i = 0; i < g_visibleIndexData.size(); ++i)
            {
                const UINT visibleId = (i < g_visibleIndices.size()) ? g_visibleIndices[i] : 0u;
                g_visibleIndexData[i].packed = DirectX::XMUINT4(visibleId, 0, 0, 0);
            }

            D3D11_MAPPED_SUBRESOURCE visMapped = {};
            if (SUCCEEDED(g_deviceContext->Map(g_visibleIndicesBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &visMapped)))
            {
                std::memcpy(visMapped.pData, g_visibleIndexData.data(), sizeof(VisibleIndexData) * g_visibleIndexData.size());
                g_deviceContext->Unmap(g_visibleIndicesBuffer, 0);
            }
        }

        SkySceneBuffer skyScene = {};
        skyScene.vp = sceneBuffer.vp;
        DirectX::XMStoreFloat4(&skyScene.cameraPos, eye);
        DirectX::XMStoreFloat3(&g_cameraWorldPosition, eye);

        hr = g_deviceContext->Map(g_skySceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            return false;
        }

        std::memcpy(mapped.pData, &skyScene, sizeof(skyScene));
        g_deviceContext->Unmap(g_skySceneBuffer, 0);
        return true;
    }

    void UpdateModelBuffer(const DirectX::XMMATRIX& modelMatrix, float shininess = 32.0f)
    {
        if (g_deviceContext == nullptr || g_modelBuffer == nullptr)
        {
            return;
        }

        ModelBuffer modelBuffer = {};
        DirectX::XMStoreFloat4x4(&modelBuffer.model, DirectX::XMMatrixTranspose(modelMatrix));
        modelBuffer.material = DirectX::XMFLOAT4(shininess, 0.0f, 0.0f, 0.0f);
        g_deviceContext->UpdateSubresource(g_modelBuffer, 0, nullptr, &modelBuffer, 0, 0);
    }

    void UpdateTransparentColorBuffer(const DirectX::XMFLOAT4& color)
    {
        if (g_deviceContext == nullptr || g_transparentColorBuffer == nullptr)
        {
            return;
        }

        TransparentColorBuffer cb = {};
        cb.color = color;
        g_deviceContext->UpdateSubresource(g_transparentColorBuffer, 0, nullptr, &cb, 0, 0);
    }

    bool InitializeDirect3D(HWND hWnd)
    {
        IDXGIAdapter1* adapter = SelectHardwareAdapter();

        UINT deviceFlags = 0;
#ifdef _DEBUG
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        const D3D_FEATURE_LEVEL requestedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

        HRESULT hr = D3D11CreateDevice(
            adapter,
            adapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            deviceFlags,
            requestedFeatureLevels,
            ARRAYSIZE(requestedFeatureLevels),
            D3D11_SDK_VERSION,
            &g_device,
            nullptr,
            &g_deviceContext);

#ifdef _DEBUG
        if (FAILED(hr) && hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            hr = D3D11CreateDevice(
                adapter,
                adapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                0,
                requestedFeatureLevels,
                ARRAYSIZE(requestedFeatureLevels),
                D3D11_SDK_VERSION,
                &g_device,
                nullptr,
                &g_deviceContext);
        }
#endif

        SafeRelease(adapter);

        if (FAILED(hr))
        {
            return false;
        }

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* dxgiAdapter = nullptr;
        IDXGIFactory2* factory = nullptr;

        hr = g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        if (FAILED(hr))
        {
            SafeRelease(dxgiDevice);
            SafeRelease(dxgiAdapter);
            SafeRelease(factory);
            return false;
        }

        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        if (FAILED(hr))
        {
            SafeRelease(dxgiDevice);
            SafeRelease(dxgiAdapter);
            SafeRelease(factory);
            return false;
        }

        hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            SafeRelease(dxgiDevice);
            SafeRelease(dxgiAdapter);
            SafeRelease(factory);
            return false;
        }

        RECT clientRect = {};
        GetClientRect(hWnd, &clientRect);

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = clientRect.right - clientRect.left;
        swapChainDesc.Height = clientRect.bottom - clientRect.top;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.Stereo = FALSE;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Flags = 0;

        hr = factory->CreateSwapChainForHwnd(
            g_device,
            hWnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &g_swapChain);
        if (SUCCEEDED(hr))
        {
            factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        }

        SafeRelease(dxgiDevice);
        SafeRelease(dxgiAdapter);
        SafeRelease(factory);

        if (FAILED(hr))
        {
            return false;
        }

        if (!CreateBackBufferResources())
        {
            return false;
        }

        if (!CreateCubeGeometry())
        {
            return false;
        }

        if (!CreateConstantBuffers())
        {
            return false;
        }

        UploadSkyGeomConstants();

        if (!LoadDdsTextures())
        {
            return false;
        }

        InitializeInstances();

        if (!CreateShadersAndInputLayout())
        {
            return false;
        }

        if (!CreateSkyboxShadersAndInputLayout())
        {
            return false;
        }

        if (!CreatePostProcessShaders())
        {
            return false;
        }

        if (!CreateSamplerState())
        {
            return false;
        }

        if (!CreateDepthStencilStates())
        {
            return false;
        }

        if (!CreateTransparentBlendState())
        {
            return false;
        }

        const std::wstring cwd = GetCurrentWorkingDirectoryW();
        const std::wstring dir = GetExecutableDirectory();
        if (!CreateDiffuseTextureArray(dir, cwd))
        {
            // Keep rendering possible with single-texture fallback.
            g_textureArraySRV = g_cubeTextureSRV;
            if (g_textureArraySRV != nullptr)
            {
                g_textureArraySRV->AddRef();
            }
        }

        return CreateRasterizerState();
    }

    void CleanupDirect3D()
    {
        ReleaseBackBufferResources();
        ReleaseSceneResources();
        SafeRelease(g_swapChain);

        if (g_deviceContext != nullptr)
        {
            g_deviceContext->ClearState();
            g_deviceContext->Flush();
        }

        SafeRelease(g_deviceContext);
        SafeRelease(g_device);
    }

    void RenderFrame()
    {
        if (g_deviceContext == nullptr
            || g_swapChain == nullptr
            || g_backBufferRTV == nullptr
            || g_depthStencilView == nullptr
            || g_vertexBuffer == nullptr
            || g_skyVertexBuffer == nullptr
            || g_indexBuffer == nullptr
            || g_modelBuffer == nullptr
            || g_sceneBuffer == nullptr
            || g_skySceneBuffer == nullptr
            || g_skyGeomBuffer == nullptr
            || g_vertexShader == nullptr
            || g_pixelShader == nullptr
            || g_skyVertexShader == nullptr
            || g_skyPixelShader == nullptr
            || g_transparentPixelShader == nullptr
            || g_inputLayout == nullptr
            || g_skyInputLayout == nullptr
            || g_rasterizerState == nullptr
            || g_skyRasterizerState == nullptr
            || g_depthStencilStateOpaque == nullptr
            || g_depthStencilStateSkybox == nullptr
            || g_depthStencilStateTransparent == nullptr
            || g_transparentBlendState == nullptr
            || g_cubeTextureSRV == nullptr
            || g_normalMapSRV == nullptr
            || g_leatherNormalMapSRV == nullptr
            || g_skyboxCubemapSRV == nullptr
            || g_samplerState == nullptr
            || g_transparentColorBuffer == nullptr
            || g_geomBufferInst == nullptr
            || g_visibleIndicesBuffer == nullptr)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const float elapsedSeconds = std::chrono::duration<float>(now - g_startTime).count();
        const float deltaSeconds = std::chrono::duration<float>(now - g_previousFrameTime).count();
        g_previousFrameTime = now;

        UpdateCamera(deltaSeconds);
        if (!UpdateConstantBuffers(elapsedSeconds))
        {
            return;
        }

        const bool usePostProcess =
            g_postProcessMode != 1
            &&
            g_offscreenRTV != nullptr
            && g_offscreenSRV != nullptr
            && g_postProcessVS != nullptr
            && g_postProcessPS != nullptr
            && g_postProcessParamsBuffer != nullptr
            && g_postProcessRasterizerState != nullptr;
        ID3D11RenderTargetView* sceneRTV = usePostProcess ? g_offscreenRTV : g_backBufferRTV;
        g_deviceContext->OMSetRenderTargets(1, &sceneRTV, g_depthStencilView);
        g_deviceContext->RSSetViewports(1, &g_viewport);
        g_deviceContext->ClearRenderTargetView(sceneRTV, kBackColor);
        g_deviceContext->ClearDepthStencilView(g_depthStencilView, D3D11_CLEAR_DEPTH, 0.0f, 0);

        g_deviceContext->IASetIndexBuffer(g_indexBuffer, DXGI_FORMAT_R16_UINT, 0);
        g_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Скибокс
        const UINT skyStride = sizeof(SkyVertex);
        const UINT skyOffset = 0;
        ID3D11Buffer* skyVbs[] = { g_skyVertexBuffer };
        ID3D11Buffer* skyCBs[] = { g_skySceneBuffer, g_skyGeomBuffer };
        ID3D11ShaderResourceView* skySrvs[] = { g_skyboxCubemapSRV };

        g_deviceContext->OMSetDepthStencilState(g_depthStencilStateSkybox, 0);
        g_deviceContext->RSSetState(g_skyRasterizerState);
        g_deviceContext->IASetVertexBuffers(0, 1, skyVbs, &skyStride, &skyOffset);
        g_deviceContext->IASetInputLayout(g_skyInputLayout);
        g_deviceContext->VSSetShader(g_skyVertexShader, nullptr, 0);
        g_deviceContext->VSSetConstantBuffers(0, ARRAYSIZE(skyCBs), skyCBs);
        g_deviceContext->PSSetShader(g_skyPixelShader, nullptr, 0);
        g_deviceContext->PSSetShaderResources(0, 1, skySrvs);
        g_deviceContext->PSSetSamplers(0, 1, &g_samplerState);
        g_deviceContext->DrawIndexed(kCubeIndexCount, 0, 0);

        // Непрозрачные кубы
        const UINT stride = sizeof(TextureVertex);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffers[] = { g_vertexBuffer };
        ID3D11Buffer* constantBuffers[] = { g_modelBuffer, g_sceneBuffer, g_geomBufferInst, g_visibleIndicesBuffer };
        ID3D11ShaderResourceView* diffuseSrv = (g_textureArraySRV != nullptr) ? g_textureArraySRV : g_cubeTextureSRV;
        ID3D11ShaderResourceView* cubeSrvs[] = { diffuseSrv, g_normalMapSRV, g_leatherNormalMapSRV };

        g_deviceContext->OMSetDepthStencilState(g_depthStencilStateOpaque, 0);
        g_deviceContext->RSSetState(g_rasterizerState);
        g_deviceContext->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
        g_deviceContext->IASetInputLayout(g_inputLayout);
        g_deviceContext->VSSetShader(g_vertexShader, nullptr, 0);
        g_deviceContext->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
        g_deviceContext->PSSetShader(g_pixelShader, nullptr, 0);
        g_deviceContext->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
        g_deviceContext->PSSetShaderResources(0, 3, cubeSrvs);
        g_deviceContext->PSSetSamplers(0, 1, &g_samplerState);

        // const DirectX::XMMATRIX baseRotation =
        //     DirectX::XMMatrixRotationY(elapsedSeconds * kRotationSpeed) *
        //     DirectX::XMMatrixRotationX(elapsedSeconds * (kRotationSpeed * 0.5f));
        const DirectX::XMMATRIX baseRotation = DirectX::XMMatrixIdentity();

        const UINT visibleCount = static_cast<UINT>((std::min)(g_visibleIndices.size(), static_cast<size_t>(kMaxInstances)));
        g_lastVisibleCount = visibleCount;
        if (g_hWnd != nullptr)
        {
            wchar_t title[128] = {};
            swprintf_s(
                title,
                ARRAYSIZE(title),
                L"hw7 | visible: %u/%u | post: %d",
                g_lastVisibleCount,
                static_cast<unsigned int>(g_instancesData.size()),
                g_postProcessMode);
            SetWindowTextW(g_hWnd, title);
        }
        if (visibleCount > 0)
        {
            g_deviceContext->DrawIndexedInstanced(kCubeIndexCount, visibleCount, 0, 0, 0);
        }

        std::vector<TransparentObject> transparentObjects =
        {
            { DirectX::XMFLOAT3(0.15f, 0.0f, -1.4f), DirectX::XMFLOAT4(1.0f, 0.2f, 0.2f, 0.45f), 0.0f },
            { DirectX::XMFLOAT3(-0.45f, 0.35f, -2.3f), DirectX::XMFLOAT4(0.2f, 0.8f, 1.0f, 0.40f), 0.0f },
        };

        const DirectX::XMVECTOR cameraPos = DirectX::XMLoadFloat3(&g_cameraWorldPosition);
        for (auto& obj : transparentObjects)
        {
            const DirectX::XMVECTOR objPos = DirectX::XMLoadFloat3(&obj.position);
            obj.distanceToCamera = DirectX::XMVectorGetX(
                DirectX::XMVector3Length(DirectX::XMVectorSubtract(cameraPos, objPos)));
        }

        std::sort(
            transparentObjects.begin(),
            transparentObjects.end(),
            [](const TransparentObject& a, const TransparentObject& b) { return a.distanceToCamera > b.distanceToCamera; });

        const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_deviceContext->OMSetBlendState(g_transparentBlendState, blendFactor, 0xFFFFFFFF);
        g_deviceContext->OMSetDepthStencilState(g_depthStencilStateTransparent, 0);
        g_deviceContext->PSSetShader(g_transparentPixelShader, nullptr, 0);
        ID3D11Buffer* transparentColorCBs[] = { g_transparentColorBuffer };
        g_deviceContext->PSSetConstantBuffers(2, 1, transparentColorCBs);

        for (const auto& obj : transparentObjects)
        {
            const DirectX::XMMATRIX transparentModel =
                DirectX::XMMatrixScaling(1.1f, 1.1f, 1.1f) *
                DirectX::XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z) *
                baseRotation;
            UpdateModelBuffer(transparentModel);
            UpdateTransparentColorBuffer(obj.color);
            g_deviceContext->DrawIndexed(kCubeIndexCount, 0, 0);
        }

        g_deviceContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

        ID3D11ShaderResourceView* nullSrvs[] = { nullptr, nullptr, nullptr };
        g_deviceContext->PSSetShaderResources(0, 3, nullSrvs);

        if (usePostProcess)
        {
            g_deviceContext->OMSetRenderTargets(1, &g_backBufferRTV, nullptr);
            g_deviceContext->ClearRenderTargetView(g_backBufferRTV, kBackColor);
            g_deviceContext->OMSetDepthStencilState(nullptr, 0);
            g_deviceContext->RSSetState(g_postProcessRasterizerState);
            g_deviceContext->IASetInputLayout(nullptr);
            g_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_deviceContext->VSSetShader(g_postProcessVS, nullptr, 0);
            g_deviceContext->PSSetShader(g_postProcessPS, nullptr, 0);
            PostProcessParams pp = {};
            if (g_postProcessMode == 3)
            {
                pp.brightnessContrast = DirectX::XMFLOAT4(-0.20f, 2.20f, 0.0f, 0.0f); // extreme
            }
            else
            {
                pp.brightnessContrast = DirectX::XMFLOAT4(0.02f, 1.18f, 0.0f, 0.0f); // soft
            }
            g_deviceContext->UpdateSubresource(g_postProcessParamsBuffer, 0, nullptr, &pp, 0, 0);
            g_deviceContext->PSSetConstantBuffers(0, 1, &g_postProcessParamsBuffer);
            g_deviceContext->PSSetShaderResources(0, 1, &g_offscreenSRV);
            g_deviceContext->PSSetSamplers(0, 1, &g_samplerState);
            g_deviceContext->Draw(3, 0);
            ID3D11ShaderResourceView* nullPost[] = { nullptr };
            g_deviceContext->PSSetShaderResources(0, 1, nullPost);
        }

        g_swapChain->Present(1, 0);
    }
}

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

        wcscpy_s(szTitle, L"hw4");
        wcscpy_s(szWindowClass, L"HW4WndClass");
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        CleanupDirect3D();
        return FALSE;
    }

    MSG msg = {};
    bool exitRequested = false;

    while (!exitRequested)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                exitRequested = true;
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!exitRequested)
        {
            RenderFrame();
        }
    }

    CleanupDirect3D();
    return static_cast<int>(msg.wParam);
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    return RegisterClassExW(&wcex);
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    g_hInstance = hInstance;

    RECT windowRect = { 0, 0, static_cast<LONG>(kClientWidth), static_cast<LONG>(kClientHeight) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, TRUE);

    g_hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (g_hWnd == nullptr)
    {
        return FALSE;
    }

    if (!InitializeDirect3D(g_hWnd))
    {
        DestroyWindow(g_hWnd);
        g_hWnd = nullptr;
        return FALSE;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    g_startTime = std::chrono::steady_clock::now();
    g_previousFrameTime = g_startTime;

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        g_isMouseDragging = true;
        g_lastMousePosition.x = GET_X_LPARAM(lParam);
        g_lastMousePosition.y = GET_Y_LPARAM(lParam);
        SetCapture(hWnd);
        return 0;
    case WM_LBUTTONUP:
        g_isMouseDragging = false;
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        g_isMouseDragging = false;
        return 0;
    case WM_MOUSEMOVE:
        if (g_isMouseDragging)
        {
            const int mouseX = GET_X_LPARAM(lParam);
            const int mouseY = GET_Y_LPARAM(lParam);
            const int deltaX = mouseX - g_lastMousePosition.x;
            const int deltaY = mouseY - g_lastMousePosition.y;

            g_lastMousePosition.x = mouseX;
            g_lastMousePosition.y = mouseY;

            g_cameraYaw += static_cast<float>(deltaX) * kMouseRotationSensitivity;
            g_cameraPitch -= static_cast<float>(deltaY) * kMouseRotationSensitivity;
        }
        return 0;
    case WM_MOUSEWHEEL:
    {
        const short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_cameraDistance -= (static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA)) * kMouseWheelZoomStep;
        g_cameraDistance = ClampValue(g_cameraDistance, 2.0f, 10.0f);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == '1')
        {
            g_postProcessMode = 1;
            return 0;
        }
        if (wParam == '2')
        {
            g_postProcessMode = 2;
            return 0;
        }
        if (wParam == '3')
        {
            g_postProcessMode = 3;
            return 0;
        }
        break;
    case WM_SIZE:
    {
        const UINT width = LOWORD(lParam);
        const UINT height = HIWORD(lParam);

        if (wParam != SIZE_MINIMIZED)
        {
            ResizeBackBuffer(width, height);
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps = {};
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
