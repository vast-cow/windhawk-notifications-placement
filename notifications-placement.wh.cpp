// ==WindhawkMod==
// @id              notifications-placement-fork
// @name            Customize Windows notifications placement - Mouse avoidance
// @description     Move notifications away from the cursor and align display and dismissal animations with their position
// @version         1.2.5
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
the notification moves to an adjacent corner.

Each window chooses its first escape independently. It moves to the adjacent
corner farther from the cursor, with the horizontal neighbor winning an exact
tie. On later escapes, it skips the corner it just left. For example, after
moving from bottom-right to bottom-left, it next moves to top-left.

The setting defaults to 48 device-independent pixels (DIP) and scales with
the notification monitor's DPI. With a value of 0, including negative values
treated as 0, the cursor must be inside or on the edge of the rectangle.
The chosen corner is retained until another avoidance move or a settings
change. Selecting Center on either axis disables mouse avoidance. The monitor,
DPI and edge-distance settings remain in effect.

In Automatic mode, notifications on the left enter from and dismiss to the
left edge, and notifications on the right use the right edge. This applies as
soon as a notification moves to another corner. With centered horizontal
placement, bottom-aligned notifications use the top edge, and other vertical
placements use the bottom edge. Selecting an edge manually uses that edge
for both display and dismissal at any position.

Only Windows 10 64-bit and Windows 11 are supported.

