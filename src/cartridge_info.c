/*
 * cartridge_info.c - cartridge information
 *
 * Copyright (C) 2001-2010 Piotr Fusik
 * Copyright (C) 2001-2020 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "memory.h"
#include "cartridge.h"

cart_t const CARTRIDGES[CARTRIDGE_TYPE_COUNT] = {
	{ "NONE",                                      0 }, /* 0 */
	{ "Standard 8 KB cartridge",                   8 },
	{ "Standard 16 KB cartridge",                 16 },
	{ "OSS two chip 16 KB cartridge (034M)",      16 },
	{ "Standard 32 KB 5200 cartridge",            32 },
	{ "DB 32 KB cartridge",                       32 },
	{ "Two chip 16 KB 5200 cartridge",            16 },
	{ "Bounty Bob 40 KB 5200 cartridge",          40 },
	{ "64 KB Williams cartridge",                 64 },
	{ "Express 64 KB cartridge",                  64 },
	{ "Diamond 64 KB cartridge",                  64 }, /* 10 */
	{ "SpartaDOS X 64 KB cartridge",              64 },
	{ "XEGS 32 KB cartridge",                     32 },
	{ "XEGS 64 KB cartridge (banks 0-7)",         64 },
	{ "XEGS 128 KB cartridge",                   128 },
	{ "OSS one chip 16 KB cartridge",             16 },
	{ "One chip 16 KB 5200 cartridge",            16 },
	{ "Decoded Atrax 128 KB cartridge",          128 },
	{ "Bounty Bob 40 KB cartridge",               40 },
	{ "Standard 8 KB 5200 cartridge",              8 },
	{ "Standard 4 KB 5200 cartridge",              4 }, /* 20 */
	{ "Right slot 8 KB cartridge",                 8 },
	{ "32 KB Williams cartridge",                 32 },
	{ "XEGS 256 KB cartridge",                   256 },
	{ "XEGS 512 KB cartridge",                   512 },
	{ "XEGS 1 MB cartridge",                    1024 },
	{ "MegaCart 16 KB cartridge",                 16 },
	{ "MegaCart 32 KB cartridge",                 32 },
	{ "MegaCart 64 KB cartridge",                 64 },
	{ "MegaCart 128 KB cartridge",               128 },
	{ "MegaCart 256 KB cartridge",               256 }, /* 30 */
	{ "MegaCart 512 KB cartridge",               512 },
	{ "MegaCart 1 MB cartridge",                1024 },
	{ "Switchable XEGS 32 KB cartridge",          32 },
	{ "Switchable XEGS 64 KB cartridge",          64 },
	{ "Switchable XEGS 128 KB cartridge",        128 },
	{ "Switchable XEGS 256 KB cartridge",        256 },
	{ "Switchable XEGS 512 KB cartridge",        512 },
	{ "Switchable XEGS 1 MB cartridge",         1024 },
	{ "Phoenix 8 KB cartridge",                    8 },
	{ "Blizzard 16 KB cartridge",                 16 }, /* 40 */
	{ "Atarimax 128 KB Flash cartridge",         128 },
	{ "Atarimax 1 MB Flash cartridge (old)",    1024 },
	{ "SpartaDOS X 128 KB cartridge",            128 },
	{ "OSS 8 KB cartridge",                        8 },
	{ "OSS two chip 16 KB cartridge (043M)",      16 },
	{ "Blizzard 4 KB cartridge",                   4 },
	{ "AST 32 KB cartridge",                      32 },
	{ "Atrax SDX 64 KB cartridge",                64 },
	{ "Atrax SDX 128 KB cartridge",              128 },
	{ "Turbosoft 64 KB cartridge",                64 }, /* 50 */
	{ "Turbosoft 128 KB cartridge",              128 },
	{ "Ultracart 32 KB cartridge",                32 },
	{ "Low bank 8 KB cartridge",                   8 },
	{ "SIC! 128 KB cartridge",                   128 },
	{ "SIC! 256 KB cartridge",                   256 },
	{ "SIC! 512 KB cartridge",                   512 },
	{ "Standard 2 KB cartridge",                   2 },
	{ "Standard 4 KB cartridge",                   4 },
	{ "Right slot 4 KB cartridge",                 4 },
	{ "Blizzard 32 KB cartridge",                 32 }, /* 60 */
	{ "MegaMax 2 MB cartridge",                 2048 },
	{ "The!Cart 128 MB cartridge",          128*1024 },
	{ "Flash MegaCart 4 MB cartridge",        4*1024 },
	{ "MegaCart 2 MB cartridge",              2*1024 },
	{ "The!Cart 32 MB cartridge",            32*1024 },
	{ "The!Cart 64 MB cartridge",            64*1024 },
	{ "XEGS 64 KB cartridge (banks 8-15)",        64 },
	{ "Atrax 128 KB cartridge",                  128 },
	{ "aDawliah 32 KB cartridge",                 32 },
	{ "aDawliah 64 KB cartridge",                 64 }, /* 70 */
	{ "Super Cart 64 KB 5200 cartridge",          64 },
	{ "Super Cart 128 KB 5200 cartridge",        128 },
	{ "Super Cart 256 KB 5200 cartridge",        256 },
	{ "Super Cart 512 KB 5200 cartridge",        512 },
	{ "Atarimax 1 MB Flash cartridge (new)",    1024 },

	{ "16 KB Williams cartridge",                 16 },
	{ "MIO diagnostics 8KB cartridge",             8 },
	{ "Telelink II cartridge",                     8 },
	{ "Pronto cartridge",                         16 },
	{ "JRC64 cartridge (linear)",                 64 }, /* 80 */
	{ "MDDOS cartridge",                          64 },
	{ "COS32 cartridge",                          32 },
	{ "SIC+ 1024 KB cartridge",                 1024 },
	{ "Corina 1M + 8K EEPROM cartridge",        1024 },
	{ "Corina 512K + 512K SRAM + 8K EEPROM cartridge", 512 },
	{ "XE Multicart (8KB) cartridge",              8 },
	{ "XE Multicart (16KB) cartridge",            16 },
	{ "XE Multicart (32KB) cartridge",            32 },
	{ "XE Multicart (64KB) cartridge",            64 },
	{ "XE Multicart (128KB) cartridge",          128 }, /* 90 */
	{ "XE Multicart (256KB) cartridge",          256 },
	{ "XE Multicart (512KB) cartridge",          512 },
	{ "XE Multicart (1024KB) cartridge",        1024 },

	{ "Ram-Cart 64 KB cartridge",                 64 },
	{ "Ram-Cart 128 KB cartridge",               128 },
	{ "Double Ram-Cart 256 KB cartridge",        256 },
	{ "Ram-Cart 1 MB cartridge",              1*1024 },
	{ "Ram-Cart 2 MB cartridge",              2*1024 },
	{ "Ram-Cart 4 MB cartridge",              4*1024 },
	{ "Ram-Cart 8 MB cartridge",              8*1024 }, /* 100 */
	{ "Ram-Cart 16 MB cartridge",            16*1024 },
	{ "Ram-Cart 32 MB cartridge",            32*1024 },
	{ "SiDiCar 32 KB cartridge",                  32 },

	{ "J(atari)Cart8(kB) cartridge",               8 },
	{ "J(atari)Cart16(kB) cartridge",             16 },
	{ "J(atari)Cart32(kB) cartridge",             32 },
	{ "J(atari)Cart64(kB) cartridge",             64 },
	{ "J(atari)Cart128(kB) cartridge",           128 },
	{ "J(atari)Cart256(kB) cartridge",           256 },
	{ "J(atari)Cart512(kB) cartridge",           512 }, /* 110 */
	{ "J(atari)Cart1024(kB) cartridge",         1024 },
	
	{ "DCart cartridge",                         512 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 }, /* 120 */
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 }, /* 130 */
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 }, /* 140 */
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 }, /* 150 */
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "",                                          0 },
	{ "Bounty Bob 40 KB 5200 cartridge (alt.)",   40 },

	{ "JRC64 cartridge (interleaved)",            64 } /* 160 */
};

int CARTRIDGE_Checksum(const UBYTE *image, int nbytes)
{
	int checksum = 0;
	while (nbytes > 0) {
		checksum += *image++;
		nbytes--;
	}
	return checksum;
}

