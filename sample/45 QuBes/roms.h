#ifndef _ROMS_H
#define _ROMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t os12_rom[]       asm("_binary_os12_rom_start");
extern const uint8_t os12_rom_end[]   asm("_binary_os12_rom_end");

extern const uint8_t basic2_rom[]     asm("_binary_basic2_rom_start");
extern const uint8_t basic2_rom_end[] asm("_binary_basic2_rom_end");

extern const uint8_t dfs1770_rom[]     asm("_binary_dfs1770_rom_start");
extern const uint8_t dfs1770_rom_end[] asm("_binary_dfs1770_rom_end");

#ifdef __cplusplus
}
#endif

#endif