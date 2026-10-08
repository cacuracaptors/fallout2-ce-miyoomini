#include "miyoo_shutdown.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL.h>

#include "combat.h"
#include "game_dialog.h"
#include "game.h"
#include "input.h"
#include "kb.h"

namespace fallout {

#if defined(__linux__) && !defined(__ANDROID__)

// OnionOS paths.
static const char* const kOnionDir = "/mnt/SDCARD/.tmp_update";
static const char* const kCmdToRunPath = "/mnt/SDCARD/.tmp_update/cmd_to_run.sh";
static const char* const kCheckoffDir = "/mnt/SDCARD/.tmp_update/checkoff";

// Created by OnionOS the moment the device is told to turn off (power button,
// or the sleep timer running out), before it asks the programs to close.
static const char* const kOffOrderFlag = "/tmp/.offOrder";
static const long long kOffOrderCheckIntervalMs = 250;

// In the game folder: present while a power-off save is waiting to be loaded.
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

static bool gOnion = false;
static volatile sig_atomic_t gRequested = 0;
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

static long long nowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
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

static void writeResumeFlag(const char* state)
{
    writeWholeFile(kResumeFlagFile, state, strlen(state));
}

static void cleanupAtExit()
{
    if (gPidPath[0] != '\0') {
        remove(gPidPath);
    }
    if (gCheckoffPath[0] != '\0') {
        remove(gCheckoffPath);
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

void miyooShutdownInit(const char* executablePath)
{
    if (!fileExists(kOnionDir)) {
        return;
    }

    gOnion = true;

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

    atexit(cleanupAtExit);
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

    writeResumeFlag("loading\n");
    gResumeInProgress = true;
    return true;
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

void miyooShutdownHandleInGame(MiyooShutdownSaveProc* save)
{
    if (!gOnion || !gRequested || gBusy) {
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

    if (save()) {
        // Load this save on the next launch, and have OnionOS launch the game
        // again on the next boot.
        writeResumeFlag("resume\n");
        if (gCmdToRun != nullptr) {
            writeWholeFile(kCmdToRunPath, gCmdToRun, gCmdToRunLength);
        }
    }

    finish();
}

void miyooShutdownPoll()
{
    if (!gOnion || gBusy) {
        return;
    }

    if (!gRequested) {
        checkOffOrder();
        if (!gRequested) {
            return;
        }
    }

    noteRequest();
    long long now = nowMs();

    if (gResumeInProgress) {
        // Turned off while the power-off save was still loading: the save is
        // intact, so keep it for the next boot.
        gBusy = true;
        writeResumeFlag("resume\n");
        if (gCmdToRun != nullptr) {
            writeWholeFile(kCmdToRunPath, gCmdToRun, gCmdToRunLength);
        }
        finish();
    }

    if (!gInGame) {
        gBusy = true;
        finish();
    }

    // Conversations cannot be closed without breaking their scripts.
    if (miyooDialogActive()) {
        gBusy = true;
        finish();
    }

    long long limit = isInCombat() ? kWaitInCombatMs : kWaitOutsideCombatMs;
    if (now - gRequestMs > limit) {
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

#endif

} // namespace fallout
