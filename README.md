# Fallout 2 - Miyoo Mini Plus / OnionOS Port

This is a port of [**FOR:CE**](https://github.com/fallout2-ce/fallout2-ce) — the actively maintained
Fallout 2 community engine fork — to the **Miyoo Mini Plus** handheld running **OnionOS**.

Unlike a straightforward port of the original 1998 game, this build inherits everything FOR:CE offers:
native mod support (including the [Restoration Project](https://github.com/BGforgeNet/Fallout2_Restoration_Project)),
a built-in sfall-compatible scripting engine, dozens of quality-of-life settings, and many bug fixes over
vanilla Fallout 2 — all running natively on ARM, with a full D-pad/button control scheme and an on-screen
keyboard for text entry.

## Installation

1. Download the latest release from the [Releases page](../../releases).
2. Copy the `Fallout 2` folder to `/Roms/PORTS/Games/` on your SD card.
3. Copy the `Shortcuts` and `Imgs` folder contents into `/Roms/PORTS/Shortcuts/` and `/Roms/PORTS/Shortcuts/Imgs/` respectively.
4. You need your own legitimate copy of Fallout 2's data files. Copy these into the `Fallout 2` game
   folder on the SD card:
   - `master.dat`
   - `critter.dat`
   - `data/` (if your installation has one)

## Installing with the Restoration Project (RPU) — optional

This port fully supports the [Restoration Project](https://github.com/BGforgeNet/Fallout2_Restoration_Project)
mod, which restores a large amount of cut content. To use it:

1. On a PC, install Fallout 2 normally.
2. Run the RPU installer on top of that same installation.
3. Copy **all** files from that installed folder into the `Fallout 2` game folder on your SD card,
   overwriting when prompted. This includes `master.dat`, `critter.dat`, `ce.dat`, `mods/`, `data/` —
   everything. Since it's hard to know in advance exactly which files RPU needs, copying everything over
   is the simplest way to make sure nothing is missing.

You don't need to do anything else — this port automatically detects and uses RPU if it's present, and
works fine without it if you skip this section entirely. Other mods that don't rely on a Windows-only
`ddraw.dll` (Nevada, Sonora, Party Orders, NPC Armor, and more) should work the same way: drop the mod's
files into the `mods/` folder and list it in `mods/mods_order.txt`.

## Controls

| Button | Without Select | With Select held |
|---|---|---|
| D-pad | Mouse movement | Camera scroll |
| A | Attack | Skilldex |
| B | End turn | Character |
| X | Slow mouse (hold) | Inventory |
| Y | End combat | Pip-Boy |
| L1 | Right click | Quickload |
| R1 | Left click | Quicksave |
| L2 | Switch active item | Automap |
| R2 | Switch item mode | Center on player |
| Start | Enter / OK | — |
| Select | *(modifier)* | — |
| Menu key | Esc / Exit / Return | — |

The Menu key fires Esc on **release**, not on press — this means the OnionOS Menu+Power screenshot
combo won't accidentally exit the game before you can take the screenshot.

**Typing** (character name, save name, etc.): use the D-pad to cycle through letters, D-pad left toggles
upper/lowercase, D-pad right or Select confirms a letter, and L2 deletes.

A Miyoo-specific "Quick Guide" screen with this same control scheme is available from the Options menu
at any time in-game.

## Quality of life benefits over vanilla Fallout

* Party members can loot and barter in place of PC
* Directly equip party members instead of convincing them to use the right equipment
* Ctrl-click (L2, quickly) to move items when bartering, looting, or stealing, and auto-balance caps
* Music continues playing between maps
* Auto open doors
* Integrated "HELP" menu (with a Miyoo-specific control reference screen)
* Last used save slot is remembered
* You can cancel elevator floor selection using the Menu key
* Item/Corpse/Container/Critter highlighting
* Dozens of small things that just work a little better than they did in the original — better
  pathfinding, fewer graphics glitches, less finicky weapon stacking, and much more

> **Note:** A few FOR:CE features that require a screen wider/taller than 640×480 (the 2-column
> inventory, the 4-row barter screen, and other high-resolution UI layouts) are not available on this
> handheld's fixed resolution. Everything else works normally.

CE has broad (though not total) compatibility with [Sfall](https://github.com/sfall-team/sfall)
scripting extensions. Many traditional Fallout mods work out of the box. See
[SFALL_COMPATIBILITY.md](SFALL_COMPATIBILITY.md) for the current compatibility status.

## Credits

- [fallout2-ce/fallout2-ce (FOR:CE)](https://github.com/fallout2-ce/fallout2-ce)
- Original fallout2-ce by Alexander Batalov
- [Restoration Project (RPU)](https://github.com/BGforgeNet/Fallout2_Restoration_Project) by the BGforgeNet community
- Miyoo Mini Plus SDL2 driver by Steward Fu

## License

The source code in this repository is available under the [Sustainable Use License](LICENSE.md).
