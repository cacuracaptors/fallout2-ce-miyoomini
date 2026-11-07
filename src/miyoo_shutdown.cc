#include "miyoo_shutdown.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__) && !defined(__ANDROID__)
#include <linux/fb.h>
// (<linux/input.h> is not included: its KEY_* macros clash with kb.h's enum.)
#endif

#include <SDL.h>

#include "combat.h"
#include "game_dialog.h"
#include "game.h"
#include "input.h"
#include "kb.h"

namespace fallout {

#if defined(__linux__) && !defined(__ANDROID__)

// The few evdev definitions needed (from <linux/input.h>).
struct MiyooInputEvent {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};
static const int kEvKey = 1;
static const int kKeyMax = 0x2ff;
static const int kKeyEsc = 1;
static const int kKeyVolumeDown = 114;
static const int kKeyVolumeUp = 115;
#define MIYOO_EVIOCGKEY(len) _IOC(_IOC_READ, 'E', 0x18, len)

// OnionOS paths.
static const char* const kOnionDir = "/mnt/SDCARD/.tmp_update";
static const char* const kCmdToRunPath = "/mnt/SDCARD/.tmp_update/cmd_to_run.sh";
static const char* const kCheckoffDir = "/mnt/SDCARD/.tmp_update/checkoff";
static const char* const kRunSwitcherFlag = "/mnt/SDCARD/.tmp_update/.runGameSwitcher";
static const char* const kConfigDir = "/mnt/SDCARD/.tmp_update/config/";
static const char* const kKeymapPath = "/mnt/SDCARD/.tmp_update/config/keymap.json";
static const char* const kRomScreenDir = "/mnt/SDCARD/Saves/CurrentProfile/romScreens";
static const char* const kRecentList = "/mnt/SDCARD/Roms/recentlist.json";
static const char* const kRecentHidden = "/mnt/SDCARD/Roms/recentlist-hidden.json";
static const char* const kPrevStatePath = "/tmp/prev_state";
static const char* const kLowBatFlag = "/tmp/.lowBat";
static const char* const kOwnBatteryFlag = "/tmp/hasBatteryDisplay";

// Created by OnionOS the moment the device is told to turn off (power button,
// or the sleep timer running out), before it asks the programs to close.
static const char* const kOffOrderFlag = "/tmp/.offOrder";
static const long long kOffOrderCheckIntervalMs = 250;

// In the game folder: present while a save is waiting to be loaded.
// "resume": written at power off, always loaded on the next launch.
// "switch": written when leaving through the MENU button (Game Switcher,
// back to the menu, previous game); loaded only when the game is opened from
// the Game Switcher (OnionOS leaves "switcher" or "game" in /tmp/prev_state),
// never when it is started from the game list.
static const char* const kResumeFlagFile = "miyoo_resume.txt";

// How long to wait for the game loop before closing without saving: enemies
// can take a while to finish their turns in combat, and open windows are
// closed one at a time.
static const long long kWaitOutsideCombatMs = 6000;
static const long long kWaitInCombatMs = 15000;

// Open windows (inventory, Pip-Boy, character screen, menus, ...) are closed
// by sending ESC, one every this many ms, as the player would.
static const long long kEscapeIntervalMs = 300;
static const int kMaxEscapes = 15;

// OnionOS's in-game MENU actions (keymap.json).
enum {
    kMenuOff = 0,
    kMenuSwitcher = 1,
    kMenuExit = 2,
    kMenuQuick = 3,
    kMenuRaMenu = 4,
};
enum {
    kGestureSingle = 0,
    kGestureLong = 1,
    kGestureDouble = 2,
};
static const long long kMenuLongMs = 700;
static const long long kMenuDoubleMs = 300;

static bool gOnion = false;
static volatile sig_atomic_t gRequested = 0; // power off (SIGTERM, off order, low battery)
static int gMenuKind = 0; // pending MENU request (kMenuSwitcher/Exit/Quick), 0 none
static long long gRequestMs = 0;
static bool gInGame = false;
static bool gBusy = false;
static bool gResumeInProgress = false;
static long long gOffOrderCheckMs = 0;
static bool gOffOrderAtStart = false;
static long long gGameLoopSeenMs = 0;
static long long gLastEscapeMs = 0;
static int gEscapeCount = 0;

static char* gCmdToRun = nullptr;
static size_t gCmdToRunLength = 0;
static char gPidPath[128];
static char gCheckoffPath[256];
static char gRomScreenPath[200];
static int gMenuAction[3] = { kMenuSwitcher, kMenuExit, kMenuQuick }; // single, long, double

// MENU button, read straight from evdev (the same device SDL reads), timed as
// OnionOS's keymon times it for RetroArch.
static int gEvFd = -1;
static unsigned char gDownAny[kKeyMax / 8 + 1];
static bool gMenuDown = false;
static bool gMenuMixed = false;
static bool gMenuLongDone = false;
static bool gMenuSingleWait = false;
static bool gMenuSkipRelease = false;
static long long gMenuPressMs = 0;

// Low battery.
static bool gBatteryLow = false;
static long long gBatteryCheckMs = 0;
static bool gOwnBatteryFlag = false;

// Volume / brightness bars: keymon draws them straight into the screen
// memory (page 0). While they show, the game stops presenting frames and the
// screen is put on page 0, so they stay visible instead of being overwritten
// or hidden by the game's own page flipping.
static bool gOverlayDisabled = false;
static bool gOverlayActive = false;
static bool gOverlayEnded = false;
static long long gOverlayUntil = 0;
static const long long kOverlayHoldMs = 1500;

static long long nowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long realMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void onSigterm(int sig)
{
    (void)sig;
    gRequested = 1;
}

static bool fileExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool writeWholeFile(const char* path, const char* data, size_t length)
{
    char temp[1024];
    snprintf(temp, sizeof(temp), "%s.tmp", path);

    FILE* stream = fopen(temp, "wb");
    if (stream == nullptr) {
        return false;
    }
    bool ok = fwrite(data, 1, length, stream) == length;
    ok = (fflush(stream) == 0) && ok;
    ok = (fsync(fileno(stream)) == 0) && ok;
    ok = (fclose(stream) == 0) && ok;
    if (!ok || rename(temp, path) != 0) {
        remove(temp);
        return false;
    }
    return true;
}

static void touchFile(const char* path)
{
    writeWholeFile(path, "", 0);
}

static void writeResumeFlag(const char* state)
{
    writeWholeFile(kResumeFlagFile, state, strlen(state));
}

static int readIntFile(const char* path, int fallback)
{
    FILE* stream = fopen(path, "r");
    int value = fallback;
    if (stream == nullptr) {
        return fallback;
    }
    if (fscanf(stream, "%d", &value) != 1) {
        value = fallback;
    }
    fclose(stream);
    return value;
}

static const char* const kGovernorPath = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
static const char* const kGovernorsPath = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors";

static int gBoostDepth = 0;
static bool gBoostActive = false;
static char gBoostOriginal[32];

static bool readSysfs(const char* path, char* buffer, size_t size)
{
    FILE* stream = fopen(path, "r");
    if (stream == nullptr) {
        return false;
    }
    size_t length = fread(buffer, 1, size - 1, stream);
    fclose(stream);
    buffer[length] = '\0';
    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == ' ')) {
        buffer[--length] = '\0';
    }
    return length > 0;
}

