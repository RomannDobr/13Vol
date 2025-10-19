// 13Volume_ChatGPT-5

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <strsafe.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <atomic>
#include <cmath> // для round()

// --------------------- Константы ---------------------
static const int POPUP_WIDTH = 120;
static const int POPUP_HEIGHT = 115;
static const int POPUP_PADDING = 12;
static const wchar_t *POPUP_CLASS_NAME = L"13VoIPopupClass";
static const wchar_t *MSG_WINDOW_CLASS = L"13VoIMessageWindowClass";
#define WM_SHOW_13VOI (WM_APP + 1)
#define WM_HIDE_13VOI (WM_APP + 2)
#define WM_DIGIT_13VOI (WM_APP + 3)
static const UINT POPUP_TIMEOUT_MS = 5000;

// --------------------- Глобальные переменные ---------------------
static HHOOK g_hKeyboardHook = NULL;
static HWND g_hMsgWindow = NULL;
static HWND g_hPopupWindow = NULL;
static std::atomic<bool> g_keyDown[256];
static IAudioEndpointVolume *g_pEndpointVolume = nullptr;
static int g_inputValue = -1;
static bool g_isPopupVisible = false;

// --------------------- Прототипы функций ---------------------
// Работа с громкостью
int GetCurrentVolume99();
void SetVolumeFrom99(int val);
bool InitEndpointVolume();
void UninitEndpointVolume();

// Работа с Popup
void RedrawPopup();
void ShowPopupWindow();
void HidePopupWindow();

// Оконные процедуры
LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Создание окна сообщений
HWND CreateMessageWindow(HINSTANCE hInst);

// Клавиатурный хук
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

// --------------------- Главная функция ---------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!InitEndpointVolume())
        return -1;

    for (int i = 0; i < 256; ++i)
        g_keyDown[i] = false;

    g_hMsgWindow = CreateMessageWindow(hInstance);
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(NULL), 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hKeyboardHook)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }

    UninitEndpointVolume();
    CoUninitialize();
    return 0;
}

// --------------------- Реализации функций ---------------------

// ---------- Работа с громкостью ----------
int GetCurrentVolume99()
{
    if (!g_pEndpointVolume)
        return 0;
    float level = 0.0f;
    if (SUCCEEDED(g_pEndpointVolume->GetMasterVolumeLevelScalar(&level)))
    {
        int v = (int)std::round(level * 99.0f);
        if (v < 0)
            v = 0;
        if (v > 99)
            v = 99;
        return v;
    }
    return 0;
}

void SetVolumeFrom99(int val)
{
    if (!g_pEndpointVolume)
        return;
    if (val < 0)
        val = 0;
    if (val > 99)
        val = 99;
    float scalar = (val / 99.0f);
    g_pEndpointVolume->SetMasterVolumeLevelScalar(scalar, NULL);
}

bool InitEndpointVolume()
{
    HRESULT hr;
    IMMDeviceEnumerator *pEnumerator = nullptr;
    IMMDevice *pDevice = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator);
    if (FAILED(hr) || !pEnumerator)
        return false;

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr) || !pDevice)
    {
        if (pEnumerator)
            pEnumerator->Release();
        return false;
    }

    hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void **)&g_pEndpointVolume);
    pDevice->Release();
    pEnumerator->Release();
    return SUCCEEDED(hr) && g_pEndpointVolume;
}

void UninitEndpointVolume()
{
    if (g_pEndpointVolume)
    {
        g_pEndpointVolume->Release();
        g_pEndpointVolume = nullptr;
    }
}

// ---------- Popup окно ----------
void RedrawPopup()
{
    if (g_hPopupWindow)
        InvalidateRect(g_hPopupWindow, NULL, TRUE);
}

LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Фон
        HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // Рамка
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        // Заголовок
        const wchar_t *title = L"13 Volume";
        HFONT hTitle = CreateFontW(23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, hTitle);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        RECT rTitle = rc;
        rTitle.top += 5;
        // rTitle.left += POPUP_PADDING;
        DrawTextW(hdc, title, -1, &rTitle, DT_CENTER | DT_TOP | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(hTitle);

        // Уровень громкости
        int vol = GetCurrentVolume99();
        wchar_t volText[64];
        StringCchPrintfW(volText, _countof(volText), L"%02d", vol);
        HFONT hVal = CreateFontW(110, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        oldFont = (HFONT)SelectObject(hdc, hVal);
        RECT rVal = rc;
        rVal.top += 10;
        // rVal.left += POPUP_PADDING;
        DrawTextW(hdc, volText, -1, &rVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(hVal);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

void ShowPopupWindow()
{
    if (g_hPopupWindow == NULL)
    {
        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.lpfnWndProc = DefWindowProcW;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.lpszClassName = POPUP_CLASS_NAME;
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wcex);

        g_hPopupWindow = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            POPUP_CLASS_NAME,
            L"13VoI Popup",
            WS_POPUP,
            CW_USEDEFAULT, CW_USEDEFAULT,
            POPUP_WIDTH, POPUP_HEIGHT,
            NULL, NULL, GetModuleHandleW(NULL), NULL);

        SetWindowLongPtrW(g_hPopupWindow, GWLP_WNDPROC, (LONG_PTR)PopupWndProc);
    }

    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int x = workArea.right - POPUP_WIDTH - 10;
    int y = workArea.bottom - POPUP_HEIGHT - 10;

    SetWindowPos(g_hPopupWindow, HWND_TOPMOST, x, y, POPUP_WIDTH, POPUP_HEIGHT, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    g_isPopupVisible = true;
    RedrawPopup();
    SetTimer(g_hMsgWindow, 1, POPUP_TIMEOUT_MS, NULL);
}

void HidePopupWindow()
{
    if (g_hPopupWindow)
    {
        ShowWindow(g_hPopupWindow, SW_HIDE);
    }
    g_isPopupVisible = false;
    g_inputValue = -1;
    KillTimer(g_hMsgWindow, 1);
}

// ---------- Окно сообщений ----------
LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SHOW_13VOI:
        if (!g_isPopupVisible)
            g_inputValue = -1;
        ShowPopupWindow();
        return 0;

    case WM_DIGIT_13VOI:
    {
        int digit = (int)wParam;
        if (g_inputValue == -1)
            g_inputValue = digit;
        else
            g_inputValue = g_inputValue * 10 + digit;

        if (g_inputValue > 99)
            g_inputValue = 99;
        SetVolumeFrom99(g_inputValue);
        RedrawPopup();

        KillTimer(hwnd, 1);
        SetTimer(hwnd, 1, POPUP_TIMEOUT_MS, NULL);
        return 0;
    }

    case WM_TIMER:
        HidePopupWindow();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

HWND CreateMessageWindow(HINSTANCE hInst)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.lpfnWndProc = MsgWndProc;
    wcex.hInstance = hInst;
    wcex.lpszClassName = MSG_WINDOW_CLASS;
    wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcex.hbrBackground = NULL;
    RegisterClassExW(&wcex);

    HWND hwnd = CreateWindowExW(
        0, MSG_WINDOW_CLASS, L"13VoI Message Window",
        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, SW_HIDE);
    return hwnd;
}

// ---------- Клавиатурный хук ----------
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lParam;
        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        if (p->vkCode < 256)
        {
            if (isDown)
                g_keyDown[p->vkCode] = true;
            if (isUp)
                g_keyDown[p->vkCode] = false;
        }

        if (g_hMsgWindow)
        {
            // Проверка одновременного нажатия 1 и 3
            // Проверка одновременного нажатия 1 и 3 (верхний ряд или numpad)
            bool onePressed = g_keyDown[0x31] || g_keyDown[VK_NUMPAD1];
            bool threePressed = g_keyDown[0x33] || g_keyDown[VK_NUMPAD3];

            if (onePressed && threePressed)
            {
                PostMessageW(g_hMsgWindow, WM_SHOW_13VOI, 0, 0);
            }

            // Цифровой ввод
            if (g_isPopupVisible && isDown)
            {
                if (p->vkCode >= 0x30 && p->vkCode <= 0x39)
                {
                    int digit = p->vkCode - 0x30;
                    PostMessageW(g_hMsgWindow, WM_DIGIT_13VOI, (WPARAM)digit, 0);
                }
                else if (p->vkCode >= VK_NUMPAD0 && p->vkCode <= VK_NUMPAD9)
                {
                    int digit = p->vkCode - VK_NUMPAD0;
                    PostMessageW(g_hMsgWindow, WM_DIGIT_13VOI, (WPARAM)digit, 0);
                }
            }
        }
    }

    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}
