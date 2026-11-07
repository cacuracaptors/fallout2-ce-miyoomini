#ifndef FALLOUT_MIYOO_SHUTDOWN_H_
#define FALLOUT_MIYOO_SHUTDOWN_H_

// Miyoo Mini / OnionOS integration: when the device is turned off while the
// game is running, OnionOS sends SIGTERM and waits. The game then saves into
// a hidden slot of its own (SAVEGAME\MIYOO01, never one of the player's
// numbered slots), asks OnionOS to launch it again on the next boot, and
// loads that save on the next launch, skipping the intro and the main menu.
//
// Saving only happens where the original game lets the player save (the map
// screen and the player's turn in combat): open windows (inventory, Pip-Boy,
// character screen, menus) are closed first with ESC, and a running enemy
// turn or scripted scene is waited for. In a conversation, on the world map
// or in the main menu the game just closes, as before.
//
// Everything is disabled when the game does not run under OnionOS.

namespace fallout {

typedef bool MiyooShutdownSaveProc();

// Call once at the very start of main(), before SDL_Init.
void miyooShutdownInit(const char* executablePath);

// True once when the previous session was saved at power off and that save
// should be loaded now. Call miyooShutdownResumeFinished() after the load.
bool miyooShutdownResumePending();

// Read-only check (no side effects): true when the next launch is going to
// load the power-off save. Used to skip the splash fades.
bool miyooShutdownResumeLikely();
void miyooShutdownResumeFinished(bool loaded);

// Tells whether the game loop (map screen) is running.
void miyooShutdownSetInGame(bool inGame);

// Call where the game handles the save keys: saves and exits when the device
// is being turned off. Does not return in that case.
void miyooShutdownHandleInGame(MiyooShutdownSaveProc* save);

// Call every frame from the input loop: closes the game without saving when
// it is turned off outside the game loop (or the game loop is not reached
// in time). Does not return in that case.
void miyooShutdownPoll();

// Loading boost: while the game loads (startup, saves, maps) the CPU governor
// is switched to "performance" (each device keeps its own maximum clock), and
// the original governor is put back afterwards. Nested calls are counted.
// miyooBoostReset() restores it at once (menu or map waiting for input).
// Only active under OnionOS; any failure is silent.
void miyooBoostBegin();
void miyooBoostEnd();
void miyooBoostReset();

struct MiyooBoostScope {
    MiyooBoostScope() { miyooBoostBegin(); }
    ~MiyooBoostScope() { miyooBoostEnd(); }
};

} // namespace fallout

#endif /* FALLOUT_MIYOO_SHUTDOWN_H_ */
