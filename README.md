This is my simple attempt at having something semi-reproducible to start developing with ACE and bartman's vscode amiga plugin.

Provide Amiga roms.
By default the settings.json is to look for amiga-os-130.rom, amigaos-204.rom, and amiga-os-314.rom in the top source directory.
If you use Cloanto roms that need a rom.key file to work, that is also supported if you just put it in the same folder.

Make sure to setup the submodules:
```
git submodule update --init --recursive
```
