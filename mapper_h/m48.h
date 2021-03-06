/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef m48_h_
#define m48_h_

void m48init(uint8_t *prgROM, uint32_t prgROMsize, 
			uint8_t *prgRAM, uint32_t prgRAMsize,
			uint8_t *chrROM, uint32_t chrROMsize);
uint8_t m48get8(uint16_t addr, uint8_t val);
void m33set8(uint16_t addr, uint8_t val);
void m48set8(uint16_t addr, uint8_t val);
uint8_t m48chrGet8(uint16_t addr);
void m48chrSet8(uint16_t addr, uint8_t val);
void m48cycle();

#endif
