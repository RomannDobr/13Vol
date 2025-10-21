// 13Volume_1.3

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <strsafe.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <atomic>
#include <cmath>

// --------------------- Константы ---------------------
static const int POPUP_WIDTH = 120;
static const int POPUP_HEIGHT = 115;
static const int POPUP_PADDING = 12;
static const wchar_t *POPUP_CLASS_NAME = L"13VoIPopupClass";
static const wchar_t *MSG_WINDOW_CLASS = L"13VoIMessageWindowClass";
#define WM_SHOW_13VOI (WM_APP + 1)
#define WM_HIDE_13VOI (WM_APP + 2)
#define WM_DIGIT_13VOI (WM_APP + 3)
static const UINT POPUP_TIMEOUT_MS = 2000;
static const UINT TIMER_HIDE_POPUP = 1;
static const UINT TIMER_INPUT_TIMEOUT = 2;

// --------------------- Глобальные переменные ---------------------
static HHOOK g_hKeyboardHook = NULL;
static HWND g_hMsgWindow = NULL;
static HWND g_hPopupWindow = NULL;
static std::atomic<bool> g_keyDown[256];
static IAudioEndpointVolume *g_pEndpointVolume = nullptr;
static int g_inputValue = -1;
static bool g_isPopupVisible = false;
static bool g_popupClassRegistered = false;
static std::atomic<bool> g_inputComplete = false;

// --------------------- Прототипы функций ---------------------
// Работа с громкостью
int GetCurrentVolume99();
void SetVolumeFrom99(int val);
bool InitEndpointVolume();
void UninitEndpointVolume();

// Работа с Popup
bool RegisterPopupClass();
void RedrawPopup();
void ShowPopupWindow();
void HidePopupWindow();
void CleanupPopupWindow();

// Оконные процедуры
LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Создание окна сообщений
HWND CreateMessageWindow(HINSTANCE hInst);

// Автозагрузка
bool autorun();

// Клавиатурный хук
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

// Утилиты
void SafeRelease(IUnknown *pUnk);

// --------------------- Главная функция ---------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // Автозагрузка
    LONG check = RegGetValueA(HKEY_CURRENT_USER,
                              "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                              "13Vol",
                              RRF_RT_REG_SZ, 0, 0, 0);
    if (check == ERROR_FILE_NOT_FOUND)
        autorun(); // Добавляем в автозагрузку

    // Инициализация COM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        MessageBoxW(NULL, L"COM initialization failed", L"Error", MB_ICONERROR);
        return -1;
    }

    // Инициализация аудио
    if (!InitEndpointVolume())
    {
        MessageBoxW(NULL, L"Audio endpoint initialization failed", L"Error", MB_ICONERROR);
        CoUninitialize();
        return -1;
    }

    // Инициализация массива клавиш
    for (int i = 0; i < 256; ++i)
        g_keyDown[i] = false;

    // Создание окна сообщений
    g_hMsgWindow = CreateMessageWindow(hInstance);
    if (!g_hMsgWindow)
    {
        MessageBoxW(NULL, L"Message window creation failed", L"Error", MB_ICONERROR);
        UninitEndpointVolume();
        CoUninitialize();
        return -1;
    }

    // Установка клавиатурного хука
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(NULL), 0);
    if (!g_hKeyboardHook)
    {
        MessageBoxW(NULL, L"Keyboard hook installation failed", L"Error", MB_ICONERROR);
        UninitEndpointVolume();
        CoUninitialize();
        return -1;
    }

    // Основной цикл сообщений
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Очистка ресурсов
    CleanupPopupWindow();

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

// ---------- Утилиты ----------
void SafeRelease(IUnknown *pUnk)
{
    if (pUnk)
        pUnk->Release();
}

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
        SafeRelease(pEnumerator);
        return false;
    }

    hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void **)&g_pEndpointVolume);

    // Освобождаем ресурсы независимо от результата Activate
    SafeRelease(pDevice);
    SafeRelease(pEnumerator);

    return SUCCEEDED(hr) && g_pEndpointVolume;
}

