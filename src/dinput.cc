#include "dinput.h"

#include "debug.h"
#include "settings.h"
#include "sfall_kb_helpers.h"
#include "svga.h"
#include "win32.h"

namespace fallout {

static int gMouseWheelDeltaX = 0;
static int gMouseWheelDeltaY = 0;
static int mouseWindowMappingWindowWidth = 0;
static int mouseWindowMappingWindowHeight = 0;
static int mouseWindowMappingLogicalWidth = 0;
static int mouseWindowMappingLogicalHeight = 0;
static bool mouseRelativeMode = false;

static void mouseDeviceMapWindowToLogicalPosition(int* x, int* y);

// 0x4E0400
bool directInputInit()
{
    mouseDeviceRefreshWindowMapping();

    if (!mouseDeviceInit()) {
        debugPrint("directInputInit: mouseDeviceInit failed: %s\n", SDL_GetError());
        goto err;
    }

    if (!keyboardDeviceInit()) {
        goto err;
    }

    return true;

err:

    directInputFree();

    return false;
}

// 0x4E0478
void directInputFree()
{
}

bool mouseDeviceUsesRelativeMode()
{
    return mouseRelativeMode;
}

bool mouseDeviceInitMode()
{
    // "Relative mode" means cursor position is owned by the application, and we move based on mouse deltas
    // "Absolute mode" means cursor position is controlled by the OS, and we read it directly.
    // Mouse sensitivity settings can only apply in relative mode.

    bool wantsRelativeMode = gProgramIsActive && (screenIsExclusiveFullscreen() || settings.screen.mouse_lock);

    if (wantsRelativeMode) {
        mouseRelativeMode = true;
        SDL_SetRelativeMouseMode(SDL_TRUE); // Miyoo Mini: ignore failure, unsupported by this driver
        return true;
    }

    SDL_SetRelativeMouseMode(SDL_FALSE); // Miyoo Mini: ignore failure, unsupported by this driver

    mouseRelativeMode = false;
    mouseDeviceRefreshWindowMapping();
    return true;
}

// 0x4E04E8
bool mouseDeviceAcquire()
{
    return true;
}

// 0x4E0514
bool mouseDeviceUnacquire()
{
    return true;
}

// 0x4E053C
bool mouseDeviceGetData(MouseData* mouseState)
{
    // CE: This function is sometimes called outside loops calling `get_input`
    // and subsequently `GNW95_process_message`, so mouse events might not be
    // handled by SDL yet.
    //
    // TODO: Move mouse events processing into `GNW95_process_message` and
    // update mouse position manually.
    SDL_PumpEvents();
    Uint32 buttons = mouseDeviceUsesRelativeMode()
        ? SDL_GetRelativeMouseState(&(mouseState->x), &(mouseState->y))
        : SDL_GetMouseState(&(mouseState->x), &(mouseState->y));
    if (!mouseDeviceUsesRelativeMode()) {
        mouseDeviceMapWindowToLogicalPosition(&(mouseState->x), &(mouseState->y));
    }
    mouseState->buttons[0] = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    mouseState->buttons[1] = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    mouseState->wheelX = gMouseWheelDeltaX;
    mouseState->wheelY = gMouseWheelDeltaY;

    gMouseWheelDeltaX = 0;
    gMouseWheelDeltaY = 0;

    // BEGIN Miyoo Mini D-pad-as-mouse patch
    // The cursor speed is in pixels per second, like a real mouse, so it is
    // the same on every screen. It used to move a fixed number of pixels each
    // time the game read the mouse, so it slowed down wherever the game loop
    // runs slower (the 24 fps text entry screens, busy maps).
    static Uint32 padLastTicks = 0;
    static float padRemainderX = 0.0f;
    static float padRemainderY = 0.0f;
    Uint32 padNow = SDL_GetTicks();
    Uint32 padElapsed = padLastTicks != 0 ? padNow - padLastTicks : 0;
    padLastTicks = padNow;
    if (padElapsed > 100) {
        // Long gap (loading, a pause): do not jump.
        padElapsed = 100;
    }

    const Uint8* padState = SDL_GetKeyboardState(NULL);
    if (!padState[SDL_SCANCODE_RCTRL]) {
        // 360 and 120 pixels per second: the old 6 and 2 pixels per read at
        // 60 reads per second.
        float padSpeed = padState[SDL_SCANCODE_LSHIFT] ? 120.0f : 360.0f;
        int padDirX = (padState[SDL_SCANCODE_RIGHT] ? 1 : 0) - (padState[SDL_SCANCODE_LEFT] ? 1 : 0);
        int padDirY = (padState[SDL_SCANCODE_DOWN] ? 1 : 0) - (padState[SDL_SCANCODE_UP] ? 1 : 0);
        float padDistance = padSpeed * static_cast<float>(padElapsed) / 1000.0f;
        padRemainderX = padDirX != 0 ? padRemainderX + padDirX * padDistance : 0.0f;
        padRemainderY = padDirY != 0 ? padRemainderY + padDirY * padDistance : 0.0f;
        int padStepX = static_cast<int>(padRemainderX);
        int padStepY = static_cast<int>(padRemainderY);
        padRemainderX -= padStepX;
        padRemainderY -= padStepY;
        mouseState->x += padStepX;
        mouseState->y += padStepY;
        if (padState[SDL_SCANCODE_T]) mouseState->buttons[0] = true;
        if (padState[SDL_SCANCODE_E]) mouseState->buttons[1] = true;
    } else {
        padRemainderX = 0.0f;
        padRemainderY = 0.0f;
    }
    // END Miyoo Mini D-pad-as-mouse patch

    return true;
}

// 0x4E05A8
bool keyboardDeviceAcquire()
{
    return true;
}

// 0x4E05D4
bool keyboardDeviceUnacquire()
{
    return true;
}

void miyooResyncKeyState(); // input.cc

// 0x4E05FC
bool keyboardDeviceReset()
{
    SDL_FlushEvents(SDL_KEYDOWN, SDL_TEXTINPUT);
    sfall_kb_clear_synthetic_key_events();
    miyooResyncKeyState();
    return true;
}

// 0x4E0650
bool keyboardDeviceGetData(KeyboardData* keyboardData)
{
    return true;
}

// 0x4E070C
bool mouseDeviceInit()
{
    mouseDeviceRefreshWindowMapping();
    return mouseDeviceInitMode();
}

// 0x4E078C
void mouseDeviceFree()
{
}

// 0x4E07B8
bool keyboardDeviceInit()
{
    return true;
}

// 0x4E0874
void keyboardDeviceFree()
{
}

void handleMouseEvent(SDL_Event* event)
{
    // Mouse movement and buttons are accumulated in SDL itself and will be
    // processed later in `mouseDeviceGetData` via `SDL_GetRelativeMouseState`.

    if (event->type == SDL_MOUSEWHEEL) {
        gMouseWheelDeltaX += event->wheel.x;
        gMouseWheelDeltaY += event->wheel.y;
    }
}

static void mouseDeviceMapWindowToLogicalPosition(int* x, int* y)
{
    if (mouseWindowMappingWindowWidth <= 0 || mouseWindowMappingWindowHeight <= 0) {
        return;
    }

    *x = *x * mouseWindowMappingLogicalWidth / mouseWindowMappingWindowWidth;
    *y = *y * mouseWindowMappingLogicalHeight / mouseWindowMappingWindowHeight;
}

void mouseDeviceRefreshWindowMapping()
{
    // When mouse is in "absolute mode" and scaling is > 1, we need to transform screen coordinates to game coordinates.
    // Cache the window sizes so we have them available in mouseDeviceMapWindowToLogicalPosition.
    // Note: if we add letterboxing or other more complex scaling, we'll have to account for it here.
    mouseWindowMappingLogicalWidth = screenGetWidth();
    mouseWindowMappingLogicalHeight = screenGetHeight();

    if (gSdlWindow != nullptr) {
        SDL_GetWindowSize(gSdlWindow, &mouseWindowMappingWindowWidth, &mouseWindowMappingWindowHeight);
    } else {
        mouseWindowMappingWindowWidth = 0;
        mouseWindowMappingWindowHeight = 0;
    }
}

void mouseDeviceSetLogicalPosition(int x, int y)
{
    if (mouseDeviceUsesRelativeMode()
        || gSdlWindow == nullptr
        || mouseWindowMappingLogicalWidth <= 0
        || mouseWindowMappingLogicalHeight <= 0
        || mouseWindowMappingWindowWidth <= 0
        || mouseWindowMappingWindowHeight <= 0) {
        return;
    }

    int windowX = x * mouseWindowMappingWindowWidth / mouseWindowMappingLogicalWidth;
    int windowY = y * mouseWindowMappingWindowHeight / mouseWindowMappingLogicalHeight;
    SDL_WarpMouseInWindow(gSdlWindow, windowX, windowY);
}

} // namespace fallout