static bool writeSysfs(const char* path, const char* value)
{
    FILE* stream = fopen(path, "w");
    if (stream == nullptr) {
        return false;
    }
    bool ok = fputs(value, stream) >= 0;
    ok = (fclose(stream) == 0) && ok;
    return ok;
}

static void boostRestore()
{
    if (gBoostActive) {
        writeSysfs(kGovernorPath, gBoostOriginal);
        gBoostActive = false;
    }
}

void miyooBoostBegin()
{
    if (!gOnion) {
        return;
    }
    gBoostDepth++;
    if (gBoostActive) {
        return;
    }

    char current[32];
    char available[256];
    if (!readSysfs(kGovernorPath, current, sizeof(current))
        || !readSysfs(kGovernorsPath, available, sizeof(available))
        || strstr(available, "performance") == nullptr
        || strcmp(current, "performance") == 0) {
        return;
    }

    strcpy(gBoostOriginal, current);
    if (writeSysfs(kGovernorPath, "performance")) {
        gBoostActive = true;
    }
}

void miyooBoostEnd()
{
    if (!gOnion || gBoostDepth == 0) {
        return;
    }
    if (--gBoostDepth == 0) {
        boostRestore();
    }
}

void miyooBoostReset()
{
    gBoostDepth = 0;
    boostRestore();
}

static void cleanupAtExit()
{
    boostRestore();
    if (gPidPath[0] != '\0') {
        remove(gPidPath);
    }
    if (gCheckoffPath[0] != '\0') {
        remove(gCheckoffPath);
    }
    if (gOwnBatteryFlag) {
        remove(kOwnBatteryFlag);
        gOwnBatteryFlag = false;
    }
}

// Ends the process right away: the save (if any) is already on the card,
// and running the normal shutdown code while OnionOS is powering off only
// adds risk.
[[noreturn]] static void finish()
{
    cleanupAtExit();
    sync();
    _exit(0);
}

