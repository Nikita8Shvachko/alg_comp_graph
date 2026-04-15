#include "framework.h"
#include "hw1.h"

#include <windowsx.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

#define MAX_LOADSTRING 100

WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

namespace
{
    constexpr UINT kClientWidth = 1280;
    constexpr UINT kClientHeight = 720;
    constexpr FLOAT kBackColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    constexpr UINT kCubeIndexCount = 36;
    constexpr float kNearPlane = 0.1f;
    constexpr float kFarPlane = 100.0f;
    constexpr float kVerticalFovDegrees = 60.0f;
    constexpr float kRotationSpeed = 1.0f;
    constexpr float kCameraRotateSpeed = 1.75f;
    constexpr float kCameraZoomSpeed = 2.0f;
    constexpr float kMouseRotationSensitivity = 0.01f;
    constexpr float kMouseWheelZoomStep = 0.5f;

    struct Vertex
    {
        float position[3];
        std::uint32_t color;
    };

    struct ModelBuffer
    {
        DirectX::XMFLOAT4X4 model;
    };

    struct SceneBuffer
    {
        DirectX::XMFLOAT4X4 vp;
    };

    constexpr std::uint32_t PackColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255)
    {
        return static_cast<std::uint32_t>(red)
            | (static_cast<std::uint32_t>(green) << 8)
            | (static_cast<std::uint32_t>(blue) << 16)
            | (static_cast<std::uint32_t>(alpha) << 24);
    }

    constexpr Vertex kCubeVertices[] =
    {
        { { -0.5f, -0.5f,  0.5f }, PackColor(255,   0,   0) },
        { {  0.5f, -0.5f,  0.5f }, PackColor(  0, 255,   0) },
        { {  0.5f,  0.5f,  0.5f }, PackColor(  0,   0, 255) },
        { { -0.5f,  0.5f,  0.5f }, PackColor(255, 255,   0) },
        { { -0.5f, -0.5f, -0.5f }, PackColor(255,   0, 255) },
        { {  0.5f, -0.5f, -0.5f }, PackColor(  0, 255, 255) },
        { {  0.5f,  0.5f, -0.5f }, PackColor(255, 255, 255) },
        { { -0.5f,  0.5f, -0.5f }, PackColor(128, 128, 128) },
    };

    constexpr std::uint32_t kCubeIndices[] =
    {
        0, 1, 2, 0, 2, 3,
        4, 6, 5, 4, 7, 6,
        0, 3, 7, 0, 7, 4,
        1, 5, 6, 1, 6, 2,
        3, 2, 6, 3, 6, 7,
        0, 4, 5, 0, 5, 1,
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
    ID3D11Buffer* g_indexBuffer = nullptr;
    ID3D11Buffer* g_modelBuffer = nullptr;
    ID3D11Buffer* g_sceneBuffer = nullptr;
    ID3D11VertexShader* g_vertexShader = nullptr;
    ID3D11PixelShader* g_pixelShader = nullptr;
    ID3D11InputLayout* g_inputLayout = nullptr;
    ID3D11RasterizerState* g_rasterizerState = nullptr;
    D3D11_VIEWPORT g_viewport = {};

    std::chrono::steady_clock::time_point g_startTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point g_previousFrameTime = g_startTime;
    float g_cameraYaw = 0.0f;
    float g_cameraPitch = DirectX::XMConvertToRadians(20.0f);
    float g_cameraDistance = 4.0f;
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
        vertexBufferDesc.ByteWidth = static_cast<UINT>(sizeof(kCubeVertices));
        vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vertexData = {};
        vertexData.pSysMem = kCubeVertices;

        HRESULT hr = g_device->CreateBuffer(&vertexBufferDesc, &vertexData, &g_vertexBuffer);
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

        return SUCCEEDED(g_device->CreateBuffer(&sceneBufferDesc, nullptr, &g_sceneBuffer));
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
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = g_device->CreateInputLayout(
            inputElements,
            ARRAYSIZE(inputElements),
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            &g_inputLayout);

        SafeRelease(vertexShaderBlob);
        SafeRelease(pixelShaderBlob);

        return SUCCEEDED(hr);
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

        return SUCCEEDED(g_device->CreateRasterizerState(&rasterizerDesc, &g_rasterizerState));
    }

    void ReleaseSceneResources()
    {
        SafeRelease(g_rasterizerState);
        SafeRelease(g_inputLayout);
        SafeRelease(g_pixelShader);
        SafeRelease(g_vertexShader);
        SafeRelease(g_sceneBuffer);
        SafeRelease(g_modelBuffer);
        SafeRelease(g_indexBuffer);
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
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        hr = g_device->CreateTexture2D(&depthDesc, nullptr, &g_depthStencilTexture);
        if (FAILED(hr))
        {
            return false;
        }

        return SUCCEEDED(g_device->CreateDepthStencilView(g_depthStencilTexture, nullptr, &g_depthStencilView));
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
        if (g_deviceContext == nullptr || g_modelBuffer == nullptr || g_sceneBuffer == nullptr)
        {
            return false;
        }

        ModelBuffer modelBuffer = {};
        const DirectX::XMMATRIX model =
            DirectX::XMMatrixRotationY(elapsedSeconds * kRotationSpeed) *
            DirectX::XMMatrixRotationX(elapsedSeconds * (kRotationSpeed * 0.5f));
        DirectX::XMStoreFloat4x4(&modelBuffer.model, DirectX::XMMatrixTranspose(model));
        g_deviceContext->UpdateSubresource(g_modelBuffer, 0, nullptr, &modelBuffer, 0, 0);

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

        SceneBuffer sceneBuffer = {};
        DirectX::XMStoreFloat4x4(
            &sceneBuffer.vp,
            DirectX::XMMatrixTranspose(DirectX::XMMatrixMultiply(view, projection)));

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT hr = g_deviceContext->Map(g_sceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            return false;
        }

        std::memcpy(mapped.pData, &sceneBuffer, sizeof(sceneBuffer));
        g_deviceContext->Unmap(g_sceneBuffer, 0);
        return true;
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

        if (!CreateShadersAndInputLayout())
        {
            return false;
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
            || g_indexBuffer == nullptr
            || g_modelBuffer == nullptr
            || g_sceneBuffer == nullptr
            || g_vertexShader == nullptr
            || g_pixelShader == nullptr
            || g_inputLayout == nullptr
            || g_rasterizerState == nullptr)
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

        g_deviceContext->OMSetRenderTargets(1, &g_backBufferRTV, g_depthStencilView);
        g_deviceContext->RSSetViewports(1, &g_viewport);
        g_deviceContext->ClearRenderTargetView(g_backBufferRTV, kBackColor);
        g_deviceContext->ClearDepthStencilView(g_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        const UINT stride = sizeof(Vertex);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffers[] = { g_vertexBuffer };
        ID3D11Buffer* constantBuffers[] = { g_modelBuffer, g_sceneBuffer };

        g_deviceContext->IASetIndexBuffer(g_indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        g_deviceContext->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
        g_deviceContext->IASetInputLayout(g_inputLayout);
        g_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_deviceContext->RSSetState(g_rasterizerState);
        g_deviceContext->VSSetShader(g_vertexShader, nullptr, 0);
        g_deviceContext->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
        g_deviceContext->PSSetShader(g_pixelShader, nullptr, 0);
        g_deviceContext->DrawIndexed(kCubeIndexCount, 0, 0);

        g_swapChain->Present(1, 0);
    }
}

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_HW1, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        CleanupDirect3D();
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_HW1));

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

            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
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
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_HW1);
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
    case WM_COMMAND:
    {
        const int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            return 0;
        default:
            break;
        }
        break;
    }
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

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return static_cast<INT_PTR>(TRUE);
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return static_cast<INT_PTR>(TRUE);
        }
        break;
    default:
        break;
    }

    return static_cast<INT_PTR>(FALSE);
}
