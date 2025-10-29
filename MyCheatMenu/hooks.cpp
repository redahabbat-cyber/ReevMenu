#pragma warning(disable:4305 4309)  // Suppress XorStr warnings

#include "hooks.h"
#include "menu.h"
#include "main.h"  // RunFeatures
#include "offsets.h"
#include "log.h"
#include "kiero.h"
#include <TlHelp32.h>
#include <psapi.h>
#include <sstream>
#include <MinHook.h>  // Add this for WndProc hook; download minhook.lib and include
#include <d3d11.h>    // Add for D3D11 types
#include <dxgi.h>     // Add for DXGI types

#pragma comment(lib, "d3d11.lib")  // Link if needed
#pragma comment(lib, "dxgi.lib")   // Link if needed

using namespace std;

bool g_bMenuOpen = false;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;  // NEW: For backbuffer
HWND g_hwnd = nullptr;
typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);  // Ensure defined
typedef HRESULT(__stdcall* ResizeBuffersFn)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);  // NEW: Typedef for resize
PresentFn oPresent = nullptr;
ResizeBuffersFn oResizeBuffers = nullptr;  // NEW: For resize hook
WNDPROC oWndProc = nullptr;  // NEW: Original WndProc

// NEW: WndProc hook for ImGui input
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_bMenuOpen && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return 1;  // Block input if menu open and ImGui handled it
    }
    return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

// Simplified XOR (unchanged)
std::string XorStr(const char* str, char key = 0xDE) {
    std::string result = str;
    for (auto& c : result) c ^= key;
    return result;
}

// Sig scanner (unchanged)
void* FindPattern(const char* moduleName, const unsigned char* signature, const char* mask) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod) return nullptr;
    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hMod, &modInfo, sizeof(MODULEINFO))) return nullptr;
    BYTE* base = static_cast<BYTE*>(modInfo.lpBaseOfDll);
    DWORD size = modInfo.SizeOfImage;

    int sigLen = static_cast<int>(strlen(mask));
    for (DWORD i = 0; i < size - sigLen; ++i) {
        bool found = true;
        for (int j = 0; j < sigLen; ++j) {
            if (mask[j] != '?' && base[i + j] != signature[j]) {
                found = false;
                break;
            }
        }
        if (found) return base + i;
    }
    return nullptr;
}

// NEW: Create render target from backbuffer
void CreateRenderTarget(IDXGISwapChain* pSwapChain) {
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

// NEW: Cleanup render target
void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

void SetupImGui(IDXGISwapChain* pSwapChain) {
    if (g_pd3dDevice) return;

    if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
        g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);

        if (!g_hwnd) {
            LOG("ERROR: No HWND for ImGui - skipping init");
            return;
        }

        CreateRenderTarget(pSwapChain);  // NEW: Create RTV

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();  // NEW: Set style for visibility
        // Optional: io.Fonts->AddFontFromFileTTF("path/to/font.ttf", 16.0f); for custom font

        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
        LOG("ImGui Initialized Successfully");
    }
    else {
        LOG("ERROR: Failed to get D3D11 Device");
    }
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    LOG("Present called");  // NEW: Debug to confirm hook is hit

    static bool inited = false;
    if (!inited) {
        SetupImGui(pSwapChain);
        inited = true;
    }

    if (!g_pd3dDevice || !g_mainRenderTargetView) {
        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Removed foreground check for testing

    static bool lastInsert = false;
    bool currentInsert = GetAsyncKeyState(VK_INSERT) & 0x8000;
    if (currentInsert && !lastInsert) {
        g_bMenuOpen = !g_bMenuOpen;
        LOG("KEY: Menu toggled to " + to_string(g_bMenuOpen));
    }
    lastInsert = currentInsert;

    ImGui::GetBackgroundDrawList()->AddText(ImVec2(10, 10), IM_COL32(255, 0, 0, 255), "CHEAT ACTIVE");

    std::ostringstream oss;
    oss << "Menu: " << (g_bMenuOpen ? "ON" : "OFF");
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(10, 30), IM_COL32(255, 255, 255, 255), oss.str().c_str());

    if (g_bMenuOpen) RenderMenu();
    RunFeatures();

    ImGui::EndFrame();
    ImGui::Render();

    // NEW: Set RTV before draw
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// NEW: ResizeBuffers hook
HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    LOG("ResizeBuffers called - Reinitializing ImGui");

    if (g_mainRenderTargetView) {
        CleanupRenderTarget();
    }

    // NEW: Invalidate ImGui objects
    ImGui_ImplDX11_InvalidateDeviceObjects();

    HRESULT result = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    // Re-create
    CreateRenderTarget(pSwapChain);
    ImGui_ImplDX11_CreateDeviceObjects();

    return result;
}