static bool miyooUiDisabled()
{
    return gameUiIsDisabled();
}

static bool miyooDialogActive()
{
    return _gdialogActive();
}

static void miyooPushEscape()
{
    enqueueInputEvent(KEY_ESCAPE);
}

// Notes when the turn-off request was first seen.
static void noteRequest()
{
    if (gRequestMs == 0) {
        gRequestMs = nowMs();
    }
}

// OnionOS does not always send SIGTERM to the game when the device is turned
// off, so its off order file is checked too (a few times per second).
static void checkOffOrder()
{
    long long now = nowMs();
    if (now - gOffOrderCheckMs < kOffOrderCheckIntervalMs) {
        return;
    }
    gOffOrderCheckMs = now;
    if (!gOffOrderAtStart && fileExists(kOffOrderFlag)) {
        gRequested = 1;
    }
}

// ---------------------------------------------------------------------------
// OnionOS's settings for the MENU button in a game (as keymon reads them).

static int jsonInt(const char* json, const char* key, int fallback)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (p == nullptr) {
        return fallback;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == nullptr) {
        return fallback;
    }
    return atoi(p + 1);
}

static void readMenuSettings()
{
    FILE* stream = fopen(kKeymapPath, "r");
    if (stream != nullptr) {
        char buffer[2048];
        size_t length = fread(buffer, 1, sizeof(buffer) - 1, stream);
        buffer[length] = '\0';
        fclose(stream);
        gMenuAction[kGestureSingle] = jsonInt(buffer, "ingame_single_press", gMenuAction[kGestureSingle]);
        gMenuAction[kGestureLong] = jsonInt(buffer, "ingame_long_press", gMenuAction[kGestureLong]);
        gMenuAction[kGestureDouble] = jsonInt(buffer, "ingame_double_press", gMenuAction[kGestureDouble]);
    }

    char path[256];
    snprintf(path, sizeof(path), "%s.menuInverted", kConfigDir);
    if (fileExists(path)) {
        gMenuAction[kGestureSingle] = kMenuExit;
        gMenuAction[kGestureLong] = kMenuSwitcher;
    }
    snprintf(path, sizeof(path), "%s.noGameSwitcher", kConfigDir);
    if (fileExists(path)) {
        gMenuAction[kGestureSingle] = kMenuExit;
        gMenuAction[kGestureLong] = kMenuOff;
    }
}

// ---------------------------------------------------------------------------
// The Game Switcher's picture: romScreens/<hash>.png, the hash being
// OnionOS's FNV1A_Pippip_Yurii of the game's path in the recent list.

static uint32_t onionHash(const char* str, size_t len)
{
    const uint32_t prime = 591798841u;
    uint64_t h = 14695981039346656037ULL;
    if (len > 8) {
        size_t cycles = ((len - 1) >> 4) + 1;
        const size_t ndhead = len - (cycles << 3);
        for (; cycles--; str += 8) {
            uint64_t a, b;
            memcpy(&a, str, 8);
            memcpy(&b, str + ndhead, 8);
            h = (h ^ a) * prime;
            h = (h ^ b) * prime;
        }
    } else {
        uint64_t a = 0;
        memcpy(&a, str, len);
        h = (h ^ a) * prime;
    }
    const uint32_t h32 = (uint32_t)(h ^ (h >> 32));
    return h32 ^ (h32 >> 16);
}

// Finds the game's line in OnionOS's recent list (the .port shortcut that
// launches it, found by the game's folder name).
static void findRomScreen()
{
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        return;
    }
    const char* dir = strrchr(cwd, '/');
    dir = dir != nullptr ? dir + 1 : cwd;

    FILE* stream = fopen(fileExists(kRecentHidden) ? kRecentHidden : kRecentList, "r");
    if (stream == nullptr) {
        return;
    }

    static char line[8192];
    char path[512];
    while (fgets(line, sizeof(line), stream) != nullptr) {
        const char* typePos = strstr(line, "\"type\":");
        const char* romPos = strstr(line, "\"rompath\":\"");
        if (typePos == nullptr || romPos == nullptr) {
            continue;
        }
        int type = atoi(typePos + 7);
        if (type != 5 && type != 17) {
            continue;
        }
        romPos += 11;
        const char* end = strchr(romPos, '"');
        if (end == nullptr || end - romPos >= (int)sizeof(path)) {
            continue;
        }
        memcpy(path, romPos, (size_t)(end - romPos));
        path[end - romPos] = '\0';

        // "launch:rompath" (entries from the search panel)
        const char* colon = strchr(path, ':');
        const char* rom = colon != nullptr ? colon + 1 : path;

        bool ours = strstr(rom, dir) != nullptr;
        if (!ours) {
            // The .port shortcut names the game's folder inside.
            FILE* shortcut = fopen(rom, "r");
            if (shortcut != nullptr) {
                char buffer[4096];
                size_t length = fread(buffer, 1, sizeof(buffer) - 1, shortcut);
                buffer[length] = '\0';
                fclose(shortcut);
                ours = strstr(buffer, dir) != nullptr;
            }
        }
        if (ours) {
            snprintf(gRomScreenPath, sizeof(gRomScreenPath), "%s/%u.png", kRomScreenDir,
                (unsigned)onionHash(rom, strlen(rom)));
            break;
        }
    }
    fclose(stream);
}

