// hw1.cpp : Определяет точку входа для приложения.
//

#include "framework.h"
#include "hw1.h"

#include <d3d11.h>
#include <dxgi1_6.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define MAX_LOADSTRING 100

WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

namespace
{
    constexpr UINT kClientWidth = 1280;
    constexpr UINT kClientHeight = 720;
    constexpr FLOAT kBackColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr)
        {
            object->Release();
            object = nullptr;
        }
    }

    HINSTANCE g_hInstance = nullptr;
    HWND g_hWnd = nullptr;

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_deviceContext = nullptr;
    IDXGISwapChain1* g_swapChain = nullptr;
    ID3D11RenderTargetView* g_backBufferRTV = nullptr;
    D3D11_VIEWPORT g_viewport = {};

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

        g_viewport.TopLeftX = 0.0f;
        g_viewport.TopLeftY = 0.0f;
        g_viewport.Width = static_cast<FLOAT>(clientRect.right - clientRect.left);
        g_viewport.Height = static_cast<FLOAT>(clientRect.bottom - clientRect.top);
        g_viewport.MinDepth = 0.0f;
        g_viewport.MaxDepth = 1.0f;

        return true;
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

        SafeRelease(adapter);

        if (FAILED(hr))
        {
#ifdef _DEBUG
            if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
            {
                MessageBoxW(
                    hWnd,
                    L"Не найден Direct3D Debug Layer. Установите компонент Graphics Tools "
                    L"в Windows Optional Features и повторите запуск Debug-сборки.",
                    szTitle,
                    MB_ICONERROR | MB_OK);
            }
#endif
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

        return CreateBackBufferResources();
    }

    void CleanupDirect3D()
    {
        ReleaseBackBufferResources();
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
        if (g_deviceContext == nullptr || g_swapChain == nullptr || g_backBufferRTV == nullptr)
        {
            return;
        }

        g_deviceContext->OMSetRenderTargets(1, &g_backBufferRTV, nullptr);
        g_deviceContext->RSSetViewports(1, &g_viewport);
        g_deviceContext->ClearRenderTargetView(g_backBufferRTV, kBackColor);
        g_swapChain->Present(1, 0);
    }
}

// Отправить объявления функций, включенных в этот модуль кода:
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

//
//  ФУНКЦИЯ: MyRegisterClass()
//
//  ЦЕЛЬ: Регистрирует класс окна.
//
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

//
//   ФУНКЦИЯ: InitInstance(HINSTANCE, int)
//
//   ЦЕЛЬ: Сохраняет маркер экземпляра и создает главное окно.
//
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

    return TRUE;
}

//
//  ФУНКЦИЯ: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  ЦЕЛЬ: Обрабатывает сообщения в главном окне.
//
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

// Обработчик сообщений для окна "О программе".
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
