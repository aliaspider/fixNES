/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include "../ppu.h"

static uint8_t *m4_prgROM;
static uint8_t *m4_prgRAM;
static uint8_t *m4_chrROM;
static uint32_t m4_prgROMsize;
static uint32_t m4_prgRAMsize;
static uint32_t m4_chrROMsize;
static uint8_t m4_chrRAM[0x2000];
static uint32_t m4_curPRGBank0;
static uint32_t m4_curPRGBank1;
static uint32_t m4_lastM1PRGBank;
static uint32_t m4_lastPRGBank;
static uint32_t m4_CHRBank[6];
static uint8_t m4_writeAddr;
static uint8_t m4_tmpAddr;
static bool m4_chr_bank_flip;
static bool m4_prg_bank_flip;
static uint8_t m4_irqCtr;
static bool m4_irqEnable;
static bool m4_altirq;
static bool m4_clear;
static uint8_t m4_irqReloadVal;
static uint8_t m4_irqCooldown;
static uint8_t m4_irqStart;
extern bool mapper_interrupt;
static uint16_t m4_prevAddr;
//used externally
uint32_t m4_prgROMadd;
uint32_t m4_chrROMadd;
uint32_t m4_prgROMand;
uint32_t m4_chrROMand;

void m4init(uint8_t *prgROMin, uint32_t prgROMsizeIn, 
			uint8_t *prgRAMin, uint32_t prgRAMsizeIn,
			uint8_t *chrROMin, uint32_t chrROMsizeIn)
{
	m4_prgROM = prgROMin;
	m4_prgROMsize = prgROMsizeIn;
	m4_prgRAM = prgRAMin;
	m4_prgRAMsize = prgRAMsizeIn;
	m4_prgROMand = prgROMsizeIn-1;
	m4_curPRGBank0 = 0;
	m4_curPRGBank1 = 0x2000;
	m4_lastPRGBank = (prgROMsizeIn - 0x2000);
	m4_lastM1PRGBank = m4_lastPRGBank - 0x2000;
	if(chrROMsizeIn > 0)
	{
		m4_chrROM = chrROMin;
		m4_chrROMsize = chrROMsizeIn;
		m4_chrROMand = chrROMsizeIn-1;
	}
	else
	{
		m4_chrROM = m4_chrRAM;
		m4_chrROMsize = 0x2000;
		m4_chrROMand = 0x1FFF;
	}
	memset(m4_chrRAM,0,0x2000);
	memset(m4_CHRBank,0,6*sizeof(uint32_t));
	m4_tmpAddr = 0;
	m4_irqCtr = 0;
	m4_irqStart = 0;
	m4_writeAddr = 0;
	m4_irqEnable = false;
	//can enable MMC3A IRQ behaviour
	m4_altirq = false;
	m4_clear = false;
	m4_irqReloadVal = 0xFF;
	m4_irqCooldown = 0;
	m4_chr_bank_flip = false;
	m4_prg_bank_flip = false;
	m4_prevAddr = 0;
	m4_prgROMadd = 0;
	m4_chrROMadd = 0;
	printf("Mapper 4 inited\n");
}

uint8_t m4get8(uint16_t addr, uint8_t val)
{
	if(addr >= 0x6000 && addr < 0x8000)
		return m4_prgRAM[addr&0x1FFF];
	else if(addr >= 0x8000)
	{
		if(addr < 0xA000)
		{
			if(m4_prg_bank_flip)
				return m4_prgROM[(((m4_lastM1PRGBank+(addr&0x1FFF)))&m4_prgROMand)|m4_prgROMadd];
			else
				return m4_prgROM[(((m4_curPRGBank0<<13)+(addr&0x1FFF))&m4_prgROMand)|m4_prgROMadd];
		}
		else if(addr < 0xC000)
			return m4_prgROM[(((m4_curPRGBank1<<13)+(addr&0x1FFF))&m4_prgROMand)|m4_prgROMadd];
		else if(addr < 0xE000)
		{
			if(m4_prg_bank_flip)
				return m4_prgROM[(((m4_curPRGBank0<<13)+(addr&0x1FFF))&m4_prgROMand)|m4_prgROMadd];
			else
				return m4_prgROM[((m4_lastM1PRGBank+(addr&0x1FFF))&m4_prgROMand)|m4_prgROMadd];
		}
		return m4_prgROM[((m4_lastPRGBank+(addr&0x1FFF))&m4_prgROMand)|m4_prgROMadd];
	}
	return val;
}