![Screenshot](https://i.imgur.com/4PxMvLg.png)

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
- animationDirection: automatic
  $name: Notification display and dismissal animation direction
  $description: >-
    The edge notifications enter from and dismiss to. Automatic follows the
    current placement, including after mouse avoidance. Choosing an edge
    keeps that direction when the notification moves.
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

#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
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
// Encode both corners of the last move in a small integer (1..16).
constexpr wchar_t kAvoidanceCornerProp[] =
    L"Windhawk.notifications-placement-fork.MouseAvoidanceCorner.v2";
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

struct EscapeMove {
    Corner from;
    Corner to;
};

HANDLE EncodeEscapeMove(Corner from, Corner to) {
    return reinterpret_cast<HANDLE>(1 + 4 * static_cast<ULONG_PTR>(from) +
                                    static_cast<ULONG_PTR>(to));
}

std::optional<EscapeMove> DecodeEscapeMove(HANDLE property) {
    ULONG_PTR value = reinterpret_cast<ULONG_PTR>(property);
    if (value < 1 || value > 16) {
        return std::nullopt;
    }
    --value;
    return EscapeMove{static_cast<Corner>(value / 4),
                      static_cast<Corner>(value % 4)};
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
    auto move = DecodeEscapeMove(GetPropW(hwnd, kAvoidanceCornerProp));
    return CornerPlacement(move ? move->to : ConfiguredCorner(settings));
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

Corner ChooseAdjacentCorner(Corner current, POINT cursor, const RECT& area,
                            std::optional<Corner> cornerToAvoid) {
    unsigned bits = static_cast<unsigned>(current);
    Corner horizontal = static_cast<Corner>(bits ^ 1u);
    Corner vertical = static_cast<Corner>(bits ^ 2u);
    if (cornerToAvoid == horizontal) {
        return vertical;
    }
    if (cornerToAvoid == vertical) {
        return horizontal;
    }
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

struct AnimationRotation {
    winrt::weak_ref<FrameworkElement> element;
    winrt::Windows::Foundation::IInspectable originalTransform;
    winrt::Windows::Foundation::IInspectable originalOrigin;
    Media::RotateTransform rotation;
};

// XAML access stays on the window thread. Weak references let a toast be
// destroyed normally and let us restore live elements outside the visual tree.
thread_local std::vector<AnimationRotation> g_animationRotations;

void SetAnimationRotation(const FrameworkElement& element, int angle) {
    const auto transformProperty = UIElement::RenderTransformProperty();
    const auto localTransform = element.ReadLocalValue(transformProperty);
    auto entry = std::find_if(g_animationRotations.begin(),
                              g_animationRotations.end(), [&](const auto& item) {
        return item.element.get() == element;
    });
    if (entry != g_animationRotations.end() &&
        localTransform == entry->rotation) {
        if (entry->rotation.Angle() != angle) {
            entry->rotation.Angle(angle);
        }
        return;
    }
    if (entry != g_animationRotations.end()) {
        g_animationRotations.erase(entry);
    }

    Media::RotateTransform rotation;
    rotation.Angle(angle);
    g_animationRotations.push_back({
        winrt::make_weak(element),
        localTransform,
        element.ReadLocalValue(UIElement::RenderTransformOriginProperty()),
        rotation,
    });
    element.RenderTransform(rotation);
    element.RenderTransformOrigin({0.5f, 0.5f});
}

void ClearAnimationRotations() {
    if (g_animationRotations.empty()) {
        return;
    }

    const auto transformProperty = UIElement::RenderTransformProperty();
    const auto originProperty = UIElement::RenderTransformOriginProperty();
    const auto unset = DependencyProperty::UnsetValue();
    for (const auto& entry : g_animationRotations) {
        auto element = entry.element.get();
        if (!element ||
            element.ReadLocalValue(transformProperty) != entry.rotation) {
            continue;
        }
        if (entry.originalTransform == unset) {
            element.ClearValue(transformProperty);
        } else {
            element.SetValue(transformProperty, entry.originalTransform);
        }
        if (entry.originalOrigin == unset) {
            element.ClearValue(originProperty);
        } else {
            element.SetValue(originProperty, entry.originalOrigin);
        }
    }
    g_animationRotations.clear();
}

void UpdateAnimationDirectionStyle(HWND hwnd) {
    if (g_unloading) {
        ClearAnimationRotations();
        return;
    }

    const Settings settings = GetSettings();
    const Placement placement = EffectivePlacement(hwnd, settings);
    int angle = 0;

    switch (settings.animationDirection) {
        case AnimationDirection::automatic:
            if (placement.horizontal == HorizontalPlacement::center) {
                if (placement.vertical == VerticalPlacement::bottom) {
                    angle = 90;
                } else {
                    angle = -90;
                }
            } else if (placement.horizontal == HorizontalPlacement::left) {
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

    if (!angle) {
        ClearAnimationRotations();
        return;
    }

    std::erase_if(g_animationRotations,
                  [](const auto& entry) { return !entry.element.get(); });

    auto window = Window::Current();
    if (!window) {
        Wh_Log(L"Failed to get current window");
        return;
    }

    FrameworkElement windowContent = window.Content().as<FrameworkElement>();
    if (!windowContent) {
        Wh_Log(L"Failed to get window content");
        return;
    }

    FrameworkElement launcherFrame = nullptr;

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
        launcherFrame = child;
    }

    if (!launcherFrame) {
        Wh_Log(L"Failed to find launcher frame");
        return;
    }

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

            SetAnimationRotation(revealGrid, -angle);
            foundAnyRootGridContent = true;
        }

        // Older Windows 11 versions have both RevealGrid and RevealGrid2
        // (before ~Jul 2026). Newer builds only have RevealGrid.
        if (FrameworkElement revealGrid2 =
                FindChildByName(mainGrid, L"RevealGrid2")) {
            Wh_Log(L"Applying transform to toast view %s", name.c_str());

            SetAnimationRotation(revealGrid2, -angle);
            foundAnyRootGridContent = true;
        }

        return false;  // continue enumeration to find all matching children
    });

    if (!foundAnyRootGridContent) {
        Wh_Log(L"Failed to find root grid content");
        return;
    }

    SetAnimationRotation(launcherFrame, angle);
}

void TryUpdateAnimationDirectionStyle(HWND hwnd) {
    try {
        UpdateAnimationDirectionStyle(hwnd);
    } catch (...) {
        Wh_Log(L"Failed to update animation direction");
    }
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

    // Skip if no size or empty size.
    if ((uFlags & SWP_NOSIZE) || cx == 0 || cy == 0) {
        Wh_Log(L"Skipping");
        uFlags |= SWP_NOMOVE | SWP_NOSIZE;
    } else {
        if (uFlags & SWP_NOMOVE) {
            RECT rc{};
            GetWindowRect(hWnd, &rc);
            uFlags &= ~SWP_NOMOVE;
            X = rc.left;
            Y = rc.top;
        }
        AdjustCoreWindowPos(hWnd, &X, &Y, &cx, &cy);
    }

    BOOL ret = original();
    // A placement call without a size can precede the next toast's reveal.
    if (ret && g_target == Target::ShellExperienceHost &&
        GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId()) {
        TryUpdateAnimationDirectionStyle(hWnd);
    }

    return ret;
}

namespace ShellExperienceHost {

bool TryAvoidMouse(HWND hwnd) {
    const Settings settings = GetSettings();
    if (g_unloading || !IsWindowVisible(hwnd) || !IsCornerPlacement(settings)) {
        return false;
    }

    POINT cursor{};
    RECT rc{};
    if (!GetCursorPos(&cursor) || !GetWindowRect(hwnd, &rc) ||
        IsRectEmpty(&rc)) {
        return false;
    }
    HMONITOR monitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (!monitor ||
        monitor != MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL)) {
        return false;
    }

    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_DEFAULT, &dpiX, &dpiY);
    double threshold = settings.mouseAvoidanceDistance * (dpiX / 96.0);
    double dx = static_cast<double>(cursor.x) -
                std::clamp(cursor.x, rc.left, rc.right);
    double dy = static_cast<double>(cursor.y) -
                std::clamp(cursor.y, rc.top, rc.bottom);
    if (dx * dx + dy * dy > threshold * threshold) {
        return false;
    }

    RECT area{};
    if (!GetMonitorWorkArea(monitor, &area) || IsRectEmpty(&area)) {
        return false;
    }

    HANDLE oldCorner = GetPropW(hwnd, kAvoidanceCornerProp);
    auto lastMove = DecodeEscapeMove(oldCorner);
    Corner current = lastMove ? lastMove->to : ConfiguredCorner(settings);
    auto cornerToAvoid = lastMove ? std::optional{lastMove->from} : std::nullopt;
    Corner next = ChooseAdjacentCorner(current, cursor, area, cornerToAvoid);
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
        return false;  // No physical room to move in the selected direction.
    }

    // Publish the corner first, so even a concurrent Windows placement request
    // uses the new corner. SetProp is shared by both hooked processes.
    HANDLE newCorner = EncodeEscapeMove(current, next);
    if (!SetPropW(hwnd, kAvoidanceCornerProp, newCorner)) {
        Wh_Log(L"Mouse avoidance: SetProp failed for %p", hwnd);
        return false;
    }

    // Do not call the hook here: its no-size branch intentionally blocks moves.
    if (!SetWindowPos_Original(hwnd, nullptr, x, y, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                               SWP_NOOWNERZORDER)) {
        if (oldCorner) {
            SetPropW(hwnd, kAvoidanceCornerProp, oldCorner);
        } else {
            RemovePropW(hwnd, kAvoidanceCornerProp);
        }
        Wh_Log(L"Mouse avoidance: SetWindowPos failed for %p", hwnd);
        return false;
    }

    Wh_Log(L"Mouse avoidance: %p corner %u -> %u at (%d,%d)", hwnd,
           static_cast<unsigned>(current), static_cast<unsigned>(next), x, y);
    return true;
}

