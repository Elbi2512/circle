# ESP32Atom SD layout

The emulator uses the existing ESP32 AtoMMC/SD implementation. Its Quatom-compatible per-core directories are:

- `/mmc/Gera`
- `/mmc/Rola`
- `/mmc/Kevo`
- `/mmc/Towa`

The new emulator layer loads its ROM set from `/roms`:

- `/roms/akernel.rom`
- `/roms/abasic.rom`
- `/roms/afloat.rom`
- `/roms/dosrom.rom`
- `/roms/axr1.rom`
- `/roms/ramrom.rom`

Raspberry Pi firmware and Circle-only boot assets from the Quatom tree are intentionally not part of this PlatformIO data layout.