static void writeRomScreen()
{
    if (gRomScreenPath[0] == '\0') {
        return;
    }
    mkdir(kRomScreenDir, 0777);
    miyooSaveScreenPng(gRomScreenPath);
}

// ---------------------------------------------------------------------------
// The previous game, as keymon's quick switch does it: the second game of the
// recent list moves to the top, its command goes into cmd_to_run.sh and
// /tmp/quick_switch makes runtime.sh start it when this game ends.

static bool quickSwitchPrepare()
{
    const char* list = fileExists(kRecentHidden) ? kRecentHidden : kRecentList;
    FILE* stream = fopen(list, "r");
    if (stream == nullptr) {
        return false;
    }

    static char lines[64][2048];
    int count = 0;
    int pick = -1;
    int valid = 0;
    char cmd[2200] = "";
    while (count < 64 && fgets(lines[count], sizeof(lines[count]), stream) != nullptr) {
        const char* line = lines[count++];
        if (pick >= 0) {
            continue;
        }
        const char* typePos = strstr(line, "\"type\":");
        const char* romPos = strstr(line, "\"rompath\":\"");
        if (typePos == nullptr || romPos == nullptr) {
            continue;
        }
        int type = atoi(typePos + 7);
        if (type != 5 && type != 17) {
            continue;
        }
        char rom[1024];
        char launch[1024] = "";
        romPos += 11;
        const char* end = strchr(romPos, '"');
        if (end == nullptr || end - romPos >= (int)sizeof(rom)) {
            continue;
        }
        memcpy(rom, romPos, (size_t)(end - romPos));
        rom[end - romPos] = '\0';
        char* colon = strchr(rom, ':');
        if (colon != nullptr) {
            *colon = '\0';
            snprintf(launch, sizeof(launch), "%s", rom);
            memmove(rom, colon + 1, strlen(colon + 1) + 1);
        } else {
            const char* l = strstr(line, "\"launch\":\"");
            const char* le = l != nullptr ? strchr(l + 10, '"') : nullptr;
            if (le != nullptr && le - (l + 10) < (int)sizeof(launch)) {
                memcpy(launch, l + 10, (size_t)(le - (l + 10)));
                launch[le - (l + 10)] = '\0';
            }
        }
        if (!fileExists(rom) || launch[0] == '\0' || !fileExists(launch)) {
            continue;
        }
        if (valid++ == 1) {
            pick = count - 1;
            snprintf(cmd, sizeof(cmd), "LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so \"%s\" \"%s\"", launch, rom);
        }
    }
    bool complete = feof(stream) != 0;
    fclose(stream);
    if (pick < 0) {
        return false;
    }

    if (pick > 0 && complete) {
        // The picked game moves to the top of the list (a list longer than
        // what was read here is left as it is).
        static char out[64 * 2048];
        size_t length = 0;
        for (int i = -1; i < count; i++) {
            int k = i < 0 ? pick : i;
            if (i == pick) {
                continue;
            }
            size_t l = strlen(lines[k]);
            memcpy(out + length, lines[k], l);
            length += l;
        }
        writeWholeFile(list, out, length);
    }

    if (!writeWholeFile(kCmdToRunPath, cmd, strlen(cmd))) {
        return false;
    }
    touchFile("/tmp/quick_switch");
    touchFile("/tmp/force_auto_load_state");
    return true;
}

// What happens after the game state was saved (or when it could not be saved
// and the game is just closed): the MENU actions' effects on OnionOS.
static void doMenuAction(int action)
{
    if (action == kMenuSwitcher) {
        touchFile(kRunSwitcherFlag);
    } else if (action == kMenuQuick) {
        quickSwitchPrepare();
    }
}

static void putBackCmdToRun()
{
    if (gCmdToRun != nullptr) {
        writeWholeFile(kCmdToRunPath, gCmdToRun, gCmdToRunLength);
    }
}

