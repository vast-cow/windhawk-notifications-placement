// ==WindhawkMod==
// @id              notifications-placement-fork
// @name            Customize Windows notifications placement - Mouse avoidance
// @description     Move notifications to an adjacent corner when the mouse approaches
// @version         1.2.4
// @author          m417z
// @github          https://github.com/m417z
// @twitter         https://twitter.com/m417z
// @homepage        https://m417z.com/
// @include         explorer.exe
// @include         ShellExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshcore
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.
//
// For bug reports and feature requests, please open an issue here:
// https://github.com/ramensoftware/windhawk-mods/issues
//
// For pull requests, development takes place here:
// https://github.com/m417z/my-windhawk-mods

// ==WindhawkModReadme==
/*
# Customize Windows notifications placement

Move notifications to another monitor or another corner of the screen.

## Mouse avoidance (local modification)

Every 300 ms, the mod checks the shortest straight-line distance from the
cursor to the current notification window rectangle. If the cursor is on the
same monitor and the distance is at or below **Mouse avoidance distance**,
the notification moves to the farther of the two adjacent corners.
The setting defaults to 48 device-independent pixels (DIP) and scales with
the notification monitor's DPI. With a value of 0, including negative values
treated as 0, the cursor must be inside or on the edge of the rectangle.
The chosen corner is retained until another avoidance move or a settings
change. The horizontal neighbor wins an exact distance tie. Selecting Center
on either axis disables mouse avoidance. The monitor, DPI and edge-distance
settings remain in effect. Automatic animation direction follows the effective
corner when the Shell next positions the notification on its UI thread.

Only Windows 10 64-bit and Windows 11 are supported.

![Screenshot](https://i.imgur.com/4PxMvLg.png)

## Notification transparency

Use **Notification transparency (%)** to adjust the entire notification,
including its background, text, icons, and buttons. The default of 0 keeps
the original appearance. Use 50 for half transparency or 100 for full
transparency. Changes also apply to notifications already on screen.

## Selecting a monitor

You can select a monitor by its number or by its interface name in the mod
settings.

### By monitor number

Set the **Monitor** setting to the desired monitor number (1, 2, 3, etc.). Note
that this number may differ from the monitor number shown in Windows Display
Settings.

Set the **Monitor** setting to 0 to use the monitor where the mouse cursor is
currently located.

### By interface name

If monitor numbers change frequently (e.g., after locking your PC or
restarting), you can use the monitor's interface name instead. To find the
interface name:

1. Go to the mod's **Advanced** tab.
2. Set **Debug logging** to **Mod logs**.
3. Click on **Show log output**.
4. In the mod settings, enter any text (e.g., `TEST`) in the **Monitor interface
   name** field.
5. Trigger a notification to appear.
6. In the log output, look for lines containing `Found display device`. You will
   see one line per monitor, for example:
   ```
   Found display device \\.\DISPLAY1, interface name: \\?\DISPLAY#DELA1D2#5&abc123#0#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}
   Found display device \\.\DISPLAY2, interface name: \\?\DISPLAY#GSM5B09#4&def456#0#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}
   ```
   Use the interface name that follows the "interface name:" text. You may need
   to experiment to determine which interface name corresponds to which physical
   monitor.
7. Copy the relevant interface name (or a unique substring of it) into the
   **Monitor interface name** setting.
8. Set **Debug logging** back to **None** when done.

The **Monitor interface name** setting takes priority over the **Monitor**
number when both are configured.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- monitor: 1
  $name: Monitor
  $description: >-
    The monitor number that notifications will appear on, set to zero to use the
    monitor where the mouse cursor is located
- monitorInterfaceName: ""
  $name: Monitor interface name
  $description: >-
    If not empty, the given monitor interface name (can also be an interface
    name substring) will be used instead of the monitor number. Can be useful if
    the monitor numbers change often. To see all available interface names, set
    any interface name, enable mod logs, trigger a notification and look for
    "Found display device" messages.
- horizontalPlacement: right
  $name: Horizontal placement on the screen
  $options:
  - right: Right
  - left: Left
  - center: Center
- horizontalDistanceFromScreenEdge: 0
  $name: Distance from the right/left side of the screen
- verticalPlacement: bottom
  $name: Vertical placement on the screen
  $options:
  - bottom: Bottom
  - top: Top
  - center: Center
- verticalDistanceFromScreenEdge: 0
  $name: Distance from the bottom/top side of the screen
- mouseAvoidanceDistance: 48
  $name: Mouse avoidance distance
  $description: >-
    Maximum distance from the notification window in device-independent pixels
    (DIP) at which the notification moves away. Negative values are treated as 0.
- notificationTransparency: 0
  $name: Notification transparency (%)
  $description: >-
    Transparency of the entire notification. 0 keeps the original appearance,
    50 makes it half transparent, and 100 makes it fully transparent.
- animationDirection: automatic
  $name: Notification appearance animation direction
  $options:
  - automatic: Automatic
  - fromLeft: From left
  - fromRight: From right
  - fromTop: From top
  - fromBottom: From bottom
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#undef GetCurrentTime

#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace winrt::Windows::UI::Xaml;

std::atomic<bool> g_unloading;

enum class HorizontalPlacement {
    right,
    left,
    center,
};

enum class VerticalPlacement {
    bottom,
    top,
    center,
};

enum class AnimationDirection {
    automatic,
    fromLeft,
    fromRight,
    fromTop,
    fromBottom,
};

struct Settings {
    int monitor;
    std::wstring monitorInterfaceName;
    HorizontalPlacement horizontalPlacement;
    int horizontalDistanceFromScreenEdge;
    VerticalPlacement verticalPlacement;
    int verticalDistanceFromScreenEdge;
    int mouseAvoidanceDistance;
    int notificationTransparency;
    AnimationDirection animationDirection;
};

Settings g_settings{};
std::mutex g_settingsMutex;

Settings GetSettings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
}

// The same HWND is positioned by both explorer.exe and ShellExperienceHost.exe.
// Store the selected corner on the HWND itself, not in process-local state.
// Values are small integers (1..4), never cross-process pointers.
constexpr wchar_t kAvoidanceCornerProp[] =
    L"Windhawk.notifications-placement-fork.MouseAvoidanceCorner.v1";
constexpr DWORD kMousePollIntervalMs = 300;
HANDLE g_mouseStopEvent = nullptr;
HANDLE g_mouseThread = nullptr;

// Bit 0: right; bit 1: bottom. Flipping one bit selects an adjacent corner.
enum class Corner : unsigned {
    topLeft = 0,
    topRight = 1,
    bottomLeft = 2,
    bottomRight = 3,
};

struct Placement {
    HorizontalPlacement horizontal;
    VerticalPlacement vertical;
};

bool IsCornerPlacement(const Settings& settings) {
    return settings.horizontalPlacement != HorizontalPlacement::center &&
           settings.verticalPlacement != VerticalPlacement::center;
}

Corner ConfiguredCorner(const Settings& settings) {
    return static_cast<Corner>(
        (settings.horizontalPlacement == HorizontalPlacement::right ? 1u : 0u) |
        (settings.verticalPlacement == VerticalPlacement::bottom ? 2u : 0u));
}

Corner EffectiveCorner(HWND hwnd, const Settings& settings) {
    ULONG_PTR value = reinterpret_cast<ULONG_PTR>(
        GetPropW(hwnd, kAvoidanceCornerProp));
    if (value >= 1 && value <= 4) {
        return static_cast<Corner>(value - 1);
    }
    return ConfiguredCorner(settings);
}

Placement CornerPlacement(Corner corner) {
    unsigned bits = static_cast<unsigned>(corner);
    return {bits & 1u ? HorizontalPlacement::right : HorizontalPlacement::left,
            bits & 2u ? VerticalPlacement::bottom : VerticalPlacement::top};
}

Placement EffectivePlacement(HWND hwnd, const Settings& settings) {
    if (g_unloading) {
        return {HorizontalPlacement::right, VerticalPlacement::bottom};
    }
    if (!IsCornerPlacement(settings)) {
        return {settings.horizontalPlacement, settings.verticalPlacement};
    }
    return CornerPlacement(EffectiveCorner(hwnd, settings));
}

POINT CornerPoint(const RECT& area, Corner corner) {
    unsigned bits = static_cast<unsigned>(corner);
    return {bits & 1u ? area.right - 1 : area.left,
            bits & 2u ? area.bottom - 1 : area.top};
}

double CornerDistanceSquared(POINT cursor, const RECT& area, Corner corner) {
    POINT point = CornerPoint(area, corner);
    double dx = static_cast<double>(cursor.x) - point.x;
    double dy = static_cast<double>(cursor.y) - point.y;
    return dx * dx + dy * dy;
}

Corner ChooseAdjacentCorner(Corner current, POINT cursor, const RECT& area) {
    unsigned bits = static_cast<unsigned>(current);
    Corner horizontal = static_cast<Corner>(bits ^ 1u);
    Corner vertical = static_cast<Corner>(bits ^ 2u);
    return CornerDistanceSquared(cursor, area, horizontal) >=
                   CornerDistanceSquared(cursor, area, vertical)
               ? horizontal
               : vertical;
}

enum class Target {
    Explorer,
    ShellExperienceHost,
};

Target g_target;

bool g_inCToastCenterExperienceManager_PositionView;

thread_local bool g_customAnimationDirectionApplied;

WINUSERAPI UINT WINAPI GetDpiForWindow(HWND hwnd);
typedef enum MONITOR_DPI_TYPE {
    MDT_EFFECTIVE_DPI = 0,
    MDT_ANGULAR_DPI = 1,
    MDT_RAW_DPI = 2,
    MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
STDAPI GetDpiForMonitor(HMONITOR hmonitor,
                        MONITOR_DPI_TYPE dpiType,
                        UINT* dpiX,
                        UINT* dpiY);

HMONITOR GetMonitorById(int monitorId) {
    HMONITOR monitorResult = nullptr;
    int currentMonitorId = 0;

    auto monitorEnumProc = [&](HMONITOR hMonitor) -> BOOL {
        if (currentMonitorId == monitorId) {
            monitorResult = hMonitor;
            return FALSE;
        }
        currentMonitorId++;
        return TRUE;
    };

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor,
           LPARAM dwData) -> BOOL {
            auto& proc = *reinterpret_cast<decltype(monitorEnumProc)*>(dwData);
            return proc(hMonitor);
        },
        reinterpret_cast<LPARAM>(&monitorEnumProc));

    return monitorResult;
}

HMONITOR GetMonitorByInterfaceNameSubstr(PCWSTR interfaceNameSubstr) {
    HMONITOR monitorResult = nullptr;

    auto monitorEnumProc = [&](HMONITOR hMonitor) -> BOOL {
        MONITORINFOEX monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);

        if (GetMonitorInfo(hMonitor, &monitorInfo)) {
            DISPLAY_DEVICE displayDevice = {
                .cb = sizeof(displayDevice),
            };

            if (EnumDisplayDevices(monitorInfo.szDevice, 0, &displayDevice,
                                   EDD_GET_DEVICE_INTERFACE_NAME)) {
                Wh_Log(L"Found display device %s, interface name: %s",
                       monitorInfo.szDevice, displayDevice.DeviceID);

                if (wcsstr(displayDevice.DeviceID, interfaceNameSubstr)) {
                    Wh_Log(L"Matched display device");
                    monitorResult = hMonitor;
                    return FALSE;
                }
            }
        }
        return TRUE;
    };

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor,
           LPARAM dwData) -> BOOL {
            auto& proc = *reinterpret_cast<decltype(monitorEnumProc)*>(dwData);
            return proc(hMonitor);
        },
        reinterpret_cast<LPARAM>(&monitorEnumProc));

    return monitorResult;
}

bool GetMonitorWorkArea(HMONITOR monitor, RECT* rc) {
    MONITORINFO monitorInfo{
        .cbSize = sizeof(MONITORINFO),
    };
    return GetMonitorInfo(monitor, &monitorInfo) &&
           CopyRect(rc, &monitorInfo.rcWork);
}

std::wstring GetProcessFileName(DWORD dwProcessId) {
    HANDLE hProcess =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
    if (!hProcess) {
        return std::wstring{};
    }

    WCHAR processPath[MAX_PATH];

    DWORD dwSize = ARRAYSIZE(processPath);
    if (!QueryFullProcessImageName(hProcess, 0, processPath, &dwSize)) {
        CloseHandle(hProcess);
        return std::wstring{};
    }

    CloseHandle(hProcess);

    PCWSTR processFileName = wcsrchr(processPath, L'\\');
    if (!processFileName) {
        return std::wstring{};
    }

    processFileName++;
    return processFileName;
}

FrameworkElement EnumChildElements(
    FrameworkElement element,
    std::function<bool(FrameworkElement)> enumCallback) {
    int childrenCount = Media::VisualTreeHelper::GetChildrenCount(element);

    for (int i = 0; i < childrenCount; i++) {
        auto child = Media::VisualTreeHelper::GetChild(element, i)
                         .try_as<FrameworkElement>();
        if (!child) {
            Wh_Log(L"Failed to get child %d of %d", i + 1, childrenCount);
            continue;
        }

        if (enumCallback(child)) {
            return child;
        }
    }

    return nullptr;
}

FrameworkElement FindChildByName(FrameworkElement element, PCWSTR name) {
    return EnumChildElements(element, [name](FrameworkElement child) {
        return child.Name() == name;
    });
}

FrameworkElement FindChildByClassName(FrameworkElement element,
                                      PCWSTR className) {
    return EnumChildElements(element, [className](FrameworkElement child) {
        return winrt::get_class_name(child) == className;
    });
}

bool IsTargetCoreWindow(HWND hWnd) {
    DWORD processId = 0;
    if (!hWnd || !GetWindowThreadProcessId(hWnd, &processId)) {
        return false;
    }

    WCHAR szClassName[32];
    if (GetClassName(hWnd, szClassName, ARRAYSIZE(szClassName)) == 0 ||
        _wcsicmp(szClassName, L"Windows.UI.Core.CoreWindow") != 0) {
        return false;
    }

    if (processId != GetCurrentProcessId() || g_target != Target::ShellExperienceHost) {
        if (_wcsicmp(GetProcessFileName(processId).c_str(),
                     L"ShellExperienceHost.exe") != 0) {
            return false;
        }
    }

    // The window title is locale-dependent, and unfortunately I didn't find a
    // simpler way to identify the target window.
    // String source: Windows.UI.ShellCommon.<locale>.pri
    // String resource: \ActionCenter\AC_ToastCenter_Title
    // The strings were collected from here:
    // https://github.com/m417z/windows-language-files

    // clang-format off
    static const std::unordered_set<std::wstring> newNotificationStrings = {
        L"Nuwe kennisgewing", // AF-ZA
        L"አዲስ ማሳወቂያ", // AM-ET
        L"নতুন জাননী", // AS-IN
        L"Yeni bildiriş", // AZ-LATN-AZ
        L"Новае апавяшчэнне", // BE-BY
        L"নতুন বিজ্ঞপ্তি", // BN-IN
        L"Novo obavještenje", // BS-LATN-BA
        L"Notificació nova", // CA-ES-VALENCIA
        L"ᎢᏤᎢ ᎧᏃᎮᏓ", // CHR-CHER-US
        L"Hysbysiad newydd", // CY-GB
        L"اعلان جدید", // FA-IR
        L"Bagong notification", // FIL-PH
        L"Fógra nua", // GA-IE
        L"Brath ùr", // GD-GB
        L"નવી સૂચના", // GU-IN
        L"नई अधिसूचना", // HI-IN
        L"Նոր ծանուցում", // HY-AM
        L"Ný tilkynning", // IS-IS
        L"ახალი შეტყობინება", // KA-GE
        L"Жаңа хабарландыру", // KK-KZ
        L"ការ\u200bជូន\u200bដំណឹង\u200bថ្មី", // KM-KH
        L"ಹೊಸ ಪ್ರಕಟಣೆ", // KN-IN
        L"नवी अधिसुचोवणी", // KOK-IN
        L"Nei Notifikatioun", // LB-LU
        L"ການແຈ້ງເຕືອນໃໝ່", // LO-LA
        L"Whakamōhiotanga hōu", // MI-NZ
        L"Ново известување", // MK-MK
        L"പുതിയ അറിയിപ്പ്", // ML-IN
        L"नवीन सूचना", // MR-IN
        L"Pemberitahuan baharu", // MS-MY
        L"Notifika ġdida", // MT-MT
        L"नयाँ सूचना", // NE-NP
        L"Nytt varsel", // NN-NO
        L"ନୂତନ ବିଜ୍ଞପ୍ତି", // OR-IN
        L"ਨਵੀਂ ਸੂਚਨਾ", // PA-IN
        L"Musuq willana", // QUZ-PE
        L"Njoftim i ri", // SQ-AL
        L"Ново обавјештење", // SR-CYRL-BA
        L"Ново обавештење", // SR-CYRL-RS
        L"புதிய அறிவிப்பு", // TA-IN
        L"కొత్త నోటిఫికేషన్", // TE-IN
        L"Яңа белдерү", // TT-RU
        L"يېڭى ئۇقتۇرۇش", // UG-CN
        L"نئی اطلاع", // UR-PK
        L"Yangi xabarnoma", // UZ-LATN-UZ
        L"\u200f\u200fإعلام جديد", // AR-SA
        L"Ново известие", // BG-BG
        L"Notificació nova", // CA-ES
        L"Nové oznámení", // CS-CZ
        L"Ny meddelelse", // DA-DK
        L"Neue Benachrichtigung", // DE-DE
        L"Νέα ειδοποίηση", // EL-GR
        L"New notification", // EN-GB
        L"New notification", // EN-US
        L"Notificación nueva", // ES-ES
        L"Nueva notificación", // ES-MX
        L"Uus teatis", // ET-EE
        L"Jakinarazpen berria", // EU-ES
        L"Uusi ilmoitus", // FI-FI
        L"Nouvelle notification", // FR-CA
        L"Nouvelle notification", // FR-FR
        L"Nova notificación", // GL-ES
        L"הודעה חדשה", // HE-IL
        L"Nova obavijesti", // HR-HR
        L"Új értesítés", // HU-HU
        L"Pemberitahuan baru", // ID-ID
        L"Nuova notifica", // IT-IT
        L"新しい通知", // JA-JP
        L"새 알림", // KO-KR
        L"Naujas pranešimas", // LT-LT
        L"Jauns paziņojums", // LV-LV
        L"Ny varsling", // NB-NO
        L"Nieuwe melding", // NL-NL
        L"Nowe powiadomienie", // PL-PL
        L"Nova notificação", // PT-BR
        L"Nova notificação", // PT-PT
        L"Notificare nouă", // RO-RO
        L"Новое уведомление", // RU-RU
        L"Nové oznámenie", // SK-SK
        L"Novo obvestilo", // SL-SI
        L"Novo obaveštenje", // SR-LATN-RS
        L"Nytt meddelande", // SV-SE
        L"การแจ้งให้ทราบใหม่", // TH-TH
        L"Yeni bildirim", // TR-TR
        L"Нове сповіщення", // UK-UA
        L"Thông báo mới", // VI-VN
        L"新通知", // ZH-CN
        L"新通知", // ZH-TW
    };
    // clang-format on

    WCHAR szWindowText[256]{};
    if (GetWindowText(hWnd, szWindowText, ARRAYSIZE(szWindowText)) == 0 ||
        !newNotificationStrings.contains(szWindowText)) {
        Wh_Log(L"Not targeting CoreWindow, window text: %s", szWindowText);
        return false;
    }

    return true;
}

std::vector<HWND> GetCoreWindows() {
    struct ENUM_WINDOWS_PARAM {
        std::vector<HWND>* hWnds;
    };

    std::vector<HWND> hWnds;
    ENUM_WINDOWS_PARAM param = {&hWnds};
    EnumWindows(
        [](HWND hWnd, LPARAM lParam) -> BOOL {
            ENUM_WINDOWS_PARAM& param = *(ENUM_WINDOWS_PARAM*)lParam;

            DWORD pid = 0;
            GetWindowThreadProcessId(hWnd, &pid);
            if (pid == GetCurrentProcessId() && IsTargetCoreWindow(hWnd)) {
                param.hWnds->push_back(hWnd);
            }

            return TRUE;
        },
        (LPARAM)&param);

    return hWnds;
}

void AdjustCoreWindowPos(HWND hwnd, int* x, int* y, int* cx, int* cy) {
    const Settings settings = GetSettings();
    const Placement placement = EffectivePlacement(hwnd, settings);
    Wh_Log(L"Before: %dx%d %dx%d", *x, *y, *cx, *cy);

    RECT rc{
        .left = *x,
        .top = *y,
        .right = *x + *cx,
        .bottom = *y + *cy,
    };
    HMONITOR srcMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);

    UINT srcMonitorDpiX = 96;
    UINT srcMonitorDpiY = 96;
    GetDpiForMonitor(srcMonitor, MDT_DEFAULT, &srcMonitorDpiX, &srcMonitorDpiY);

    RECT srcMonitorWorkArea;
    if (!GetMonitorWorkArea(srcMonitor, &srcMonitorWorkArea)) {
        return;
    }

    HMONITOR destMonitor = nullptr;

    if (!g_unloading) {
        if (!settings.monitorInterfaceName.empty()) {
            destMonitor = GetMonitorByInterfaceNameSubstr(
                settings.monitorInterfaceName.c_str());
        } else if (settings.monitor == 0) {
            POINT pt;
            GetCursorPos(&pt);
            destMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        } else if (settings.monitor >= 1) {
            destMonitor = GetMonitorById(settings.monitor - 1);
        }
    }

    if (!destMonitor) {
        HMONITOR primaryMonitor =
            MonitorFromPoint({0, 0}, MONITOR_DEFAULTTONEAREST);

        destMonitor = primaryMonitor;
    }

    RECT destMonitorWorkArea;
    int horizontalDistanceFromScreenEdge = 0;
    int verticalDistanceFromScreenEdge = 0;

    Wh_Log(L"Monitor %p->%p", srcMonitor, destMonitor);

    if (destMonitor != srcMonitor) {
        UINT destMonitorDpiX = 96;
        UINT destMonitorDpiY = 96;
        GetDpiForMonitor(destMonitor, MDT_DEFAULT, &destMonitorDpiX,
                         &destMonitorDpiY);

        if (!GetMonitorWorkArea(destMonitor, &destMonitorWorkArea)) {
            return;
        }

        *cx = MulDiv(*cx, destMonitorDpiX, srcMonitorDpiX);
        if (*y + *cy == srcMonitorWorkArea.bottom) {
            *y = destMonitorWorkArea.bottom -
                 MulDiv(*cy, destMonitorDpiY, srcMonitorDpiY);
            *cy = MulDiv(*cy, destMonitorDpiY, srcMonitorDpiY);
        } else {
            *cy = MulDiv(*cy, destMonitorDpiY, srcMonitorDpiY);
        }

        if (*y == destMonitorWorkArea.top &&
            *y + *cy > destMonitorWorkArea.bottom) {
            *cy = destMonitorWorkArea.bottom - destMonitorWorkArea.top;
        }

        if (!g_unloading) {
            horizontalDistanceFromScreenEdge =
                MulDiv(settings.horizontalDistanceFromScreenEdge,
                       destMonitorDpiX, 96);
            verticalDistanceFromScreenEdge = MulDiv(
                settings.verticalDistanceFromScreenEdge, destMonitorDpiY, 96);
        }
    } else {
        CopyRect(&destMonitorWorkArea, &srcMonitorWorkArea);

        if (!g_unloading) {
            horizontalDistanceFromScreenEdge =
                MulDiv(settings.horizontalDistanceFromScreenEdge,
                       srcMonitorDpiX, 96);
            verticalDistanceFromScreenEdge = MulDiv(
                settings.verticalDistanceFromScreenEdge, srcMonitorDpiY, 96);
        }
    }

    switch (placement.horizontal) {
        case HorizontalPlacement::right:
            *x = destMonitorWorkArea.right - *cx -
                 horizontalDistanceFromScreenEdge;
            break;

        case HorizontalPlacement::left:
            *x = destMonitorWorkArea.left + horizontalDistanceFromScreenEdge;
            break;

        case HorizontalPlacement::center:
            *x = destMonitorWorkArea.left +
                 (destMonitorWorkArea.right - destMonitorWorkArea.left - *cx) /
                     2 +
                 horizontalDistanceFromScreenEdge;
            break;
    }

    switch (placement.vertical) {
        case VerticalPlacement::bottom:
            *y = destMonitorWorkArea.bottom - *cy -
                 verticalDistanceFromScreenEdge;
            break;

        case VerticalPlacement::top:
            *y = destMonitorWorkArea.top + verticalDistanceFromScreenEdge;
            break;

        case VerticalPlacement::center:
            *y = destMonitorWorkArea.top +
                 (destMonitorWorkArea.bottom - destMonitorWorkArea.top - *cy) /
                     2 +
                 verticalDistanceFromScreenEdge;
            break;
    }

    Wh_Log(L"After: %dx%d %dx%d", *x, *y, *cx, *cy);
}

using CToastCenterExperienceManager_PositionView_t =
    HRESULT(WINAPI*)(void* pThis);
CToastCenterExperienceManager_PositionView_t
    CToastCenterExperienceManager_PositionView_Original;
HRESULT WINAPI CToastCenterExperienceManager_PositionView_Hook(void* pThis) {
    Wh_Log(L">");

    g_inCToastCenterExperienceManager_PositionView = true;
    HRESULT ret = CToastCenterExperienceManager_PositionView_Original(pThis);
    g_inCToastCenterExperienceManager_PositionView = false;

    return ret;
}

using MonitorFromPoint_t = decltype(&MonitorFromPoint);
MonitorFromPoint_t MonitorFromPoint_Original;
HMONITOR WINAPI MonitorFromPoint_Hook(POINT pt, DWORD dwFlags) {
    Wh_Log(L">");
    if (g_inCToastCenterExperienceManager_PositionView && !g_unloading &&
        pt.x == 0 && pt.y == 0) {
        const Settings settings = GetSettings();
        HMONITOR monitor = nullptr;

        if (!settings.monitorInterfaceName.empty()) {
            monitor = GetMonitorByInterfaceNameSubstr(
                settings.monitorInterfaceName.c_str());
        } else if (settings.monitor == 0) {
            POINT cursorPt;
            GetCursorPos(&cursorPt);
            monitor =
                MonitorFromPoint_Original(cursorPt, MONITOR_DEFAULTTONEAREST);
        } else if (settings.monitor >= 1) {
            monitor = GetMonitorById(settings.monitor - 1);
        }

        if (monitor) {
            return monitor;
        }
    }

    return MonitorFromPoint_Original(pt, dwFlags);
}

int GetAnimationAngle(HWND hwnd, const Settings& settings) {
    const Placement placement = EffectivePlacement(hwnd, settings);
    int angle = 0;

    switch (g_unloading ? AnimationDirection::fromRight
                        : settings.animationDirection) {
        case AnimationDirection::automatic:
            if (placement.horizontal == HorizontalPlacement::center) {
                if (placement.vertical == VerticalPlacement::bottom) {
                    angle = 90;
                } else {
                    angle = -90;
                }
            } else if (placement.horizontal ==
                       HorizontalPlacement::left) {
                angle = 180;
            }
            break;

        case AnimationDirection::fromLeft:
            angle = 180;
            break;

        case AnimationDirection::fromRight:
            break;

        case AnimationDirection::fromTop:
            angle = 90;
            break;

        case AnimationDirection::fromBottom:
            angle = -90;
            break;
    }

    return angle;
}

FrameworkElement GetToastCenterGrid() {
    auto window = Window::Current();
    if (!window) {
        Wh_Log(L"Failed to get current window");
        return nullptr;
    }

    auto windowContent = window.Content().try_as<FrameworkElement>();
    if (!windowContent) {
        Wh_Log(L"Failed to get window content");
        return nullptr;
    }

    FrameworkElement child = windowContent;
    if ((child = FindChildByClassName(
             child, L"Windows.UI.Xaml.Controls.ContentPresenter")) &&
        (child =
             FindChildByClassName(child, L"ActionCenter.ToastCenterPage")) &&
        (child = FindChildByName(child, L"ToastCenterMainGrid")) &&
        (child = FindChildByName(child, L"ToastCenterView")) &&
        (child = FindChildByName(child, L"ToastCenterScrollViewer")) &&
        (child = FindChildByName(child, L"Root")) &&
        (child =
             FindChildByClassName(child, L"Windows.UI.Xaml.Controls.Grid")) &&
        (child = FindChildByName(child, L"ScrollContentPresenter")) &&
        (child = FindChildByName(child, L"ToastCenterGrid"))) {
        return child;
    }

    Wh_Log(L"Failed to find ToastCenterGrid");
    return nullptr;
}

struct ToastOpacity {
    winrt::weak_ref<FrameworkElement> grid;
    winrt::Windows::Foundation::IInspectable localOpacity;
    Data::Binding binding{nullptr};
    double baseOpacity;
};

std::vector<ToastOpacity>& ToastOpacitiesForCurrentThread() {
    // The lock protects the map. Only the owning UI thread uses its vector.
    static std::mutex mutex;
    static std::unordered_map<DWORD, std::vector<ToastOpacity>> opacities;
    std::lock_guard lock(mutex);
    return opacities[GetCurrentThreadId()];
}

void RestoreToastOpacities() {
    auto& opacities = ToastOpacitiesForCurrentThread();
    if (opacities.empty()) {
        return;
    }

    auto property = UIElement::OpacityProperty();
    auto unsetValue = DependencyProperty::UnsetValue();
    for (const auto& original : opacities) {
        if (auto grid = original.grid.get()) {
            if (original.binding) {
                grid.SetBinding(property, original.binding);
            } else if (original.localOpacity == unsetValue) {
                grid.ClearValue(property);
            } else {
                grid.SetValue(property, original.localOpacity);
            }
        }
    }
    opacities.clear();
}

void ApplyToastOpacity(const FrameworkElement& grid, int transparency) {
    auto& opacities = ToastOpacitiesForCurrentThread();
    std::erase_if(opacities, [](const ToastOpacity& original) {
        return !original.grid.get();
    });

    auto property = UIElement::OpacityProperty();
    double currentBase =
        winrt::unbox_value<double>(grid.GetAnimationBaseValue(property));
    auto original = std::find_if(
        opacities.begin(), opacities.end(),
        [&](const ToastOpacity& entry) { return entry.grid.get() == grid; });
    bool firstUpdate = original == opacities.end();
    if (firstUpdate) {
        auto expression = grid.GetBindingExpression(property);
        opacities.push_back({
            winrt::make_weak(grid),
            grid.ReadLocalValue(property),
            expression ? expression.ParentBinding() : nullptr,
            currentBase,
        });
        original = std::prev(opacities.end());
    }

    double opacity = original->baseOpacity * ((100 - transparency) / 100.0);
    if (firstUpdate || currentBase != opacity) {
        grid.Opacity(opacity);
    }
}

void UpdateAnimationDirectionStyle(const FrameworkElement& launcherFrame,
                                   int angle) {
    auto origin = winrt::Windows::Foundation::Point{0.5, 0.5};

    bool foundAnyRootGridContent = false;
    EnumChildElements(launcherFrame, [&](FrameworkElement toastView) {
        auto name = toastView.Name();
        if (name.empty()) {
            return false;  // continue enumeration
        }

        if (!name.starts_with(L"FlexibleNormalToastView") &&
            !name.starts_with(L"FlexiblePriorityToastView")) {
            return false;  // continue enumeration
        }

        FrameworkElement mainGrid = FindChildByName(toastView, L"MainGrid");
        if (!mainGrid) {
            return false;  // continue enumeration
        }

        if (FrameworkElement revealGrid =
                FindChildByName(mainGrid, L"RevealGrid")) {
            Wh_Log(L"Applying transform to toast view %s", name.c_str());

            Media::RotateTransform transform;
            transform.Angle(-angle);
            revealGrid.RenderTransform(transform);
            revealGrid.RenderTransformOrigin(origin);

            foundAnyRootGridContent = true;
        }

        // Older Windows 11 versions have both RevealGrid and RevealGrid2
        // (before ~Jul 2026). Newer builds only have RevealGrid.
        if (FrameworkElement revealGrid2 =
                FindChildByName(mainGrid, L"RevealGrid2")) {
            Wh_Log(L"Applying transform to toast view %s", name.c_str());

            Media::RotateTransform transform;
            transform.Angle(-angle);
            revealGrid2.RenderTransform(transform);
            revealGrid2.RenderTransformOrigin(origin);

            foundAnyRootGridContent = true;
        }

        return false;  // continue enumeration to find all matching children
    });

    if (!foundAnyRootGridContent) {
        Wh_Log(L"Failed to find root grid content");
        return;
    }

    Media::RotateTransform transform;
    transform.Angle(angle);
    launcherFrame.RenderTransform(transform);
    launcherFrame.RenderTransformOrigin(origin);

    g_customAnimationDirectionApplied = (angle != 0);
}

void UpdateNotificationStyleOnOwnerThread(HWND hwnd) {
    try {
        const Settings settings = GetSettings();
        int transparency = g_unloading ? 0 : settings.notificationTransparency;
        if (!transparency) {
            RestoreToastOpacities();
        }

        int angle = GetAnimationAngle(hwnd, settings);
        bool updateAnimation = g_customAnimationDirectionApplied || angle != 0;
        if (!transparency && !updateAnimation) {
            return;
        }

        if (auto grid = GetToastCenterGrid()) {
            // The grid contains both regular and priority toasts, including
            // children inserted after the window has been positioned.
            if (transparency) {
                ApplyToastOpacity(grid, transparency);
            }
            if (updateAnimation) {
                UpdateAnimationDirectionStyle(grid, angle);
            }
        }
    } catch (...) {
        Wh_Log(L"Failed to update notification style");
    }
}

void RefreshNotificationStyle(HWND hwnd) {
    DWORD processId = 0;
    DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
    if (!threadId || processId != GetCurrentProcessId()) {
        return;
    }
    if (threadId == GetCurrentThreadId()) {
        UpdateNotificationStyleOnOwnerThread(hwnd);
        return;
    }

    static const UINT message = RegisterWindowMessageW(
        L"Windhawk.notifications-placement-fork.RefreshNotificationStyle");
    if (!message) {
        Wh_Log(L"Failed to register notification style message");
        return;
    }

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (code == HC_ACTION) {
                auto request = reinterpret_cast<const CWPSTRUCT*>(lParam);
                if (request->message == message &&
                    IsTargetCoreWindow(request->hwnd)) {
                    UpdateNotificationStyleOnOwnerThread(request->hwnd);
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        },
        nullptr, threadId);
    if (!hook) {
        Wh_Log(L"Failed to dispatch notification style update");
        return;
    }

    // Wait for the UI-thread update, including restoration before unload.
    SendMessageW(hwnd, message, 0, 0);
    UnhookWindowsHookEx(hook);
}

using SetWindowPos_t = decltype(&SetWindowPos);
SetWindowPos_t SetWindowPos_Original;
BOOL WINAPI SetWindowPos_Hook(HWND hWnd,
                              HWND hWndInsertAfter,
                              int X,
                              int Y,
                              int cx,
                              int cy,
                              UINT uFlags) {
    auto original = [&]() {
        return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy,
                                     uFlags);
    };

    if (!IsTargetCoreWindow(hWnd)) {
        return original();
    }

    Wh_Log(L"%08X %08X", (DWORD)(ULONG_PTR)hWnd, uFlags);

    RECT rc{};
    GetWindowRect(hWnd, &rc);

    // Skip if no size or empty size.
    if ((uFlags & SWP_NOSIZE) || cx == 0 || cy == 0) {
        Wh_Log(L"Skipping");
        uFlags |= SWP_NOMOVE | SWP_NOSIZE;
        BOOL ret = original();
        // The native call may occur just before the next toast is revealed.
        // XAML access must stay on its owning thread, never the polling thread.
        if (g_target == Target::ShellExperienceHost &&
            GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId()) {
            UpdateNotificationStyleOnOwnerThread(hWnd);
        }
        return ret;
    }

    if (uFlags & SWP_NOMOVE) {
        uFlags &= ~SWP_NOMOVE;
        X = rc.left;
        Y = rc.top;
    }

    AdjustCoreWindowPos(hWnd, &X, &Y, &cx, &cy);

    BOOL ret =
        SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);

    if (g_target == Target::ShellExperienceHost &&
        GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId()) {
        UpdateNotificationStyleOnOwnerThread(hWnd);
    }

    return ret;
}

namespace ShellExperienceHost {

void AvoidMouse(HWND hwnd, POINT cursor, HMONITOR cursorMonitor,
                const Settings& settings) {
    if (g_unloading || !IsWindowVisible(hwnd) ||
        !IsCornerPlacement(settings)) {
        return;
    }

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc) || IsRectEmpty(&rc)) {
        return;
    }
    HMONITOR monitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (!monitor || monitor != cursorMonitor) {
        return;
    }

    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_DEFAULT, &dpiX, &dpiY);
    double threshold = settings.mouseAvoidanceDistance * (dpiX / 96.0);
    double dx = static_cast<double>(cursor.x) -
                std::clamp(cursor.x, rc.left, rc.right);
    double dy = static_cast<double>(cursor.y) -
                std::clamp(cursor.y, rc.top, rc.bottom);
    if (dx * dx + dy * dy > threshold * threshold) {
        return;
    }

    RECT area{};
    if (!GetMonitorWorkArea(monitor, &area) || IsRectEmpty(&area)) {
        return;
    }

    Corner current = EffectiveCorner(hwnd, settings);
    Corner next = ChooseAdjacentCorner(current, cursor, area);
    Placement placement = CornerPlacement(next);
    int marginX = MulDiv(settings.horizontalDistanceFromScreenEdge, dpiX, 96);
    int marginY = MulDiv(settings.verticalDistanceFromScreenEdge, dpiY, 96);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int x = placement.horizontal == HorizontalPlacement::right
                ? area.right - width - marginX : area.left + marginX;
    int y = placement.vertical == VerticalPlacement::bottom
                ? area.bottom - height - marginY : area.top + marginY;

    if (x == rc.left && y == rc.top) {
        return;  // No physical room to move in the selected direction.
    }

    // Publish the corner first, so even a concurrent Windows placement request
    // uses the new corner. SetProp is shared by both hooked processes.
    HANDLE oldCorner = GetPropW(hwnd, kAvoidanceCornerProp);
    HANDLE newCorner = reinterpret_cast<HANDLE>(
        static_cast<ULONG_PTR>(next) + 1);
    if (!SetPropW(hwnd, kAvoidanceCornerProp, newCorner)) {
        Wh_Log(L"Mouse avoidance: SetProp failed for %p", hwnd);
        return;
    }

    // Do not call the hook here: its no-size branch intentionally blocks moves.
    // ASYNCWINDOWPOS avoids a synchronous wait for the Shell UI thread.
    if (!SetWindowPos_Original(hwnd, nullptr, x, y, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                               SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS)) {
        if (oldCorner) {
            SetPropW(hwnd, kAvoidanceCornerProp, oldCorner);
        } else {
            RemovePropW(hwnd, kAvoidanceCornerProp);
        }
        Wh_Log(L"Mouse avoidance: SetWindowPos failed for %p", hwnd);
        return;
    }

    Wh_Log(L"Mouse avoidance: %p corner %u -> %u at (%d,%d)", hwnd,
           static_cast<unsigned>(current), static_cast<unsigned>(next), x, y);
}

DWORD WINAPI MouseMonitorThread(void*) {
    // GetCursorPos, GetWindowRect, and monitor work areas must be in the
    // same screen-coordinate space, including on mixed-DPI desktops.
    auto oldDpiContext = SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    while (WaitForSingleObject(g_mouseStopEvent, kMousePollIntervalMs) ==
           WAIT_TIMEOUT) {
        if (g_unloading) {
            break;
        }
        const Settings settings = GetSettings();
        if (!IsCornerPlacement(settings)) {
            continue;
        }
        POINT cursor{};
        if (!GetCursorPos(&cursor)) {
            continue;
        }
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);
        if (!monitor) {
            continue;
        }
        for (HWND hwnd : GetCoreWindows()) {
            if (g_unloading) {
                break;
            }
            AvoidMouse(hwnd, cursor, monitor, settings);
        }
    }
    if (oldDpiContext) {
        SetThreadDpiAwarenessContext(oldDpiContext);
    }
    return 0;
}

void StartMouseMonitor() {
    if (g_mouseThread || g_unloading) {
        return;
    }
    g_mouseStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_mouseStopEvent) {
        Wh_Log(L"Mouse avoidance: failed to create stop event");
        return;
    }
    g_mouseThread = CreateThread(nullptr, 0, MouseMonitorThread, nullptr, 0, nullptr);
    if (!g_mouseThread) {
        Wh_Log(L"Mouse avoidance: failed to create monitoring thread");
        CloseHandle(g_mouseStopEvent);
        g_mouseStopEvent = nullptr;
    }
}

void StopMouseMonitor() {
    if (g_mouseThread) {
        SetEvent(g_mouseStopEvent);
        WaitForSingleObject(g_mouseThread, INFINITE);
        CloseHandle(g_mouseThread);
        g_mouseThread = nullptr;
    }
    if (g_mouseStopEvent) {
        CloseHandle(g_mouseStopEvent);
        g_mouseStopEvent = nullptr;
    }
}

void ApplySettings() {
    for (HWND hCoreWnd : GetCoreWindows()) {
        Wh_Log(L"Adjusting core window %08X", (DWORD)(ULONG_PTR)hCoreWnd);

        // Reapply the configured starting corner after settings changes, and
        // remove our cross-process state before unloading the mod.
        RemovePropW(hCoreWnd, kAvoidanceCornerProp);
        RECT rc;
        if (!GetWindowRect(hCoreWnd, &rc)) {
            continue;
        }

        int x = rc.left;
        int y = rc.top;
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;

        AdjustCoreWindowPos(hCoreWnd, &x, &y, &cx, &cy);

        SetWindowPos_Original(hCoreWnd, nullptr, x, y, cx, cy,
                              SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshNotificationStyle(hCoreWnd);
    }
}

}  // namespace ShellExperienceHost

bool HookTwinuiPcshellSymbols() {
    HMODULE module = LoadLibraryEx(L"twinui.pcshell.dll", nullptr,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        Wh_Log(L"Loading twinui.pcshell.dll failed");
        return false;
    }

    // twinui.pcshell.dll
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(private: long __cdecl CToastCenterExperienceManager::PositionView(void))"},
            &CToastCenterExperienceManager_PositionView_Original,
            CToastCenterExperienceManager_PositionView_Hook,
        },
    };

    if (!HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks))) {
        Wh_Log(L"HookSymbols failed");
        return false;
    }

    return true;
}

void LoadSettings() {
    Settings settings{};
    settings.monitor = Wh_GetIntSetting(L"monitor");
    PCWSTR monitorName = Wh_GetStringSetting(L"monitorInterfaceName");
    settings.monitorInterfaceName = monitorName ? monitorName : L"";
    Wh_FreeStringSetting(monitorName);

    PCWSTR horizontalPlacement = Wh_GetStringSetting(L"horizontalPlacement");
    settings.horizontalPlacement = HorizontalPlacement::right;
    if (wcscmp(horizontalPlacement, L"left") == 0) {
        settings.horizontalPlacement = HorizontalPlacement::left;
    } else if (wcscmp(horizontalPlacement, L"center") == 0) {
        settings.horizontalPlacement = HorizontalPlacement::center;
    }
    Wh_FreeStringSetting(horizontalPlacement);

    settings.horizontalDistanceFromScreenEdge =
        Wh_GetIntSetting(L"horizontalDistanceFromScreenEdge");

    PCWSTR verticalPlacement = Wh_GetStringSetting(L"verticalPlacement");
    settings.verticalPlacement = VerticalPlacement::bottom;
    if (wcscmp(verticalPlacement, L"top") == 0) {
        settings.verticalPlacement = VerticalPlacement::top;
    } else if (wcscmp(verticalPlacement, L"center") == 0) {
        settings.verticalPlacement = VerticalPlacement::center;
    }
    Wh_FreeStringSetting(verticalPlacement);

    settings.verticalDistanceFromScreenEdge =
        Wh_GetIntSetting(L"verticalDistanceFromScreenEdge");

    settings.mouseAvoidanceDistance =
        std::max<int>(0, Wh_GetIntSetting(L"mouseAvoidanceDistance"));

    settings.notificationTransparency =
        std::clamp<int>(Wh_GetIntSetting(L"notificationTransparency"), 0, 100);

    PCWSTR animationDirection = Wh_GetStringSetting(L"animationDirection");
    settings.animationDirection = AnimationDirection::automatic;
    if (wcscmp(animationDirection, L"fromLeft") == 0) {
        settings.animationDirection = AnimationDirection::fromLeft;
    } else if (wcscmp(animationDirection, L"fromRight") == 0) {
        settings.animationDirection = AnimationDirection::fromRight;
    } else if (wcscmp(animationDirection, L"fromTop") == 0) {
        settings.animationDirection = AnimationDirection::fromTop;
    } else if (wcscmp(animationDirection, L"fromBottom") == 0) {
        settings.animationDirection = AnimationDirection::fromBottom;
    }
    Wh_FreeStringSetting(animationDirection);

    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = settings;
}

BOOL Wh_ModInit() {
    Wh_Log(L">");

    LoadSettings();

    g_target = Target::Explorer;

    WCHAR moduleFilePath[MAX_PATH];
    switch (
        GetModuleFileName(nullptr, moduleFilePath, ARRAYSIZE(moduleFilePath))) {
        case 0:
        case ARRAYSIZE(moduleFilePath):
            Wh_Log(L"GetModuleFileName failed");
            return FALSE;

        default:
            if (PCWSTR moduleFileName = wcsrchr(moduleFilePath, L'\\')) {
                moduleFileName++;
                if (_wcsicmp(moduleFileName, L"ShellExperienceHost.exe") == 0) {
                    g_target = Target::ShellExperienceHost;
                }
            } else {
                Wh_Log(L"GetModuleFileName returned an unsupported path");
                return FALSE;
            }
            break;
    }

    WindhawkUtils::SetFunctionHook(SetWindowPos, SetWindowPos_Hook,
                                   &SetWindowPos_Original);

    if (g_target == Target::Explorer) {
        if (!HookTwinuiPcshellSymbols()) {
            return FALSE;
        }

        WindhawkUtils::SetFunctionHook(MonitorFromPoint, MonitorFromPoint_Hook,
                                       &MonitorFromPoint_Original);
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");

    if (g_target == Target::ShellExperienceHost) {
        ShellExperienceHost::ApplySettings();
        ShellExperienceHost::StartMouseMonitor();
    }
}

void Wh_ModBeforeUninit() {
    Wh_Log(L">");

    g_unloading = true;

    if (g_target == Target::ShellExperienceHost) {
        ShellExperienceHost::StopMouseMonitor();
        ShellExperienceHost::ApplySettings();
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");
}

void Wh_ModSettingsChanged() {
    Wh_Log(L">");

    if (g_target == Target::ShellExperienceHost) {
        ShellExperienceHost::StopMouseMonitor();
    }
    LoadSettings();

    if (g_target == Target::ShellExperienceHost) {
        ShellExperienceHost::ApplySettings();
        ShellExperienceHost::StartMouseMonitor();
    }
}
