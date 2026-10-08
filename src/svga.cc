#include "svga.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "color.h"
#include "config.h"
#include "dinput.h"
#include "draw.h"
#include "game.h"
#include "interface.h"
#include "memory.h"
#include "mouse.h"
#include "movie.h"
#include "scan_unimplemented.h"
#include "settings.h"
#include "text_font.h"
#include "tile.h"
#include "win32.h"
#include "window_manager_private.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();

// screen rect
Rect _scr_size;

// 0x6ACA18 scr_blit
void (*_scr_blit)(unsigned char* src, int src_pitch, int unused, int src_x, int src_y, int src_width, int src_height, int dest_x, int dest_y) = _GNW95_ShowRect;

// 0x6ACA1C zero_mem
void (*_zero_mem)() = nullptr;

SDL_Window* gSdlWindow = nullptr;
SDL_Surface* gSdlSurface = nullptr;
SDL_Renderer* gSdlRenderer = nullptr;
SDL_Texture* gSdlTexture = nullptr;
SDL_Surface* gSdlTextureSurface = nullptr;

// Miyoo Mini: part of gSdlTextureSurface changed since the last present
// (w == 0 means nothing changed). renderPresent() skips presenting when
// nothing changed, which saves CPU and battery on the many static screens
// of the game.
static SDL_Rect gScreenDirtyRect = { 0, 0, 0, 0 };
static Uint32 gLastPresentTicks = 0;

// Safety net: present at least this often even when nothing seems to change.
static const Uint32 kForcedPresentIntervalMs = 250;

static void screenMarkDirty(int x, int y, int width, int height)
{
    if (gSdlTextureSurface == nullptr) {
        return;
    }

    int right = x + width;
    int bottom = y + height;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (right > gSdlTextureSurface->w) {
        right = gSdlTextureSurface->w;
    }
    if (bottom > gSdlTextureSurface->h) {
        bottom = gSdlTextureSurface->h;
    }
    if (right <= x || bottom <= y) {
        return;
    }

    if (gScreenDirtyRect.w != 0) {
        int oldRight = gScreenDirtyRect.x + gScreenDirtyRect.w;
        int oldBottom = gScreenDirtyRect.y + gScreenDirtyRect.h;
        if (gScreenDirtyRect.x < x) {
            x = gScreenDirtyRect.x;
        }
        if (gScreenDirtyRect.y < y) {
            y = gScreenDirtyRect.y;
        }
        if (oldRight > right) {
            right = oldRight;
        }
        if (oldBottom > bottom) {
            bottom = oldBottom;
        }
    }

    gScreenDirtyRect.x = x;
    gScreenDirtyRect.y = y;
    gScreenDirtyRect.w = right - x;
    gScreenDirtyRect.h = bottom - y;
}

static void screenMarkDirtyAll()
{
    if (gSdlTextureSurface != nullptr) {
        screenMarkDirty(0, 0, gSdlTextureSurface->w, gSdlTextureSurface->h);
    }
}

// Miyoo Mini: changes every time the 8-bit screen (gSdlSurface) changes.
static unsigned int gScreenContentVersion = 1;

struct ScreenPaletteRangeCache {
    SDL_Surface* surface;
    unsigned int version;
    int start;
    int count;
    bool used;
};

static ScreenPaletteRangeCache gScreenPaletteRangeCache[4];
static int gScreenPaletteRangeCacheNext = 0;