void SetupHooks() {
    LOG("Setting up hooks with Kiero...");
    if (kiero::init(kiero::RenderType::D3D11) != kiero::Status::Success) {
        LOG("Kiero failed - MinHook fallback");
        MH_Initialize();
        unsigned char presentSig[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x0F, 0x29, 0x70, 0xB8, 0x0F, 0x29, 0x78, 0xA0 };
        const char* mask = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
        void* addr = FindPattern("dxgi.dll", presentSig, mask);
        if (addr) {
            MH_CreateHook(addr, hkPresent, reinterpret_cast<LPVOID*>(&oPresent));
            MH_EnableHook(addr);
            LOG("MinHook Present hooked at 0x" + to_string(reinterpret_cast<uintptr_t>(addr)));
        }
        else {
            LOG("ERROR: No Present hook - update sig!");
        }
        return;
    }

    // Bind Present (index 8)
    kiero::bind(8, (void**)&oPresent, (void*)hkPresent);
    LOG("HOOK: Present bound via Kiero");

    // NEW: Bind ResizeBuffers (index 13)
    kiero::bind(13, (void**)&oResizeBuffers, (void*)hkResizeBuffers);
    LOG("HOOK: ResizeBuffers bound via Kiero");

    // NEW: Hook WndProc with MinHook (init MH if not already)
    MH_Initialize();
    oWndProc = (WNDPROC)SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    LOG("HOOK: WndProc hooked");
}

void CleanupHooks() {
    // NEW: Restore WndProc
    if (oWndProc) SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);

    CleanupRenderTarget();  // NEW

    if (g_pd3dDevice) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_pd3dDevice->Release();
        g_pd3dDeviceContext->Release();
        g_pd3dDevice = nullptr;
        g_pd3dDeviceContext = nullptr;
    }
    kiero::shutdown();
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    LOG("Hooks cleaned up");
}

BOOL CALLBACK EnumWindowCallback(HWND hwnd, LPARAM lParam) {
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        if (strcmp(className, "SDL_app") == 0) {
            g_hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

DWORD WINAPI MainThread(LPVOID param) {
    LOG("MainThread started - Enumerating modules for client.dll");
    DWORD startTime = GetTickCount();
    uintptr_t clientBase = 0;

    while ((GetTickCount() - startTime) < 30000) {
        HMODULE hMods[1024];
        DWORD cbNeeded;
        if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
            int numMods = cbNeeded / sizeof(HMODULE);
            for (int i = 0; i < numMods; i++) {
                char modName[MAX_PATH];
                if (GetModuleBaseNameA(GetCurrentProcess(), hMods[i], modName, sizeof(modName))) {
                    if (_stricmp(modName, "client.dll") == 0) {
                        clientBase = reinterpret_cast<uintptr_t>(hMods[i]);
                        LOG("client.dll found via enum - base: 0x" + to_string(clientBase));
                        break;
                    }
                }
            }
            if (clientBase) {
                break;
            }
        }

        if (GetModuleHandleA("engine2.dll") || GetModuleHandleA("tier0.dll")) {
            LOG("Alt module found - continuing enum");
        }

        Sleep(500);
    }

    if (!clientBase) {
        LOG("ERROR: Timeout - client.dll not found. Inject after full map load.");
        return 1;
    }

    LOG("Loading offsets from client.dll base");
    offsets::dwLocalPlayerPawn = clientBase + 0x1BE3010;
    offsets::dwEntityList = clientBase + 0x1D07C80;
    offsets::dwViewMatrix = clientBase + 0x1E25FB0;
    LOG("Offsets loaded: LocalPawn=0x" + to_string(offsets::dwLocalPlayerPawn) +
        ", EntityList=0x" + to_string(offsets::dwEntityList));

    // Improved HWND find with EnumWindows
    EnumWindows(EnumWindowCallback, 0);
    if (g_hwnd) {
        LOG("Found CS2 window via Enum: SDL_app");
    }
    else {
        LOG("ERROR: No CS2 window found");
    }

    SetupHooks();

    CreateThread(nullptr, 0, FeatureThread, nullptr, 0, nullptr);

    while (true) {
        Sleep(1000);
    }
    return 0;
}