void UninitEndpointVolume()
{
    SafeRelease(g_pEndpointVolume);
}

// ---------- Popup окно ----------
bool RegisterPopupClass()
{
    if (g_popupClassRegistered)
        return true;

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.lpfnWndProc = PopupWndProc;
    wcex.hInstance = GetModuleHandleW(NULL);
    wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = POPUP_CLASS_NAME;
    wcex.style = CS_HREDRAW | CS_VREDRAW;

    if (RegisterClassExW(&wcex) == 0)
        return false;

    g_popupClassRegistered = true;
    return true;
}

void RedrawPopup()
{
    if (g_hPopupWindow && g_isPopupVisible)
    {
        InvalidateRect(g_hPopupWindow, NULL, TRUE);
        UpdateWindow(g_hPopupWindow);
    }
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
        if (bg)
        {
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);
        }

        // Рамка
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
        if (pen)
        {
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

            Rectangle(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);

            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);
        }

        // Заголовок
        const wchar_t *title = L"13 Volume";
        HFONT hTitle = CreateFontW(23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        if (hTitle)
        {
            HFONT oldFont = (HFONT)SelectObject(hdc, hTitle);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);

            RECT rTitle = rc;
            rTitle.top += 9;
            rTitle.bottom = rTitle.top + 25;
            DrawTextW(hdc, title, -1, &rTitle, DT_CENTER | DT_TOP | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            DeleteObject(hTitle);
        }

        // Уровень громкости
        int displayVol = GetCurrentVolume99(); // По умолчанию показываем текущую громкость

        if (g_inputValue != -1)
        {
            if (!g_inputComplete && g_inputValue >= 0 && g_inputValue <= 9)
                // Во время ввода одной цифры показываем ПРЕДВАРИТЕЛЬНОЕ значение ×10
                displayVol = g_inputValue * 10;
            else if (g_inputComplete)
                // После завершения ввода показываем ФИНАЛЬНОЕ значение
                displayVol = g_inputValue;
        }

        if (displayVol > 99)
            displayVol = 99;

        wchar_t volText[64];
        StringCchPrintfW(volText, _countof(volText), L"%02d", displayVol);

        HFONT hVal = CreateFontW(110, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        if (hVal)
        {
            HFONT oldFont = (HFONT)SelectObject(hdc, hVal);

            RECT rVal = rc;
            rVal.top += 15;
            DrawTextW(hdc, volText, -1, &rVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            DeleteObject(hVal);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

void ShowPopupWindow()
{
    // Регистрируем класс окна
    if (!RegisterPopupClass())
        return;

    // Создаем окно, если оно еще не создано
    if (g_hPopupWindow == NULL)
    {
        g_hPopupWindow = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            POPUP_CLASS_NAME,
            L"13VoI Popup",
            WS_POPUP,
            CW_USEDEFAULT, CW_USEDEFAULT,
            POPUP_WIDTH, POPUP_HEIGHT,
            NULL, NULL, GetModuleHandleW(NULL), NULL);

        if (g_hPopupWindow == NULL)
        {
            return;
        }

        // Устанавливаем прозрачность
        SetLayeredWindowAttributes(g_hPopupWindow, 0, 255, LWA_ALPHA);
    }

    // Получаем размеры рабочей области
    RECT workArea;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0))
    {
        workArea.right = GetSystemMetrics(SM_CXSCREEN);
        workArea.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    // Позиционируем окно в правом нижнем углу
    int x = workArea.right - POPUP_WIDTH - 10;
    int y = workArea.bottom - POPUP_HEIGHT - 10;

    // Показываем окно
    SetWindowPos(g_hPopupWindow, HWND_TOPMOST, x, y, POPUP_WIDTH, POPUP_HEIGHT,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    g_isPopupVisible = true;
    g_inputComplete = false;
    RedrawPopup();

    // Устанавливаем таймер для автоматического скрытия
    SetTimer(g_hMsgWindow, TIMER_HIDE_POPUP, POPUP_TIMEOUT_MS, NULL);
}

void HidePopupWindow()
{
    if (g_hPopupWindow)
        ShowWindow(g_hPopupWindow, SW_HIDE);
    g_isPopupVisible = false;
    g_inputValue = -1;
    g_inputComplete = false;
    KillTimer(g_hMsgWindow, TIMER_HIDE_POPUP);
    KillTimer(g_hMsgWindow, TIMER_INPUT_TIMEOUT);
}

void CleanupPopupWindow()
{
    HidePopupWindow();

    if (g_hPopupWindow)
    {
        DestroyWindow(g_hPopupWindow);
        g_hPopupWindow = NULL;
    }

    if (g_popupClassRegistered)
    {
        UnregisterClassW(POPUP_CLASS_NAME, GetModuleHandleW(NULL));
        g_popupClassRegistered = false;
    }
}

// ---------- Окно сообщений ----------
LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SHOW_13VOI:
        if (!g_isPopupVisible)
        {
            g_inputValue = -1;
            g_inputComplete = false;
        }
        ShowPopupWindow();
        return 0;

    case WM_DIGIT_13VOI:
    {
        int digit = (int)wParam;

        if (g_inputValue == -1)
        {
            // Первая цифра
            g_inputValue = digit;
            // Устанавливаем таймер для одиночной цифры
            KillTimer(hwnd, TIMER_INPUT_TIMEOUT);
            SetTimer(hwnd, TIMER_INPUT_TIMEOUT, 1500, NULL);

            // Сразу показываем предварительное значение ×10
            RedrawPopup();
        }
        else if (!g_inputComplete)
        {
            // Вторая цифра
            g_inputValue = g_inputValue * 10 + digit;
            if (g_inputValue > 99)
                g_inputValue = 99;

            SetVolumeFrom99(g_inputValue);
            g_inputComplete = true;

            // Сбрасываем таймер одиночной цифры
            KillTimer(hwnd, TIMER_INPUT_TIMEOUT);

            RedrawPopup();
        }
        // Если ввод завершен, игнорируем дальнейшие цифры

        // Сбрасываем таймер скрытия
        KillTimer(hwnd, TIMER_HIDE_POPUP);
        SetTimer(hwnd, TIMER_HIDE_POPUP, POPUP_TIMEOUT_MS, NULL);
        return 0;
    }

    case WM_TIMER:
        switch (wParam)
        {
        case TIMER_HIDE_POPUP:
            HidePopupWindow();
            break;

        case TIMER_INPUT_TIMEOUT:
            // Таймер для одиночной цифры
            if (g_inputValue >= 0 && g_inputValue <= 9 && !g_inputComplete)
            {
                int val = g_inputValue * 10;
                SetVolumeFrom99(val);
                g_inputComplete = true;
                g_inputValue = val; // Обновляем значение для корректного отображения
                RedrawPopup();
            }
            KillTimer(hwnd, TIMER_INPUT_TIMEOUT);
            break;
        }
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
    wcex.style = 0;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;

    if (RegisterClassExW(&wcex) == 0)
        return NULL;

    HWND hwnd = CreateWindowExW(
        0, MSG_WINDOW_CLASS, L"13VoI Message Window",
        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
        NULL, NULL, hInst, NULL);

    if (hwnd)
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
            bool onePressed = g_keyDown[0x31] || g_keyDown[VK_NUMPAD1];
            bool threePressed = g_keyDown[0x33] || g_keyDown[VK_NUMPAD3];

            if (onePressed && threePressed)
                PostMessageW(g_hMsgWindow, WM_SHOW_13VOI, 0, 0);

            // Цифровой ввод
            if (g_isPopupVisible && isDown && !g_inputComplete)
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

// ---------- Автозагрузка ----------
bool autorun()
{
    char exePath[MAX_PATH];

    DWORD pathLen = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (pathLen == 0 || pathLen >= MAX_PATH)
        return false;

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER,
                                "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_WRITE, &hKey);

    if (result != ERROR_SUCCESS)
        return false;

    DWORD dataSize = (DWORD)strlen(exePath) + 1;
    result = RegSetValueExA(hKey, "13Vol", 0, REG_SZ,
                            (const BYTE *)exePath, dataSize);

    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS);
}