// ---------------------------------------------------------------------------
// MENU button gestures from evdev.

static bool othersDown()
{
    for (int i = 0; i < (int)sizeof(gDownAny); i++) {
        unsigned char mask = gDownAny[i];
        if (i == kKeyEsc / 8) {
            mask &= (unsigned char)~(1u << (kKeyEsc % 8));
        }
        if (mask != 0) {
            return true;
        }
    }
    return false;
}

static void menuGesture(int gesture)
{
    int action = gMenuAction[gesture];
    if (action != kMenuSwitcher && action != kMenuExit && action != kMenuQuick) {
        return; // off, or RetroArch's own menu: nothing to do here
    }
    if (gMenuKind == 0 && !gRequested) {
        gMenuKind = action;
    }
}

static void overlayTrigger();

static void menuEvents()
{
    if (gEvFd < 0) {
        return;
    }

    MiyooInputEvent events[32];
    for (;;) {
        ssize_t bytes = read(gEvFd, events, sizeof(events));
        if (bytes <= 0) {
            break;
        }
        int count = (int)(bytes / sizeof(events[0]));
        for (int i = 0; i < count; i++) {
            if (events[i].type != kEvKey) {
                continue;
            }
            int code = events[i].code;
            int value = events[i].value;
            long long when = (long long)events[i].time.tv_sec * 1000 + events[i].time.tv_usec / 1000;
            if (when <= 0) {
                when = realMs();
            }

            if (code >= 0 && code < kKeyMax) {
                if (value != 0) {
                    gDownAny[code / 8] |= (unsigned char)(1u << (code % 8));
                } else {
                    gDownAny[code / 8] &= (unsigned char)~(1u << (code % 8));
                }
            }

            if ((code == kKeyVolumeUp || code == kKeyVolumeDown) && value != 0) {
                overlayTrigger();
            }

            if (code == kKeyEsc && value == 1) {
                // MENU pressed
                if (gMenuSingleWait && when - gMenuPressMs <= kMenuDoubleMs) {
                    gMenuSingleWait = false;
                    gMenuSkipRelease = true;
                    menuGesture(kGestureDouble);
                } else {
                    gMenuSkipRelease = false;
                }
                gMenuDown = true;
                gMenuMixed = othersDown();
                gMenuLongDone = false;
                gMenuPressMs = when;
            } else if (code == kKeyEsc && value == 0) {
                // MENU released
                if (gMenuDown && !gMenuMixed && !gMenuLongDone && !gMenuSkipRelease && !othersDown()) {
                    if (gMenuAction[kGestureDouble] != kMenuOff) {
                        gMenuSingleWait = true; // single, unless a second press comes
                    } else {
                        menuGesture(kGestureSingle);
                    }
                }
                gMenuDown = false;
                gMenuSkipRelease = false;
            } else if (code != kKeyEsc && value == 1 && gMenuDown) {
                gMenuMixed = true; // another key with MENU (POWER, volume, ...)
            }
        }
        if (count < 32) {
            break;
        }
    }

    long long now = realMs();
    if (gMenuDown && !gMenuMixed && !gMenuLongDone && !gMenuSkipRelease && now - gMenuPressMs >= kMenuLongMs) {
        gMenuLongDone = true;
        if (!othersDown()) {
            menuGesture(kGestureLong);
        }
    }
    if (gMenuSingleWait && now - gMenuPressMs > kMenuDoubleMs) {
        gMenuSingleWait = false;
        menuGesture(kGestureSingle);
    }
}

static void menuInit()
{
    gEvFd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (gEvFd < 0) {
        return;
    }

    // Events from before the game started (the press that opened the game
    // from the Game Switcher, for one) are not gestures.
    MiyooInputEvent events[32];
    while (read(gEvFd, events, sizeof(events)) > 0) {
    }

    memset(gDownAny, 0, sizeof(gDownAny));
    if (ioctl(gEvFd, MIYOO_EVIOCGKEY(sizeof(gDownAny)), gDownAny) < 0) {
        memset(gDownAny, 0, sizeof(gDownAny));
    }
}

bool miyooMenuKeyOwned()
{
    return gOnion && gEvFd >= 0;
}

// ---------------------------------------------------------------------------
// Volume / brightness bars drawn by keymon.

static void overlayPanToFirstPage()
{
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        return;
    }
    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == 0 && info.yoffset != 0) {
        info.yoffset = 0;
        ioctl(fd, FBIOPAN_DISPLAY, &info);
    }
    close(fd);
}