// Returns true when some pixel of the 8-bit screen uses a palette index in
// [start, start + count). The answer is remembered until the screen changes,
// so color cycling over a static screen costs almost nothing.
static bool screenUsesPaletteRange(int start, int count)
{
    if (gSdlSurface == nullptr || count <= 0) {
        return false;
    }

    if (start <= 0 && start + count >= 256) {
        return true;
    }

    for (int index = 0; index < 4; index++) {
        ScreenPaletteRangeCache* entry = &(gScreenPaletteRangeCache[index]);
        if (entry->surface == gSdlSurface && entry->version == gScreenContentVersion
            && entry->start == start && entry->count == count) {
            return entry->used;
        }
    }

    bool used = false;
    unsigned int low = static_cast<unsigned int>(start);
    unsigned int range = static_cast<unsigned int>(count);
    const unsigned char* row = static_cast<const unsigned char*>(gSdlSurface->pixels);
    for (int y = 0; y < gSdlSurface->h && !used; y++) {
        for (int x = 0; x < gSdlSurface->w; x++) {
            if (static_cast<unsigned int>(row[x]) - low < range) {
                used = true;
                break;
            }
        }
        row += gSdlSurface->pitch;
    }

    ScreenPaletteRangeCache* entry = &(gScreenPaletteRangeCache[gScreenPaletteRangeCacheNext]);
    gScreenPaletteRangeCacheNext = (gScreenPaletteRangeCacheNext + 1) % 4;
    entry->surface = gSdlSurface;
    entry->version = gScreenContentVersion;
    entry->start = start;
    entry->count = count;
    entry->used = used;

    return used;
}

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

// 0x4CAD08 init_mode_320_200
int _init_mode_320_200()
{
    return _GNW95_init_mode_ex(320, 200, 8);
}

// 0x4CAD40 init_mode_320_400
int _init_mode_320_400()
{
    return _GNW95_init_mode_ex(320, 400, 8);
}

// 0x4CAD5C init_mode_640_480_16
int _init_mode_640_480_16()
{
    return -1;
}

// 0x4CAD64 init_mode_640_480
int _init_mode_640_480()
{
    return _init_vesa_mode(640, 480);
}

// 0x4CAD94 init_mode_640_400
int _init_mode_640_400()
{
    return _init_vesa_mode(640, 400);
}

// 0x4CADA8 init_mode_800_600
int _init_mode_800_600()
{
    return _init_vesa_mode(800, 600);
}

// 0x4CADBC init_mode_1024_768
int _init_mode_1024_768()
{
    return _init_vesa_mode(1024, 768);
}

// 0x4CADD0 init_mode_1280_1024
int _init_mode_1280_1024()
{
    return _init_vesa_mode(1280, 1024);
}

// 0x4CADF8
void _get_start_mode_()
{
}

// 0x4CADFC zero_vid_mem
void _zero_vid_mem()
{
    if (_zero_mem) {
        _zero_mem();
    }
}

// 0x4CAE1C GNW95_init_mode_ex
int _GNW95_init_mode_ex(int width, int height, int bpp)
{
    width = settings.screen.resolution_x;
    height = settings.screen.resolution_y;
    int scale = settings.screen.scale;

    // Only allow scaling if resulting game resolution is >= 640x480
    if ((width / scale) < 640 || (height / scale) < 480) {
        scale = 1;
    } else {
        width /= scale;
        height /= scale;
    }

    if (_GNW95_init_window(width, height, settings.screen.windowed, scale) == -1) {
        return -1;
    }

    if (directDrawInit(width, height, bpp) == -1) {
        return -1;
    }

    // macOS seems to require dequeuing NSApp events in order for window to
    // become visible. There is no concrete number of calls required to make
    // it happen. Sadly there is no particular event to watch for because SDL
    // marks window as shown immediately after creation (see
    // `SDL_FinishWindowCreation`).
    for (int i = 0; i < 10; i++) {
        SDL_PumpEvents();
    }

    _scr_size.left = 0;
    _scr_size.top = 0;
    _scr_size.right = width - 1;
    _scr_size.bottom = height - 1;

    _mouse_blit_trans = nullptr;
    _scr_blit = _GNW95_ShowRect;
    _zero_mem = _GNW95_zero_vid_mem;
    _mouse_blit = _GNW95_ShowRect;

    return 0;
}

// 0x4CAECC init_vesa_mode
int _init_vesa_mode(int width, int height)
{
    return _GNW95_init_mode_ex(width, height, 8);
}