void AvoidMouseAndUpdateAnimation(HWND hwnd) {
    // Keep the cursor, window and work area in the same coordinate space.
    auto oldDpiContext = SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool moved = TryAvoidMouse(hwnd);
    if (oldDpiContext) {
        SetThreadDpiAwarenessContext(oldDpiContext);
    }
    if (moved) {
        TryUpdateAnimationDirectionStyle(hwnd);
    }
}

void ApplySettingsToWindow(HWND hwnd) {
    // Return to the configured starting corner and clear the shared escape
    // history. During unload, AdjustCoreWindowPos uses the Windows defaults.
    RemovePropW(hwnd, kAvoidanceCornerProp);
    RECT rc{};
    BOOL moved = FALSE;
    if (GetWindowRect(hwnd, &rc)) {
        int x = rc.left;
        int y = rc.top;
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;
        AdjustCoreWindowPos(hwnd, &x, &y, &cx, &cy);
        moved = SetWindowPos_Original(hwnd, nullptr, x, y, cx, cy,
                                      SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (moved || g_unloading) {
        TryUpdateAnimationDirectionStyle(hwnd);
    }
}

enum class WindowAction : WPARAM {
    avoidMouse,
    applySettings,
};

UINT g_windowActionMessage;

void HandleWindowAction(HWND hwnd, WindowAction action) {
    switch (action) {
        case WindowAction::avoidMouse:
            AvoidMouseAndUpdateAnimation(hwnd);
            break;
        case WindowAction::applySettings:
            ApplySettingsToWindow(hwnd);
            break;
    }
}

LRESULT CALLBACK WindowActionHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message->message == g_windowActionMessage) {
            HandleWindowAction(message->hwnd,
                               static_cast<WindowAction>(message->wParam));
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void RunWindowAction(HWND hwnd, WindowAction action) {
    DWORD processId = 0;
    DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
    if (!threadId || processId != GetCurrentProcessId()) {
        return;
    }
    if (threadId == GetCurrentThreadId()) {
        HandleWindowAction(hwnd, action);
        return;
    }

    // The hook handles the registered message on the owning UI thread.
    // Synchronous delivery keeps the move and transform in the same callback
    // and leaves no work queued after settings changes or unload.
    HHOOK hook =
        SetWindowsHookExW(WH_CALLWNDPROC, WindowActionHook, nullptr, threadId);
    if (!hook) {
        Wh_Log(L"Failed to dispatch window action for %p", hwnd);
        return;
    }
    SendMessageW(hwnd, g_windowActionMessage, static_cast<WPARAM>(action), 0);
    UnhookWindowsHookEx(hook);
}

DWORD WINAPI MouseMonitorThread(void*) {
    while (WaitForSingleObject(g_mouseStopEvent, kMousePollIntervalMs) ==
           WAIT_TIMEOUT) {
        if (g_unloading) {
            break;
        }
        const Settings settings = GetSettings();
        if (!IsCornerPlacement(settings)) {
            continue;
        }
        for (HWND hwnd : GetCoreWindows()) {
            if (WaitForSingleObject(g_mouseStopEvent, 0) == WAIT_OBJECT_0) {
                break;
            }
            if (IsWindowVisible(hwnd)) {
                RunWindowAction(hwnd, WindowAction::avoidMouse);
            }
        }
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
        // The monitor may already be sending a message to this thread.
        while (MsgWaitForMultipleObjects(1, &g_mouseThread, FALSE, INFINITE,
                                         QS_SENDMESSAGE) == WAIT_OBJECT_0 + 1) {
            MSG msg{};
            PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
        }
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
        RunWindowAction(hCoreWnd, WindowAction::applySettings);
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

    if (g_target == Target::ShellExperienceHost) {
        ShellExperienceHost::g_windowActionMessage = RegisterWindowMessageW(
            L"Windhawk.notifications-placement-fork.WindowAction");
        if (!ShellExperienceHost::g_windowActionMessage) {
            Wh_Log(L"Failed to register notification window action message");
            return FALSE;
        }
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