static void overlayTrigger()
{
    if (gOverlayDisabled) {
        return;
    }
    if (!gOverlayActive) {
        gOverlayActive = true;
        overlayPanToFirstPage();
    }
    gOverlayUntil = nowMs() + kOverlayHoldMs;
}

bool miyooOverlayHold()
{
    if (!gOverlayActive) {
        return false;
    }
    if (nowMs() > gOverlayUntil) {
        gOverlayActive = false;
        gOverlayEnded = true;
        return false;
    }
    return true;
}

bool miyooOverlayJustEnded()
{
    bool ended = gOverlayEnded;
    gOverlayEnded = false;
    return ended;
}

// ---------------------------------------------------------------------------
// Low battery: OnionOS's batmon draws a red icon straight into the screen
// memory, which flickers against the game's page flipping. The game tells
// batmon it shows the battery itself (/tmp/hasBatteryDisplay, as OnionOS's
// own apps do) and draws the same icon with the renderer. Under the "exit at"
// level the game saves and closes as for a power off, and OnionOS shows the
// low battery screen and turns the device off (/tmp/.lowBat).

static void batteryPoll()
{
    long long now = nowMs();
    if (now - gBatteryCheckMs < 2000) {
        return;
    }
    gBatteryCheckMs = now;

    char path[256];
    snprintf(path, sizeof(path), "%s.noBatteryWarning", kConfigDir);
    int warnAt = fileExists(path) ? 0 : readIntFile("/mnt/SDCARD/.tmp_update/config/battery/warnAt", 15);
    snprintf(path, sizeof(path), "%s.noLowBatteryAutoSave", kConfigDir);
    int exitAt = fileExists(path) ? 0 : readIntFile("/mnt/SDCARD/.tmp_update/config/battery/exitAt", 4);

    int percent = readIntFile("/tmp/percBat", -1);
    bool valid = percent >= 0 && percent <= 100; // 500: charging
    gBatteryLow = valid && percent < warnAt;

    if (valid && exitAt > 0 && percent <= exitAt && !gRequested) {
        touchFile(kLowBatFlag);
        gRequested = 1;
    }
}

bool miyooBatteryRects(int width, int height, SDL_Rect* out)
{
    if (!gBatteryLow) {
        return false;
    }

    const float scale = (float)width / 640.0f;
#define MIYOO_PX(v) ((int)((v) * scale + 0.5f))
    int x0 = width - MIYOO_PX(15 + 30 + 4);
    int y0 = MIYOO_PX(15);
    int bw = MIYOO_PX(30);
    int bh = MIYOO_PX(15);
    int hw = MIYOO_PX(4);
    int t = MIYOO_PX(1) > 0 ? MIYOO_PX(1) : 1;
    int sliverW = MIYOO_PX(2) > 0 ? MIYOO_PX(2) : 1;
    if (x0 < 0 || y0 + bh > height) {
        return false;
    }

    out[0] = { x0, y0, bw, t };
    out[1] = { x0, y0 + bh - t, bw, t };
    out[2] = { x0, y0, t, bh };
    out[3] = { x0 + bw - t, y0, t, bh };
    out[4] = { x0 + MIYOO_PX(3), y0 + MIYOO_PX(3), sliverW, bh - 2 * MIYOO_PX(3) };
    out[5] = { x0 + bw, y0 + MIYOO_PX(4), hw, bh - 2 * MIYOO_PX(4) };
#undef MIYOO_PX
    return true;
}

// ---------------------------------------------------------------------------

