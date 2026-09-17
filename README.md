# Fallout 2 - Miyoo Mini Plus / OnionOS Port

This is a port of [**FOR:CE**](https://github.com/fallout2-ce/fallout2-ce) - the actively maintained
Fallout 2 community engine fork - to the **Miyoo Mini Plus** handheld running **OnionOS**.

Unlike a straightforward port of the original 1998 game, this build inherits everything FOR:CE offers:
native mod support (including the [Restoration Project](https://github.com/BGforgeNet/Fallout2_Restoration_Project)),
a built-in sfall-compatible scripting engine, dozens of quality-of-life settings, and many bug fixes over
vanilla Fallout 2 - all running natively on ARM, with a full D-pad/button control scheme and an on-device
text entry system for typing.

<p align="center">
  <img src="docs/images/quick-guide.png" alt="In-game Quick Guide showing the Miyoo Mini Plus control scheme" width="480">
  <br>
  <sub>The in-game "Quick Guide" help screen, replaced with the Miyoo Mini Plus control scheme</sub>
</p>

## Installation

1. Download the latest release from the [Releases page](../../releases).
2. Copy the `Fallout 2` folder to `/Roms/PORTS/Games/` on your SD card.
3. Copy the `Shortcuts` and `Imgs` folder contents into `/Roms/PORTS/Shortcuts/` and `/Roms/PORTS/Shortcuts/Imgs/` respectively.
4. You need your own legitimate copy of Fallout 2's data files. Copy these into the `Fallout 2` game
   folder on the SD card:
   - `master.dat`
   - `critter.dat`
   - `data/` (if your installation has one)

> **If you have a previous 1.0.0 release of this port installed:** delete all files from the old
> `Fallout 2` game folder first. That version was built on a different, unmaintained fork, and mixing
> old and new files will cause problems.

> **Important:** do not copy a `fallout2.cfg` from your own installation into the game folder, and
> delete one if it's already there. This port generates its own `fallout2.cfg` tuned for this
> hardware - overwriting it with a standard PC one will break the game.

## Installing with the Restoration Project (RPU) - optional

This port fully supports the [Restoration Project](https://github.com/BGforgeNet/Fallout2_Restoration_Project)
mod, which restores a large amount of cut content - dozens of previously unused areas, quests, and
characters, new companions and expanded dialogue, and many bug fixes beyond what the base engine
already fixes.

> **Important:** if you install RPU on top of an existing save, or add it to this port after already
> starting to play, your old saves will not work correctly. **You must start a new game after
> installing RPU.** This isn't specific to this port - it's true of RPU on any platform.

To install it:

1. On a Windows PC, install Fallout 2 normally.
2. Run the RPU installer on top of that same installation.
3. Copy **all** files from that installed folder into the `Fallout 2` game folder on your SD card,
   overwriting when prompted. This includes `master.dat`, `critter.dat`, `ce.dat`, `mods/`, `data/` -
   everything. Since it's hard to know in advance exactly which files RPU needs, copying everything over
   is the simplest way to make sure nothing is missing. As above, delete any `fallout2.cfg` that comes
   along with it.

You don't need to do anything else - this port automatically detects and uses RPU if it's present, and
works fine without it if you skip this section entirely. Other mods that don't rely on a Windows-only
`ddraw.dll` (Nevada, Sonora, Party Orders, NPC Armor, and more) should work the same way: drop the mod's
files into the `mods/` folder and list it in `mods/mods_order.txt`.

## Configuring quality-of-life settings

Many of the improvements this fork adds over the original game (auto-opening doors, faster ammo
loading, and dozens more) are off by default and need to be turned on by editing `fallout2.cfg` by
hand, the same way you would on PC. Open `fallout2.cfg` in a text editor and change the value after
the `=` sign. For example, to enable auto-opening doors, find this line under the `[ui]` section:

```
auto_open_doors=0
```

...and change it to:

```
auto_open_doors=1
```

The same applies to most other settings in that file - `0` is off, `1` is on (a few settings use other
small numbers for different modes; comments in the
[official example config](https://github.com/fallout2-ce/fallout2-ce/blob/main/EXAMPLE_fallout2.cfg)
explain those).

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
| Start | Enter / OK | - |
| Select | *(modifier)* | - |
| Menu key | Esc / Exit / Return | - |

The Menu key fires Esc on **release**, not on press - this means the OnionOS Menu+Power screenshot
combo won't accidentally exit the game before you can take the screenshot.

### Typing text (character name, save names, etc.)

Since the device has no keyboard, text entry works by cycling through letters directly in the game's
own text field, the same system used in our [Fallout (1997) port](https://github.com/cacuracaptors/fallout1-ce-miyoomini):

- **D-pad Up/Down**: cycles through the alphabet/numbers/space at the current position
- **D-pad Left**: toggles uppercase/lowercase for the current letter
- **D-pad Right**: inserts a space directly and moves to the next position
- **A**: confirms the current letter and moves to the next position
- **B**: deletes the last confirmed letter
- **Start**: confirms the whole text entry (Enter)
- **Menu key**: cancels the whole text entry (Esc)

A Miyoo-specific "Quick Guide" screen with this same control scheme (pictured above) is available from
the Options menu at any time in-game.

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
* Dozens of small things that just work a little better than they did in the original - better
  pathfinding, fewer graphics glitches, less finicky weapon stacking, and much more

> **Note:** A few FOR:CE features that require a screen wider/taller than 640x480 (the 2-column
> inventory, the 4-row barter screen, and other high-resolution UI layouts) are not available on this
> handheld's fixed resolution. Everything else works normally.

CE has broad (though not total) compatibility with [Sfall](https://github.com/sfall-team/sfall)
scripting extensions. Many traditional Fallout mods work out of the box. See
[SFALL_COMPATIBILITY.md](SFALL_COMPATIBILITY.md) for the current compatibility status.

## Known issues

- Audio has a noticeable, constant latency, the same underlying hardware limitation documented in our
  Fallout (1997) port.
- The mouse cursor moves noticeably slower on screens with an open text field (character creation,
  save/load naming, etc.).

## Credits

- [fallout2-ce/fallout2-ce (FOR:CE)](https://github.com/fallout2-ce/fallout2-ce)
- Original fallout2-ce by Alexander Batalov
- [Restoration Project (RPU)](https://github.com/BGforgeNet/Fallout2_Restoration_Project) by the BGforgeNet community
- Miyoo Mini Plus SDL2 driver by Steward Fu

## License

The source code in this repository is available under the [Sustainable Use License](LICENSE.md).