// 0x4CAEDC GNW95_init_window
int _GNW95_init_window(int width, int height, WindowMode mode, int scale)
{
    if (gSdlWindow == nullptr) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

        Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

        if (mode == WindowMode::Fullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN;
        } else if (mode == WindowMode::WindowedFullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }

        gSdlWindow = SDL_CreateWindow(gProgramWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width * scale, height * scale, windowFlags);
        if (gSdlWindow == nullptr) {
            return -1;
        }

        if (!createRenderer(width, height)) {
            destroyRenderer();

            SDL_DestroyWindow(gSdlWindow);
            gSdlWindow = nullptr;

            return -1;
        }
    }

    return 0;
}

// 0x4CAF9C GNW95_init_DirectDraw
int directDrawInit(int width, int height, int bpp)
{
    if (gSdlSurface != nullptr) {
        unsigned char* palette = directDrawGetPalette();
        directDrawFree();

        if (directDrawInit(width, height, bpp) == -1) {
            return -1;
        }

        directDrawSetPalette(palette);

        return 0;
    }

    gSdlSurface = SDL_CreateRGBSurface(0, width, height, bpp, 0, 0, 0, 0);
    if (gSdlSurface == nullptr || gSdlSurface->format->palette == nullptr) {
        directDrawFree();
        return -1;
    }

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    return 0;
}

// 0x4CB1B0 GNW95_reset_mode
void directDrawFree()
{
    if (gSdlSurface != nullptr) {
        SDL_FreeSurface(gSdlSurface);
        gSdlSurface = nullptr;
    }
}

// 0x4CB310 GNW95_SetPaletteEntries
void directDrawSetPaletteInRange(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);

        // Miyoo Mini: color cycling changes a few palette entries many times
        // per second. When none of them is on screen the image does not change,
        // so the whole-screen conversion (and the present) is skipped. Later
        // blits already use the new colors.
        if (!screenUsesPaletteRange(start, count)) {
            return;
        }

        screenMarkDirtyAll();
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
    }
}

// 0x4CB568 GNW95_SetPalette
void directDrawSetPalette(unsigned char* palette)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        screenMarkDirtyAll();
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
    }
}

// 0x4CB68C GNW95_GetPalette
unsigned char* directDrawGetPalette()
{
    // 0x6ACA24
    static unsigned char palette[768];

    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color* colors = gSdlSurface->format->palette->colors;

        for (int index = 0; index < 256; index++) {
            SDL_Color* color = &(colors[index]);
            palette[index * 3] = color->r >> 2;
            palette[index * 3 + 1] = color->g >> 2;
            palette[index * 3 + 2] = color->b >> 2;
        }
    }

    return palette;
}

// 0x4CB850 GNW95_ShowRect
void _GNW95_ShowRect(unsigned char* src, int srcPitch, int unused, int srcX, int srcY, int srcWidth, int srcHeight, int destX, int destY)
{
    (void)unused;

    blitBufferToBuffer(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
    screenMarkDirty(destX, destY, srcWidth, srcHeight);
    gScreenContentVersion++;
}

// Clears drawing surface.
//
// 0x4CBBC8 GNW95_zero_vid_mem
void _GNW95_zero_vid_mem()
{
    if (!gProgramIsActive) {
        return;
    }

    unsigned char* surface = (unsigned char*)gSdlSurface->pixels;
    for (int y = 0; y < gSdlSurface->h; y++) {
        memset(surface, 0, gSdlSurface->w);
        surface += gSdlSurface->pitch;
    }

    SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
    screenMarkDirtyAll();
    gScreenContentVersion++;
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&_scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&_scr_size);
}

int screenGetVisibleHeight()
{
    int windowBottomMargin = 0;

    if (!settings.ui.iface_bar_mode) {
        windowBottomMargin = INTERFACE_BAR_HEIGHT;
    }
    return screenGetHeight() - windowBottomMargin;
}