void miyooShutdownInit(const char* executablePath)
{
    if (!fileExists(kOnionDir)) {
        return;
    }

    gOnion = true;

    // Loading boost for everything from here to the main menu.
    miyooBoostBegin();

    // Should never happen (OnionOS deletes it at boot), but a leftover off
    // order must not close the game: then only SIGTERM counts.
    gOffOrderAtStart = fileExists(kOffOrderFlag);

    // The launcher scripts are terminated too: never die writing to a pipe
    // nobody reads anymore.
    signal(SIGPIPE, SIG_IGN);

    // SDL would turn SIGTERM into an SDL_QUIT event (the game exits on it).
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = onSigterm;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &action, nullptr);

    // OnionOS deletes cmd_to_run.sh (its "game to start on boot") right before
    // asking the game to close; a copy is kept to put it back after a save.
    FILE* stream = fopen(kCmdToRunPath, "rb");
    if (stream != nullptr) {
        char buffer[4096];
        size_t length = fread(buffer, 1, sizeof(buffer), stream);
        fclose(stream);
        if (length > 0 && length < sizeof(buffer)) {
            gCmdToRun = (char*)malloc(length);
            if (gCmdToRun != nullptr) {
                memcpy(gCmdToRun, buffer, length);
                gCmdToRunLength = length;
            }
        }
    }

    const char* name = strrchr(executablePath, '/');
    name = name != nullptr ? name + 1 : executablePath;

    snprintf(gPidPath, sizeof(gPidPath), "/tmp/miyoo-port-%s.pid", name);
    char pid[32];
    int pidLength = snprintf(pid, sizeof(pid), "%d\n", (int)getpid());
    bool pidWritten = writeWholeFile(gPidPath, pid, pidLength);

    // OnionOS runs the scripts in checkoff/ before powering off: this one
    // makes it wait (up to 20 s) until the game has finished saving.
    if (pidWritten && fileExists(kCheckoffDir)) {
        snprintf(gCheckoffPath, sizeof(gCheckoffPath), "%s/miyoo-port-%s.sh", kCheckoffDir, name);
        char script[2048];
        int scriptLength = snprintf(script, sizeof(script),
            "#!/bin/sh\n"
            "# Created by the %s Miyoo Mini port: when the device is turned off\n"
            "# while the game is running, waits (up to 20 s) for the game to finish\n"
            "# saving. Deleted by the game when it closes normally.\n"
            "pid=$(cat \"%s\" 2>/dev/null)\n"
            "[ -n \"$pid\" ] || exit 0\n"
            "i=0\n"
            "while [ -d \"/proc/$pid\" ] && [ $i -lt 200 ]; do\n"
            "    sleep 0.1\n"
            "    i=$((i + 1))\n"
            "done\n"
            "exit 0\n",
            name, gPidPath);
        if (!writeWholeFile(gCheckoffPath, script, scriptLength)) {
            gCheckoffPath[0] = '\0';
        }
    }

    // Game Switcher: the MENU button's actions and the picture OnionOS shows.
    readMenuSettings();
    findRomScreen();
    menuInit();

    // Low battery icon: the game draws it itself.
    if (fileExists("/tmp/percBat") || fileExists(kConfigDir)) {
        touchFile(kOwnBatteryFlag);
        gOwnBatteryFlag = true;
    }

    gOverlayDisabled = fileExists("overlay_off.txt");

    atexit(cleanupAtExit);
}

// Reads the resume flag; true when the save it announces should be loaded now.
static bool resumeFlagAllowsLoad(const char* state)
{
    if (strncmp(state, "switch", 6) == 0) {
        // Only when the game is opened from the Game Switcher.
        char prev[32] = { 0 };
        FILE* stream = fopen(kPrevStatePath, "r");
        if (stream != nullptr) {
            if (fgets(prev, sizeof(prev), stream) != nullptr) {
                prev[strcspn(prev, "\r\n")] = '\0';
            }
            fclose(stream);
        }
        return strcmp(prev, "switcher") == 0 || strcmp(prev, "game") == 0;
    }
    return strncmp(state, "resume", 6) == 0;
}

bool miyooShutdownResumePending()
{
    if (!gOnion || !fileExists(kResumeFlagFile)) {
        return false;
    }

    char state[32] = { 0 };
    FILE* stream = fopen(kResumeFlagFile, "rb");
    if (stream != nullptr) {
        fread(state, 1, sizeof(state) - 1, stream);
        fclose(stream);
    }

    // A previous attempt started loading and never finished (the game
    // crashed): do not try again. (If the save itself is missing, the load
    // just fails and the main menu appears.)
    if (strncmp(state, "loading", 7) == 0) {
        remove(kResumeFlagFile);
        sync();
        return false;
    }

    // A save left by the MENU button is for the Game Switcher only: opened
    // from the game list, the game starts from the beginning.
    if (!resumeFlagAllowsLoad(state)) {
        remove(kResumeFlagFile);
        sync();
        return false;
    }

    writeResumeFlag("loading\n");
    gResumeInProgress = true;
    return true;
}

bool miyooShutdownResumeLikely()
{
    if (!gOnion || !fileExists(kResumeFlagFile)) {
        return false;
    }

    char state[32] = { 0 };
    FILE* stream = fopen(kResumeFlagFile, "rb");
    if (stream != nullptr) {
        fread(state, 1, sizeof(state) - 1, stream);
        fclose(stream);
    }

    return resumeFlagAllowsLoad(state);
}

void miyooShutdownResumeFinished(bool loaded)
{
    if (!gResumeInProgress) {
        return;
    }
    gResumeInProgress = false;
    remove(kResumeFlagFile);
    sync();
    (void)loaded;
}

void miyooShutdownSetInGame(bool inGame)
{
    gInGame = inGame;
}