extern bool cpuWriteTMP;
void m4set8(uint16_t addr, uint8_t val)
{
	if(addr >= 0x6000 && addr < 0x8000)
	{
		//printf("m4set8 %04x %02x\n", addr, val);
		m4_prgRAM[addr&0x1FFF] = val;
	}
	else if(addr >= 0x8000)
	{
		// mmc1 regs cant be written to
		// with just 1 cpu cycle delay
		//if(cpuWriteTMP)
		//	return;
		//printf("m4set8 %04x %02x\n", addr, val);
		if(addr < 0xA000)
		{
			if((addr&1) == 0)
			{
				m4_chr_bank_flip = ((val&(1<<7)) != 0);
				m4_prg_bank_flip = ((val&(1<<6)) != 0);
				m4_writeAddr = (val&7);
			}
			else
			{
				switch(m4_writeAddr)
				{
					case 0:
						m4_CHRBank[0] = val;
						break;
					case 1:
						m4_CHRBank[1] = val;
						break;
					case 2:
						m4_CHRBank[2] = val;
						break;
					case 3:
						m4_CHRBank[3] = val;
						break;
					case 4:
						m4_CHRBank[4] = val;
						break;
					case 5:
						m4_CHRBank[5] = val;
						break;
					case 6:
						m4_curPRGBank0 = val;
						break;
					case 7:
						m4_curPRGBank1 = val;
						break;
				}
			}
		}
		else if(addr < 0xC000)
		{
			if((addr&1) == 0)
			{
				if(!ppu4Screen)
				{
					if((val&1) == 0)
					{
						//printf("Vertical mode\n");
						ppuSetNameTblVertical();
					}
					else
					{
						//printf("Horizontal mode\n");
						ppuSetNameTblHorizontal();
					}
				}
			}
		}
		else if(addr < 0xE000)
		{
			if((addr&1) == 0)
			{
				//printf("Reload value set to %i\n", val);
				m4_irqReloadVal = val;
				if(m4_altirq)
					m4_clear = true;
			}
			else
			{
				//printf("Reset counter\n");
				m4_irqCtr = 0;
			}
		}
		else
		{
			if((addr&1) == 0)
			{
				m4_irqEnable = false;
				mapper_interrupt = false;
				m4_irqStart = 0;
				//printf("Interrupt disabled\n");
			}
			else
			{
				m4_irqStart = 0;
				m4_irqEnable = true;
				//printf("Interrupt enabled\n");
			}
		}
	}
}

void m4clock(uint16_t addr)
{
	if(addr & 0x1000)
	{
		if(m4_irqCooldown == 0)
		{
			//printf("MMC3 Beep at %i %i\n", curLine, curDot);
			uint8_t oldCtr = m4_irqCtr;
			if(m4_irqCtr == 0 || m4_clear)
				m4_irqCtr = m4_irqReloadVal;
			else
				m4_irqCtr--;
			if((!m4_altirq || oldCtr != 0 || m4_clear) && m4_irqCtr == 0 && m4_irqEnable)
			{
				//printf("MMC3 Tick at %i %i\n", curLine, curDot);
				m4_irqStart = 5; //takes a bit before trigger
				m4_irqEnable = false;
			}
			m4_clear = false;
		}
		//make sure to pass all other sprites
		//before counting up again
		m4_irqCooldown = 20;
	}
}

uint8_t m4chrGet8(uint16_t addr)
{
	//printf("%04x\n",addr);
	m4clock(addr);
	addr &= 0x1FFF;
	if(m4_chr_bank_flip)
		addr ^= 0x1000;
	if(addr < 0x800)
		return m4_chrROM[(((m4_CHRBank[0]<<10)+(addr&0x7FF))&m4_chrROMand)|m4_chrROMadd];
	else if(addr < 0x1000)
		return m4_chrROM[(((m4_CHRBank[1]<<10)+(addr&0x7FF))&m4_chrROMand)|m4_chrROMadd];
	else if(addr < 0x1400)
		return m4_chrROM[(((m4_CHRBank[2]<<10)+(addr&0x3FF))&m4_chrROMand)|m4_chrROMadd];
	else if(addr < 0x1800)
		return m4_chrROM[(((m4_CHRBank[3]<<10)+(addr&0x3FF))&m4_chrROMand)|m4_chrROMadd];
	else if(addr < 0x1C00)
		return m4_chrROM[(((m4_CHRBank[4]<<10)+(addr&0x3FF))&m4_chrROMand)|m4_chrROMadd];
	return m4_chrROM[(((m4_CHRBank[5]<<10)+(addr&0x3FF))&m4_chrROMand)|m4_chrROMadd];
}

void m4chrSet8(uint16_t addr, uint8_t val)
{
	//printf("m4chrSet8 %04x %02x\n", addr, val);
	if(m4_chrROM == m4_chrRAM) //Writable
		m4_chrROM[addr&0x1FFF] = val;
}

void m4cycle()
{
	uint16_t curAddr = ppuGetCurVramAddr();
	if((curAddr & 0x1000) && !(m4_prevAddr & 0x1000))
		m4clock(curAddr);
	m4_prevAddr = curAddr;
	if(m4_irqCooldown)
		m4_irqCooldown--;
	if(m4_irqStart == 1)
	{
		mapper_interrupt = true;
		m4_irqStart = 0;
	}
	else if(m4_irqStart > 1)
		m4_irqStart--;
}