// returns true if the game is running in fullscreen mode, false otherwise (including windowed fullscreen mode)
bool screenIsExclusiveFullscreen()
{
    Uint32 flags = SDL_GetWindowFlags(gSdlWindow);
    return (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN;
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == nullptr) {
        return false;
    }

    if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
        return false;
    }

    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture == nullptr) {
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, nullptr, nullptr, nullptr) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == nullptr) {
        return false;
    }

    // New texture: the first present must upload everything.
    screenMarkDirtyAll();

    return true;
}

static void destroyRenderer()
{
    if (gSdlTextureSurface != nullptr) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = nullptr;
    }

    if (gSdlTexture != nullptr) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = nullptr;
    }

    if (gSdlRenderer != nullptr) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = nullptr;
    }
}

void handleWindowSizeChanged()
{
    movieHandleRendererReset();
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());
    mouseDeviceRefreshWindowMapping();

    // Miyoo Mini: the new texture starts black; copy the current screen into
    // it, or only the parts redrawn afterwards would show up.
    if (gSdlSurface != NULL && gSdlTextureSurface != NULL) {
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
        screenMarkDirtyAll();
    }
}

void renderFpsCounter()
{
    if (!settings.debug.show_fps || gSdlSurface == nullptr || gSdlTextureSurface == nullptr) {
        return;
    }

    static unsigned int sampleStartTicks = 0;
    static int sampleFrames = 0;
    static double fps = 0.0;

    unsigned int now = SDL_GetTicks();
    if (sampleStartTicks == 0) {
        sampleStartTicks = now;
    }

    sampleFrames++;

    unsigned int elapsed = now - sampleStartTicks;
    if (elapsed >= 500) {
        fps = sampleFrames * 1000.0 / elapsed;
        sampleFrames = 0;
        sampleStartTicks = now;
    }

    char text[32];
    snprintf(text, sizeof(text), "FPS: %.1f", fps);

    ScopedFont font(101);

    constexpr int kPadding = 2;
    int textWidth = fontGetStringWidth(text);
    int textHeight = fontGetLineHeight();
    int width = textWidth + kPadding * 2;
    int height = textHeight + kPadding * 2;

    if (width > gSdlSurface->w) {
        width = gSdlSurface->w;
    }

    if (height > gSdlSurface->h) {
        height = gSdlSurface->h;
    }

    bufferFill(static_cast<unsigned char*>(gSdlSurface->pixels), width, height, gSdlSurface->pitch, COLOR_BLACK);
    if (width > kPadding * 2 && height > kPadding * 2) {
        fontDrawText(static_cast<unsigned char*>(gSdlSurface->pixels) + gSdlSurface->pitch * kPadding + kPadding, text, width - kPadding * 2, gSdlSurface->pitch, COLOR_LIGHT_GREY);
    }

    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = width;
    rect.h = height;
    SDL_BlitSurface(gSdlSurface, &rect, gSdlTextureSurface, &rect);
    screenMarkDirty(rect.x, rect.y, rect.w, rect.h);
    gScreenContentVersion++;
}

void renderPresent()
{
    // Miyoo Mini: nothing changed since the last present, so the screen
    // already shows the right image. A movie shown as an overlay changes on
    // its own, so it is always presented.
    Uint32 nowTicks = SDL_GetTicks();
    if (gScreenDirtyRect.w == 0 && !movieDirectOverlayIsActive()
        && nowTicks - gLastPresentTicks < kForcedPresentIntervalMs) {
        return;
    }

    if (gScreenDirtyRect.w != 0) {
        // The whole texture is uploaded: the Miyoo Mini renderer ignores the
        // position of a partial update (it always writes at the top-left corner).
        SDL_UpdateTexture(gSdlTexture, nullptr, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
        gScreenDirtyRect.w = 0;
        gScreenDirtyRect.h = 0;
    }
    SDL_RenderClear(gSdlRenderer);
    SDL_RenderCopy(gSdlRenderer, gSdlTexture, nullptr, nullptr);
    // render movie SDL texture if present
    movieRenderDirectOverlay();
    SDL_RenderPresent(gSdlRenderer);
    gLastPresentTicks = SDL_GetTicks();
}

} // namespace fallout