// The MENU request is given up (the game goes on): the game could not be
// saved now (a conversation, the world map, an enemy turn that does not end).
static void cancelMenuRequest()
{
    gMenuKind = 0;
    gRequestMs = 0;
    gGameLoopSeenMs = 0;
    gLastEscapeMs = 0;
    gEscapeCount = 0;
}

void miyooShutdownHandleInGame(MiyooShutdownSaveProc* save)
{
    if (!gOnion || gBusy) {
        return;
    }

    bool off = gRequested != 0;
    if (!off && gMenuKind == 0) {
        return;
    }

    noteRequest();
    gGameLoopSeenMs = nowMs();

    // A script or an action is running (the player could not save now either):
    // wait for it to finish.
    if (miyooUiDisabled()) {
        return;
    }

    gBusy = true;

    if (!off) {
        // The Game Switcher's picture, from the screen the player sees now.
        writeRomScreen();
    }

    if (save()) {
        if (off) {
            // Load this save on the next launch, and have OnionOS launch the
            // game again on the next boot.
            writeResumeFlag("resume\n");
            putBackCmdToRun();
        } else {
            writeResumeFlag("switch\n");
            doMenuAction(gMenuKind);
        }
        finish();
    }

    if (off) {
        finish();
    }

    // The save failed: stay in the game rather than lose the progress.
    gBusy = false;
    cancelMenuRequest();
}

void miyooShutdownPoll()
{
    if (!gOnion || gBusy) {
        return;
    }

    menuEvents();
    batteryPoll();

    bool off = gRequested != 0;
    if (!off) {
        checkOffOrder();
        off = gRequested != 0;
    }
    bool menu = !off && gMenuKind != 0;
    if (!off && !menu) {
        return;
    }

    noteRequest();
    long long now = nowMs();

    if (gResumeInProgress) {
        // Left while the save was still loading: the save is intact, so keep
        // it for the next launch.
        gBusy = true;
        if (off) {
            writeResumeFlag("resume\n");
            putBackCmdToRun();
        } else {
            writeResumeFlag("switch\n");
            doMenuAction(gMenuKind);
        }
        finish();
    }

    if (!gInGame) {
        if (menu && gGameLoaded) {
            // Between maps or on the world map: it cannot be saved here.
            // Wait for the game loop for a while, then give up.
            if (now - gRequestMs > kWaitOutsideCombatMs) {
                cancelMenuRequest();
            }
            return;
        }
        gBusy = true;
        if (menu) {
            // Main menu, intro, loading: nothing to resume from. A save left
            // by an older session must not be taken for this one.
            remove(kResumeFlagFile);
            doMenuAction(gMenuKind);
        }
        finish();
    }

    // Conversations cannot be closed without breaking their scripts.
    if (miyooDialogActive()) {
        if (menu) {
            cancelMenuRequest();
            return;
        }
        gBusy = true;
        finish();
    }

    long long limit = isInCombat() ? kWaitInCombatMs : kWaitOutsideCombatMs;
    if (now - gRequestMs > limit) {
        if (menu) {
            cancelMenuRequest();
            return;
        }
        gBusy = true;
        finish();
    }

    // The map screen saves as soon as it runs. If it has not run for a while,
    // a window is open over it: close it with ESC.
    if (now - gGameLoopSeenMs >= kEscapeIntervalMs
        && now - gRequestMs >= kEscapeIntervalMs
        && now - gLastEscapeMs >= kEscapeIntervalMs
        && gEscapeCount < kMaxEscapes) {
        miyooPushEscape();
        gLastEscapeMs = now;
        gEscapeCount++;
    }
}

#else

void miyooShutdownInit(const char* executablePath)
{
    (void)executablePath;
}

bool miyooShutdownResumePending()
{
    return false;
}

bool miyooShutdownResumeLikely()
{
    return false;
}

void miyooShutdownResumeFinished(bool loaded)
{
    (void)loaded;
}

void miyooShutdownSetInGame(bool inGame)
{
    (void)inGame;
}

void miyooShutdownHandleInGame(MiyooShutdownSaveProc* save)
{
    (void)save;
}

void miyooShutdownPoll()
{
}

bool miyooMenuKeyOwned()
{
    return false;
}

bool miyooOverlayHold()
{
    return false;
}

bool miyooOverlayJustEnded()
{
    return false;
}

bool miyooBatteryRects(int width, int height, SDL_Rect* out)
{
    (void)width;
    (void)height;
    (void)out;
    return false;
}

void miyooBoostBegin()
{
}

void miyooBoostEnd()
{
}

void miyooBoostReset()
{
}

#endif

} // namespace fallout
