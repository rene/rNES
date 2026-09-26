/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES - Nintendo Entertainment System Emulator
 * Copyright 2021-2026 Renê de Souza Pinto
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @file romdec.c
 *
 * It provides all the functions to handle ROM files of iNES and NES 2.0
 * format.
 */
#include "romdec.h"
#include "hal/hal.h"
#include <errno.h>
#include <stdlib.h>

/** Fixup field left as the header has it */
#define FIXUP_KEEP (-1)
/** Fixup mirroring: anything the header says, unless it says four-screen */
#define FIXUP_NOT_FOUR_SCREEN (-2)

/**
 * A cartridge whose iNES header does not describe the board it was dumped
 * from. Unlicensed titles are the usual offenders: the header was filled in
 * by hand (or by a tool that guessed) long after the fact, and nothing in the
 * image itself contradicts it, so the only way to tell is to recognise the
 * dump. Entries are keyed by the CRC-32 of the image with the 16 byte header
 * removed, which is the key the No-Intro and NesCartDB databases use. Any
 * field set to FIXUP_KEEP is left as the header has it.
 */
struct _rom_fixup {
	/** CRC-32 of the ROM image, header excluded */
	uint32_t crc;
	/** Mapper the board really implements */
	int16_t mapper;
	/** Real PRG ROM size in bytes */
	int64_t prg_size;
	/** Real CHR ROM size in bytes */
	int64_t chr_size;
	/** Real nametable arrangement (rom_mirroring_t), or FIXUP_KEEP, or
	 * FIXUP_NOT_FOUR_SCREEN for boards that cannot provide four-screen VRAM
	 * but whose dumps often claim it
	 */
	int mirroring;
	/** Title of the dump, for the reader of this table */
	const char *title;
};

/** Board correction that keeps the ROM sizes declared by the header */
#define FIXUP_BOARD(crc, mapper, mirroring, title)                             \
	{(crc), (mapper), FIXUP_KEEP, FIXUP_KEEP, (mirroring), (title)}

/** Known bad headers, and what the cartridge behind them actually is */
static const struct _rom_fixup rom_fixups[] = {
	/* Headed as an MMC1 board with 32 KB of PRG ROM; it is really a Color
	 * Dreams board carrying 64 KB. Color Dreams swaps the whole of
	 * $8000-$ffff on a single write, so under MMC1 rules the bank swap never
	 * happens: the game calls into its second bank, runs whatever bank 0
	 * holds at that address instead, unbalances the stack and ends up in the
	 * IRQ vector, which this game points at its own reset path. It reboots
	 * itself a few frames in, forever, on a black screen.
	 */
	{0xcb53c523, 11, 0x10000, 0x8000, VERTICAL_MIRRORING,
	 "King Neptune's Adventure (USA) (Unl)"},

	/* The header database of FCEUX (src/ines-correct.h), which only ever
	 * corrects the mapper and the nametable arrangement. FCEUX keys it by the
	 * CRC-32 of the PRG ROM followed by the CHR ROM, which for a clean dump
	 * (no trainer, nothing past the CHR ROM) is the same as the key above.
	 * Where FCEUX lists a CRC twice, its first entry wins, as it does there.
	 */
	FIXUP_BOARD(0xaf5d7aa2, FIXUP_KEEP, HORIZONTAL_MIRRORING, "Clu Clu Land"),
	FIXUP_BOARD(0xcfb224e6, FIXUP_KEEP, VERTICAL_MIRRORING,
				"Dragon Ninja (J) [p1][!].nes"),
	FIXUP_BOARD(0x4f2f1846, FIXUP_KEEP, VERTICAL_MIRRORING,
				"Famista '89 - Kaimaku Han!! (J)"),
	FIXUP_BOARD(0x82f204ae, FIXUP_KEEP, VERTICAL_MIRRORING,
				"Liang Shan Ying Xiong (NJ023) (Ch) [!]"),
	FIXUP_BOARD(0x684afccd, FIXUP_KEEP, VERTICAL_MIRRORING, "Space Hunter (J)"),
	FIXUP_BOARD(0xad9c63e2, FIXUP_KEEP, VERTICAL_MIRRORING, "Space Shadow (J)"),
	FIXUP_BOARD(0xe1526228, FIXUP_KEEP, VERTICAL_MIRRORING, "Quest of Ki"),
	FIXUP_BOARD(0xfcdaca80, 0, HORIZONTAL_MIRRORING, "Elevator Action"),
	FIXUP_BOARD(0xc05a365b, 0, HORIZONTAL_MIRRORING, "Exed Exes (J)"),
	FIXUP_BOARD(0x32fa246f, 0, HORIZONTAL_MIRRORING, "Tag Team Pro Wrestling"),
	FIXUP_BOARD(0xb3c30bea, 0, HORIZONTAL_MIRRORING, "Xevious (J)"),
	FIXUP_BOARD(0xe492d45a, 0, HORIZONTAL_MIRRORING, "Zippy Race"),
	FIXUP_BOARD(0xe28f2596, 0, VERTICAL_MIRRORING, "Pac Land (J)"),
	FIXUP_BOARD(0xd8ee7669, 1, FIXUP_NOT_FOUR_SCREEN,
				"Adventures of Rad Gravity"),
	FIXUP_BOARD(0x5b837e8d, 1, FIXUP_NOT_FOUR_SCREEN, "Alien Syndrome"),
	FIXUP_BOARD(0x37ba3261, 1, FIXUP_NOT_FOUR_SCREEN,
				"Back to the Future 2 and 3"),
	FIXUP_BOARD(0x5b6ca654, 1, FIXUP_NOT_FOUR_SCREEN, "Barbie rev X"),
	FIXUP_BOARD(0x61a852ea, 1, FIXUP_NOT_FOUR_SCREEN,
				"Battle Stadium - Senbatsu Pro Yakyuu"),
	FIXUP_BOARD(0xf6fa4453, 1, FIXUP_NOT_FOUR_SCREEN, "Bigfoot"),
	FIXUP_BOARD(0x391aa1b8, 1, FIXUP_NOT_FOUR_SCREEN, "Bloody Warriors (J)"),
	FIXUP_BOARD(0xa5e8d2cd, 1, FIXUP_NOT_FOUR_SCREEN, "Breakthru"),
	FIXUP_BOARD(0x3f56a392, 1, FIXUP_NOT_FOUR_SCREEN, "Captain Ed (J)"),
	FIXUP_BOARD(0x078ced30, 1, FIXUP_NOT_FOUR_SCREEN,
				"Choujin - Ultra Baseball"),
	FIXUP_BOARD(0xfe364be5, 1, FIXUP_NOT_FOUR_SCREEN, "Deep Dungeon 4"),
	FIXUP_BOARD(0x57c12280, 1, FIXUP_NOT_FOUR_SCREEN, "Demon Sword"),
	FIXUP_BOARD(0xd09b74dc, 1, FIXUP_NOT_FOUR_SCREEN, "Great Tank (J)"),
	FIXUP_BOARD(0xe8baa782, 1, FIXUP_NOT_FOUR_SCREEN, "Gun Hed (J)"),
	FIXUP_BOARD(0x970bd9c2, 1, FIXUP_NOT_FOUR_SCREEN, "Hanjuku Hero"),
	FIXUP_BOARD(0xcd7a2fd7, 1, FIXUP_NOT_FOUR_SCREEN, "Hanjuku Hero"),
	FIXUP_BOARD(0x63469396, 1, FIXUP_NOT_FOUR_SCREEN, "Hokuto no Ken 4"),
	FIXUP_BOARD(0xe94d5181, 1, FIXUP_NOT_FOUR_SCREEN, "Mirai Senshi - Lios"),
	FIXUP_BOARD(0x7156cb4d, 1, FIXUP_NOT_FOUR_SCREEN,
				"Muppet Adventure Carnival thingy"),
	FIXUP_BOARD(0x70f67ab7, 1, FIXUP_NOT_FOUR_SCREEN, "Musashi no Bouken"),
	FIXUP_BOARD(0x291bcd7d, 1, FIXUP_NOT_FOUR_SCREEN, "Pachio Kun 2"),
	FIXUP_BOARD(0xa9a4ea4c, 1, FIXUP_NOT_FOUR_SCREEN, "Satomi Hakkenden"),
	FIXUP_BOARD(0xcc3544b0, 1, FIXUP_NOT_FOUR_SCREEN, "Triathron"),
	FIXUP_BOARD(0x934db14a, 1, FIXUP_KEEP, "All-Pro Basketball"),
	FIXUP_BOARD(0xf74dfc91, 1, FIXUP_KEEP, "Win, Lose, or Draw"),
	FIXUP_BOARD(0x9ea1dc76, 2, HORIZONTAL_MIRRORING, "Rainbow Islands"),
	FIXUP_BOARD(0x6d65cac6, 2, HORIZONTAL_MIRRORING, "Terra Cresta"),
	FIXUP_BOARD(0xe1b260da, 2, VERTICAL_MIRRORING, "Argos no Senshi"),
	FIXUP_BOARD(0x1d0f4d6b, 2, VERTICAL_MIRRORING, "Black Bass thinging"),
	FIXUP_BOARD(0x266ce198, 2, VERTICAL_MIRRORING, "City Adventure Touch"),
	FIXUP_BOARD(0x804f898a, 2, VERTICAL_MIRRORING, "Dragon Unit"),
	FIXUP_BOARD(0x55773880, 2, VERTICAL_MIRRORING, "Gilligan's Island"),
	FIXUP_BOARD(0x6e0eb43e, 2, VERTICAL_MIRRORING, "Puss n Boots"),
	FIXUP_BOARD(0x2bb6a0f8, 2, VERTICAL_MIRRORING, "Sherlock Holmes"),
	FIXUP_BOARD(0x28c11d24, 2, VERTICAL_MIRRORING, "Sukeban Deka"),
	FIXUP_BOARD(0x02863604, 2, VERTICAL_MIRRORING, "Sukeban Deka"),
	FIXUP_BOARD(0x419461d0, 2, VERTICAL_MIRRORING, "Super Cars"),
	FIXUP_BOARD(0xdbf90772, 3, HORIZONTAL_MIRRORING, "Alpha Mission"),
	FIXUP_BOARD(0xd858033d, 3, HORIZONTAL_MIRRORING, "Armored Scrum Object"),
	FIXUP_BOARD(0x9bde3267, 3, VERTICAL_MIRRORING, "Adventures of Dino Riki"),
	FIXUP_BOARD(0xd8eff0df, 3, VERTICAL_MIRRORING, "Gradius (J)"),
	FIXUP_BOARD(0x1d41cc8c, 3, VERTICAL_MIRRORING, "Gyruss"),
	FIXUP_BOARD(0xcf322bb3, 3, VERTICAL_MIRRORING, "John Elway's Quarterback"),
	FIXUP_BOARD(0xb5d28ea2, 3, VERTICAL_MIRRORING, "Mystery Quest - mapper 3?"),
	FIXUP_BOARD(0x02cc3973, 3, VERTICAL_MIRRORING, "Ninja Kid"),
	FIXUP_BOARD(0xbc065fc3, 3, VERTICAL_MIRRORING, "Pipe Dream"),
	FIXUP_BOARD(0xc9ee15a7, 3, FIXUP_KEEP,
				"3 is probably best. 41 WILL NOT WORK."),
	FIXUP_BOARD(
		0x13e09d7a, 4, HORIZONTAL_MIRRORING,
		"Dragon Wars (U) (proto) - comes with erroneous 4-screen mirroring set"),
	FIXUP_BOARD(0x22d6d5bd, 4, VERTICAL_MIRRORING, NULL),
	FIXUP_BOARD(0xd97c31b0, 4, VERTICAL_MIRRORING,
				"Rasaaru Ishii no Childs Quest (J)"),
	FIXUP_BOARD(0x404b2e8b, 4, FOUR_SCREEN, "Rad Racer 2"),
	FIXUP_BOARD(0x15141401, 4, FIXUP_NOT_FOUR_SCREEN, "Asmik Kun Land"),
	FIXUP_BOARD(0x4cccd878, 4, FIXUP_NOT_FOUR_SCREEN, "Cat Ninden Teyandee"),
	FIXUP_BOARD(0x59280bec, 4, FIXUP_NOT_FOUR_SCREEN, "Jackie Chan"),
	FIXUP_BOARD(0x7474ac92, 4, FIXUP_NOT_FOUR_SCREEN,
				"Kabuki: Quantum Fighter"),
	FIXUP_BOARD(0x5337f73c, 4, FIXUP_NOT_FOUR_SCREEN, "Niji no Silk Road"),
	FIXUP_BOARD(0x9eefb4b4, 4, FIXUP_NOT_FOUR_SCREEN, "Pachi Slot Adventure 2"),
	FIXUP_BOARD(0x21a653c7, 4, FIXUP_KEEP, "Super Sky Kid"),
	FIXUP_BOARD(0x9cbadc25, 5, FIXUP_NOT_FOUR_SCREEN, "JustBreed"),
	FIXUP_BOARD(0xf518dd58, 7, FIXUP_NOT_FOUR_SCREEN, "Captain Skyhawk"),
	FIXUP_BOARD(0x84382231, 9, HORIZONTAL_MIRRORING, "Punch Out (J)"),
	FIXUP_BOARD(0xbe939fce, 9, VERTICAL_MIRRORING, "Punchout"),
	FIXUP_BOARD(0x345d3a1a, 11, VERTICAL_MIRRORING, "Castle of Deceit"),
	FIXUP_BOARD(0x5e66eaea, 13, VERTICAL_MIRRORING, "Videomation"),
	FIXUP_BOARD(0xcd373baa, 14, FIXUP_KEEP, "Samurai Spirits (Rex Soft)"),
	FIXUP_BOARD(0xbfc7a2e9, 16, FIXUP_NOT_FOUR_SCREEN, NULL),
	FIXUP_BOARD(0x6e68e31a, 16, FIXUP_NOT_FOUR_SCREEN, "Dragon Ball 3"),
	FIXUP_BOARD(0x33b899c9, 16, FIXUP_KEEP,
				"Dragon Ball - Dai Maou Fukkatsu (J) [!]"),
	FIXUP_BOARD(0xa262a81f, 16, FIXUP_KEEP, "Rokudenashi Blues (J)"),
	FIXUP_BOARD(0xe4a291ce, 23, FIXUP_KEEP, "World Hero (Unl) [!]"),
	FIXUP_BOARD(0x51e9cd33, 23, FIXUP_KEEP, "World Hero (Unl) [b1]"),
	FIXUP_BOARD(0x105dd586, 27, FIXUP_KEEP, "Mi Hun Che variations..."),
	FIXUP_BOARD(0xbc9bb6c1, 27, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x43753886, 27, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x5b3de3d1, 27, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x511e73f8, 27, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x5555fca3, 32, FIXUP_NOT_FOUR_SCREEN, NULL),
	FIXUP_BOARD(0x283ad224, 32, FIXUP_NOT_FOUR_SCREEN, "Ai Sensei no Oshiete"),
	FIXUP_BOARD(0x243a8735, 32, ONE_SCREEN_LOW, "Major League"),
	FIXUP_BOARD(0xbc7b1d0f, 33, FIXUP_KEEP,
				"Bakushou!! Jinsei Gekijou 2 (J) [!]"),
	FIXUP_BOARD(0xc2730c30, 34, HORIZONTAL_MIRRORING, "Deadly Towers"),
	FIXUP_BOARD(0x4c7c1af3, 34, VERTICAL_MIRRORING, "Caesar's Palace"),
	FIXUP_BOARD(0x932ff06e, 34, VERTICAL_MIRRORING, "Classic Concentration"),
	FIXUP_BOARD(0xf46ef39a, 37, FIXUP_KEEP,
				"Super Mario Bros. + Tetris + Nintendo World Cup (E) [!]"),
	FIXUP_BOARD(0x7ccb12a3, 43, FIXUP_KEEP, "SMB2j"),
	FIXUP_BOARD(0x6c71feae, 45, FIXUP_KEEP, "Kunio 8-in-1"),
	FIXUP_BOARD(0xe2c94bc2, 48, FIXUP_KEEP, "Super Bros 8 (Unl) [!]"),
	FIXUP_BOARD(0xaebd6549, 48, FIXUP_NOT_FOUR_SCREEN,
				"Bakushou!! Jinsei Gekijou 3"),
	FIXUP_BOARD(0x6cdc0cd9, 48, FIXUP_NOT_FOUR_SCREEN, "Bubble Bobble 2"),
	FIXUP_BOARD(0x99c395f9, 48, FIXUP_NOT_FOUR_SCREEN, "Captain Saver"),
	FIXUP_BOARD(0xa7b0536c, 48, FIXUP_NOT_FOUR_SCREEN, "Don Doko Don 2"),
	FIXUP_BOARD(0x40c0ad47, 48, FIXUP_NOT_FOUR_SCREEN, "Flintstones 2"),
	FIXUP_BOARD(0x1500e835, 48, FIXUP_NOT_FOUR_SCREEN, "Jetsons (J)"),
	{0xa912b064, 51, FIXUP_KEEP, 0, FIXUP_NOT_FOUR_SCREEN,
	 "11-in-1 Ball Games (has CHR ROM when it shouldn't)"},
	FIXUP_BOARD(0xb19a55dd, 64, FIXUP_NOT_FOUR_SCREEN, "Road Runner"),
	FIXUP_BOARD(0xf92be3ec, 64, FIXUP_KEEP, "Rolling Thunder"),
	FIXUP_BOARD(0xe84274c5, 66, VERTICAL_MIRRORING, NULL),
	FIXUP_BOARD(0xbde3ae9b, 66, VERTICAL_MIRRORING, "Doraemon"),
	FIXUP_BOARD(0x9552e8df, 66, VERTICAL_MIRRORING, "Dragon Ball"),
	FIXUP_BOARD(0x811f06d9, 66, VERTICAL_MIRRORING, "Dragon Power"),
	FIXUP_BOARD(0xd26efd78, 66, VERTICAL_MIRRORING, "SMB Duck Hunt"),
	FIXUP_BOARD(0xdd8ed0f7, 70, VERTICAL_MIRRORING, "Kamen Rider Club"),
	FIXUP_BOARD(0xbba58be5, 70, FIXUP_KEEP,
				"Family Trainer - Manhattan Police"),
	FIXUP_BOARD(0x370ceb65, 70, FIXUP_KEEP,
				"Family Trainer - Meiro Dai Sakusen"),
	FIXUP_BOARD(0xe62e3382, 71, FIXUP_KEEP, "Mig-29 Soviet Fighter"),
	FIXUP_BOARD(
		0xac7b0742, 71, FIXUP_KEEP,
		"Golden KTV (Ch) [!], not actually 71, but UNROM without BUS conflict"),
	FIXUP_BOARD(0x054bd3e9, 74, FIXUP_KEEP,
				"Di 4 Ci - Ji Qi Ren Dai Zhan (As)"),
	FIXUP_BOARD(0x496ac8f7, 74, FIXUP_KEEP, "Ji Jia Zhan Shi (As)"),
	FIXUP_BOARD(0xae854cef, 74, FIXUP_KEEP, "Jia A Fung Yun (Chinese)"),
	FIXUP_BOARD(0x3d1c3137, 78, FIXUP_NOT_FOUR_SCREEN,
				"Uchuusen - Cosmo Carrier"),
	FIXUP_BOARD(0xa4fbb438, 79, HORIZONTAL_MIRRORING, NULL),
	FIXUP_BOARD(0xd4a76b07, 79, HORIZONTAL_MIRRORING, "F-15 City Wars"),
	FIXUP_BOARD(0x1eb4a920, 79, VERTICAL_MIRRORING, "Double Strike"),
	FIXUP_BOARD(0x3e1271d5, 79, VERTICAL_MIRRORING, "Tiles of Fate"),
	FIXUP_BOARD(0xd2699893, 88, HORIZONTAL_MIRRORING, "Dragon Spirit"),
	FIXUP_BOARD(0xbb7c5f7a, 89, FIXUP_NOT_FOUR_SCREEN,
				"Mito Koumon or something similar"),
	FIXUP_BOARD(0x0da5e32e, 101, FIXUP_KEEP, "new Uruusey Yatsura"),
	FIXUP_BOARD(0x8eab381c, 113, VERTICAL_MIRRORING, "Death Bots"),
	FIXUP_BOARD(0x6a03d3f3, 114, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x0d98db53, 114, FIXUP_KEEP, "Pocahontas"),
	FIXUP_BOARD(0x4e7729ff, 114, FIXUP_KEEP, "Super Donkey Kong"),
	FIXUP_BOARD(0xc5e5c5b2, 115, FIXUP_KEEP, "Bao Qing Tian (As).nes"),
	FIXUP_BOARD(0xa1dc16c0, 116, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xe40dfb7e, 116, FIXUP_KEEP, "Somari (P conf.)"),
	FIXUP_BOARD(0xc9371ebb, 116, FIXUP_KEEP, "Somari (W conf.)"),
	FIXUP_BOARD(0xcbf4366f, 118, FIXUP_NOT_FOUR_SCREEN,
				"Alien Syndrome (U.S. unlicensed)"),
	FIXUP_BOARD(0x78b657ac, 118, FIXUP_KEEP, "Armadillo"),
	FIXUP_BOARD(0x90c773c1, 118, FIXUP_KEEP, "Goal! 2"),
	FIXUP_BOARD(0xb9b4d9e0, 118, FIXUP_KEEP, "NES Play Action Football"),
	FIXUP_BOARD(0x07d92c31, 118, FIXUP_KEEP, "RPG Jinsei Game"),
	FIXUP_BOARD(0x37b62d04, 118, FIXUP_KEEP, "Ys 3"),
	FIXUP_BOARD(0x318e5502, 121, FIXUP_KEEP, "Sonic 3D Blast 6 (Unl)"),
	FIXUP_BOARD(0xddcfb058, 121, FIXUP_KEEP,
				"Street Fighter Zero 2 '97 (Unl) [!]"),
	FIXUP_BOARD(0x5aefbc94, 133, FIXUP_KEEP, "Jovial Race (Sachen) [a1][!]"),
	FIXUP_BOARD(0xc2df0a00, 140, VERTICAL_MIRRORING, "Bio Senshi Dan(hacked)"),
	FIXUP_BOARD(0xe46b1c5d, 140, VERTICAL_MIRRORING,
				"Mississippi Satsujin Jiken"),
	FIXUP_BOARD(0x3293afea, 140, VERTICAL_MIRRORING,
				"Mississippi Satsujin Jiken"),
	FIXUP_BOARD(0x6bc65d7e, 140, VERTICAL_MIRRORING, "Youkai Club"),
	FIXUP_BOARD(0x5caa3e61, 144, VERTICAL_MIRRORING, "Death Race"),
	FIXUP_BOARD(0x48239b42, 146, FIXUP_KEEP, "Mahjong Companion (Sachen) [!]"),
	FIXUP_BOARD(0xb6a727fa, 146, FIXUP_KEEP, "Papillion (As) [!]"),
	FIXUP_BOARD(0xa62b79e1, 146, FIXUP_KEEP, "Side Winder (HES) [!]"),
	FIXUP_BOARD(0xcc868d4e, 149, FIXUP_KEEP, "16 Mahjong [p1][!]"),
	FIXUP_BOARD(0x29582ca1, 150, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x40dbf7a2, 150, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x73fb55ac, 150, FIXUP_KEEP,
				"2-in-1 Cosmo Cop + Cyber Monster (Sachen) [!]"),
	FIXUP_BOARD(0xddcbda16, 150, FIXUP_KEEP,
				"2-in-1 Tough Cop + Super Tough Cop (Sachen) [!]"),
	FIXUP_BOARD(0x47918d84, 150, FIXUP_KEEP, "auto-upturn"),
	FIXUP_BOARD(0x0f141525, 152, FIXUP_NOT_FOUR_SCREEN,
				"Arkanoid 2 (Japanese)"),
	FIXUP_BOARD(0xbda8f8e4, 152, FIXUP_NOT_FOUR_SCREEN, "Gegege no Kitarou 2"),
	FIXUP_BOARD(0xb1a94b82, 152, FIXUP_NOT_FOUR_SCREEN, "Pocket Zaurus"),
	FIXUP_BOARD(0x026c5fca, 152, FIXUP_NOT_FOUR_SCREEN,
				"Saint Seiya Ougon Densetsu"),
	FIXUP_BOARD(0x3f15d20d, 153, FIXUP_NOT_FOUR_SCREEN, "Famicom Jump 2"),
	FIXUP_BOARD(0xd1691028, 154, FIXUP_NOT_FOUR_SCREEN, "Devil Man"),
	FIXUP_BOARD(0xcfd4a281, 155, FIXUP_NOT_FOUR_SCREEN,
				"Money Game. Yay for money!"),
	FIXUP_BOARD(0x2f27cdef, 155, FIXUP_NOT_FOUR_SCREEN, "Tatakae!! Rahmen Man"),
	FIXUP_BOARD(0xccc03440, 156, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x983d8175, 157, FIXUP_NOT_FOUR_SCREEN, "Datach Battle Rush"),
	FIXUP_BOARD(0x894efdbc, 157, FIXUP_NOT_FOUR_SCREEN,
				"Datach Crayon Shin Chan"),
	FIXUP_BOARD(0x19e81461, 157, FIXUP_NOT_FOUR_SCREEN, "Datach DBZ"),
	FIXUP_BOARD(0xbe06853f, 157, FIXUP_NOT_FOUR_SCREEN, "Datach J-League"),
	FIXUP_BOARD(0x0be0a328, 157, FIXUP_NOT_FOUR_SCREEN,
				"Datach SD Gundam Wars"),
	FIXUP_BOARD(0x5b457641, 157, FIXUP_NOT_FOUR_SCREEN, "Datach Ultraman Club"),
	FIXUP_BOARD(0xf51a7f46, 157, FIXUP_NOT_FOUR_SCREEN,
				"Datach Yuu Yuu Hakusho"),
	FIXUP_BOARD(0xe170404c, 159, FIXUP_KEEP,
				"SD Gundam Gaiden - Knight Gundam Monogatari (J) (V1.0) [!]"),
	FIXUP_BOARD(0x276ac722, 159, FIXUP_KEEP,
				"SD Gundam Gaiden - Knight Gundam Monogatari (J) (V1.1) [!]"),
	FIXUP_BOARD(0x0cf42e69, 159, FIXUP_KEEP,
				"Magical Taruruuto-kun - Fantastic World!! (J) (V1.0) [!]"),
	FIXUP_BOARD(0xdcb972ce, 159, FIXUP_KEEP,
				"Magical Taruruuto-kun - Fantastic World!! (J) (V1.1) [!]"),
	FIXUP_BOARD(0xb7f28915, 159, FIXUP_KEEP,
				"Magical Taruruuto-kun 2 - Mahou Daibouken (J)"),
	FIXUP_BOARD(0x183859d2, 159, FIXUP_KEEP,
				"Dragon Ball Z - Kyoushuu! Saiya Jin (J) [!]"),
	FIXUP_BOARD(0x58152b42, 160, VERTICAL_MIRRORING, "Pipe 5 (Sachen)"),
	FIXUP_BOARD(0x1c098942, 162, FIXUP_KEEP, "Xi You Ji Hou Zhuan (Ch)"),
	FIXUP_BOARD(0x081caaff, 163, FIXUP_KEEP, "Commandos (Ch)"),
	FIXUP_BOARD(0x02c41438, 176, FIXUP_KEEP, "Xing He Zhan Shi (C)"),
	FIXUP_BOARD(0x558c0dc3, 178, FIXUP_KEEP,
				"Super 2in1 (unl)[!] {mapper unsupported}"),
	FIXUP_BOARD(0xc68363f6, 180, HORIZONTAL_MIRRORING, "Crazy Climber"),
	FIXUP_BOARD(0x0f05ff0a, 181, FIXUP_KEEP, "Seicross (redump)"),
	FIXUP_BOARD(0x96ce586e, 189, FIXUP_NOT_FOUR_SCREEN,
				"Street Fighter 2 YOKO"),
	FIXUP_BOARD(0x555a555e, 191, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x2cc381f6, 191, FIXUP_KEEP,
				"Sugoro Quest - Dice no Senshitachi (As)"),
	FIXUP_BOARD(0xa145fae6, 192, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xa9115bc1, 192, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x4c7bbb0e, 192, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x98c1cd4b, 192, FIXUP_KEEP,
				"Ying Lie Qun Xia Zhuan (Chinese)"),
	FIXUP_BOARD(0xee810d55, 192, FIXUP_KEEP, "You Ling Xing Dong (Ch)"),
	FIXUP_BOARD(0x442f1a29, 192, FIXUP_KEEP, "Young chivalry"),
	FIXUP_BOARD(0x637134e8, 193, VERTICAL_MIRRORING, "Fighting Hero"),
	FIXUP_BOARD(0xa925226c, 194, FIXUP_KEEP,
				"Dai-2-Ji - Super Robot Taisen (As)"),
	FIXUP_BOARD(0x7f3dbf1b, 195, HORIZONTAL_MIRRORING, NULL),
	FIXUP_BOARD(0xb616885c, 195, HORIZONTAL_MIRRORING, "CHaos WOrld (Ch)"),
	FIXUP_BOARD(0x33c5df92, 195, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x1bc0be6c, 195, FIXUP_KEEP,
				"Captain Tsubasa Vol 2 - Super Striker (C)"),
	FIXUP_BOARD(0xd5224fde, 195, FIXUP_KEEP, "Crystalis (c)"),
	FIXUP_BOARD(0xfdec419f, 196, FIXUP_KEEP,
				"Street Fighter VI 16 Peoples (Unl) [!]"),
	FIXUP_BOARD(0x700705f4, 198, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x9a2cf02c, 198, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xd8b401a7, 198, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x28192599, 198, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x19b9e732, 198, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xdd431ba7, 198, FIXUP_KEEP, "Tenchi wo kurau 2 (c)"),
	FIXUP_BOARD(0xd871d3e6, 199, FIXUP_KEEP,
				"Dragon Ball Z 2 - Gekishin Freeza! (C)"),
	FIXUP_BOARD(0xed481b7c, 199, FIXUP_KEEP,
				"Dragon Ball Z Gaiden - Saiya Jin Zetsumetsu Keikaku (C)"),
	FIXUP_BOARD(0x44c20420, 199, FIXUP_KEEP, "San Guo Zhi 2 (C)"),
	FIXUP_BOARD(0x4e1c1e3c, 206, HORIZONTAL_MIRRORING, "Karnov"),
	FIXUP_BOARD(0x276237b3, 206, HORIZONTAL_MIRRORING, "Karnov"),
	FIXUP_BOARD(0x7678f1d5, 207, FIXUP_NOT_FOUR_SCREEN, "Fudou Myouou Den"),
	FIXUP_BOARD(0x07eb2c12, 208, FIXUP_KEEP, "Street Fighter IV"),
	FIXUP_BOARD(0xdd8ced31, 209, FIXUP_KEEP, "Power Rangers 3"),
	FIXUP_BOARD(0x063b1151, 209, FIXUP_KEEP, "Power Rangers 4"),
	FIXUP_BOARD(0xdd4d9a62, 209, FIXUP_KEEP, "Shin Samurai Spirits 2"),
	FIXUP_BOARD(0x0c47946d, 210, VERTICAL_MIRRORING, "Chibi Maruko Chan"),
	FIXUP_BOARD(0xc247cc80, 210, VERTICAL_MIRRORING, "Family Circuit '91"),
	FIXUP_BOARD(0x6ec51de5, 210, VERTICAL_MIRRORING, "Famista '92"),
	FIXUP_BOARD(0xadffd64f, 210, VERTICAL_MIRRORING, "Famista '93"),
	FIXUP_BOARD(0x429103c9, 210, VERTICAL_MIRRORING, "Famista '94"),
	FIXUP_BOARD(0x81b7f1a8, 210, VERTICAL_MIRRORING, "Heisei Tensai Bakabon"),
	FIXUP_BOARD(0x2447e03b, 210, VERTICAL_MIRRORING, "Top Striker"),
	FIXUP_BOARD(0x1dc0f740, 210, VERTICAL_MIRRORING, "Wagyan Land 2"),
	FIXUP_BOARD(0xd323b806, 210, VERTICAL_MIRRORING, "Wagyan Land 3"),
	FIXUP_BOARD(0xbd523011, 210, HORIZONTAL_MIRRORING, "Dream Master"),
	FIXUP_BOARD(0x5daae69a, 211, FIXUP_KEEP,
				"Aladdin - Return of Jaffar, The (Unl) [!]"),
	FIXUP_BOARD(0x1ec1dfeb, 217, FIXUP_KEEP, "255-in-1 (Cut version) [p1]"),
	FIXUP_BOARD(0x046d70cc, 217, FIXUP_KEEP,
				"500-in-1 (Anim Splash, Alt Mapper)[p1][!]"),
	FIXUP_BOARD(0x12f86a4d, 217, FIXUP_KEEP,
				"500-in-1 (Static Splash, Alt Mapper)[p1][!]"),
	FIXUP_BOARD(0xd09f778d, 217, FIXUP_KEEP,
				"9999999-in-1 (Static Splash, Alt Mapper)[p1][!]"),
	FIXUP_BOARD(0x62ef6c79, 232, FIXUP_NOT_FOUR_SCREEN,
				"Quattro Sports -Aladdin"),
	FIXUP_BOARD(0x2705eaeb, 234, FIXUP_KEEP, "Maxi 15"),
	FIXUP_BOARD(0x6f12afc5, 235, FIXUP_KEEP, "Golden Game 150-in-1"),
	FIXUP_BOARD(0xfb2b6b10, 241, FIXUP_KEEP, "Fan Kong Jing Ying (Ch)"),
	FIXUP_BOARD(0xb5e83c9a, 241, FIXUP_KEEP, "Xing Ji Zheng Ba (Ch)"),
	FIXUP_BOARD(0x2537b3e6, 241, FIXUP_KEEP, "Dance Xtreme - Prima (Unl)"),
	FIXUP_BOARD(0x11611e89, 241, FIXUP_KEEP, "Darkseed (Unl) [p1]"),
	FIXUP_BOARD(0x81a37827, 241, FIXUP_KEEP, "Darkseed (Unl) [p1][b1]"),
	FIXUP_BOARD(0x368c19a8, 241, FIXUP_KEEP,
				"LIKO Study Cartridge 3-in-1 (Unl) [!]"),
	FIXUP_BOARD(0xa21e675c, 241, FIXUP_KEEP, "Mashou (J) [!]"),
	FIXUP_BOARD(0x54d98b79, 241, FIXUP_KEEP, "Titanic 1912 (Unl)"),
	FIXUP_BOARD(
		0x6bea1235, 245, FIXUP_KEEP,
		"MMC3 cart, but with nobanking applied to CHR-RAM, so let it be there"),
	FIXUP_BOARD(0x345ee51a, 245, FIXUP_KEEP, "DQ4c"),
	FIXUP_BOARD(0x57514c6c, 245, FIXUP_KEEP,
				"Yong Zhe Dou E Long - Dragon Quest VI (Ch)"),
	FIXUP_BOARD(0x1d75fd35, 256, FIXUP_KEEP,
				"2-in-1 - Street Dance + Hit Mouse (Unl) [!]"),
	FIXUP_BOARD(0x6eef8bb7, 257, FIXUP_KEEP, "PEC-586 Chinese"),
	FIXUP_BOARD(0xac7e98fb, 257, FIXUP_KEEP, "PEC-586 Chinese No Tape Out"),
	FIXUP_BOARD(0x8d51a23b, 257, FIXUP_KEEP,
				"[KeWang] Chao Ji Wu Bi Han Ka (C) V1"),
	FIXUP_BOARD(0x25c76773, 257, FIXUP_KEEP,
				"[KeWang] Chao Ji Wu Bi Han Ka (C) V2"),
	FIXUP_BOARD(0x1ca9c322, 258, FIXUP_KEEP, "Blood Of Jurassic (GD-98)(Unl)"),
	FIXUP_BOARD(0x2469c1ae, 259, FIXUP_KEEP,
				"150-in-1 Unchained FIGHT version"),
	FIXUP_BOARD(0x99d4464f, 260, FIXUP_KEEP, "HP10xx/HP20xx board dumps"),
	FIXUP_BOARD(0xb72b2cf4, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x4dc6107d, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x0073dbd8, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x3b098344, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x1fc640c0, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x2f1ad1fc, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xa22214bb, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x5dd9073b, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x26a36cc2, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xd1e52b37, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0x4d4a0e1b, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xb6dd2c9d, 260, FIXUP_KEEP, NULL),
	FIXUP_BOARD(0xb02fcb57, 406, FIXUP_KEEP, "Haradius Zero ver 1.2a 2019"),
};

/**
 * Compute the CRC-32 (IEEE 802.3) of a memory block
 * @param [in] data Data block
 * @param [in] len Size of the block in bytes
 * @return uint32_t CRC-32 of the block
 */
static uint32_t crc32_block(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xffffffff;
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
	}

	return ~crc;
}

/**
 * Replace the header-derived geometry of a ROM known to be mislabelled.
 * Nothing happens unless the image matches an entry of rom_fixups[] byte for
 * byte, so a correctly headed dump is never second-guessed.
 * @param [in,out] romf ROM struct, already filled in from its header
 * @param [in] image ROM image, header included
 * @param [in] size Size of the image in bytes
 * @return const struct _rom_fixup* Applied entry, NULL if none matched
 */
static const struct _rom_fixup *
apply_rom_fixup(rom_t *romf, const uint8_t *image, size_t size)
{
	size_t i, payload, needed;
	uint64_t prg_size, chr_size;
	uint32_t crc;

	if (size <= sizeof(rom_header_t))
		return NULL;

	payload = size - sizeof(rom_header_t);
	crc = crc32_block(image + sizeof(rom_header_t), payload);

	for (i = 0; i < sizeof(rom_fixups) / sizeof(rom_fixups[0]); i++) {
		const struct _rom_fixup *fixup = &rom_fixups[i];

		if (fixup->crc != crc)
			continue;

		prg_size = romf->prg_size;
		if (fixup->prg_size != FIXUP_KEEP)
			prg_size = fixup->prg_size;
		chr_size = romf->chr_size;
		if (fixup->chr_size != FIXUP_KEEP)
			chr_size = fixup->chr_size;

		/* Belt and braces: never hand a mapper more memory than was read */
		needed = prg_size + chr_size;
		if (romf->trainer != NULL)
			needed += 512;
		if (needed > payload)
			continue;

		if (fixup->mapper != FIXUP_KEEP)
			romf->mapper = fixup->mapper;
		romf->prg_size = prg_size;
		romf->chr_size = chr_size;
		/* PRG ROM moved the end of the image, CHR ROM follows it */
		romf->chr_rom = romf->prg_rom + romf->prg_size;

		/* Correct the loaded header too, so that the nametable arrangement
		 * reported by get_rom_mirroring() cannot disagree with the one the
		 * mappers read back from the ROM struct.
		 */
		switch (fixup->mirroring) {
		case FIXUP_KEEP:
			break;
		case FIXUP_NOT_FOUR_SCREEN:
			/* The board can't provide four-screen VRAM: fall back to
			 * horizontal, and let the mapper take it from there
			 */
			if (romf->header->flag6.nes.fourscreen) {
				romf->header->flag6.nes.fourscreen = 0;
				romf->header->flag6.nes.mirroring = 0;
				romf->mirroring = HORIZONTAL_MIRRORING;
			}
			break;
		case FOUR_SCREEN:
			/* Same as a four-screen header: the mirroring bit is left as
			 * it is, and ignored
			 */
			romf->header->flag6.nes.fourscreen = 1;
			break;
		default:
			/* One-screen arrangements can't be expressed by the header,
			 * which reads back as horizontal for them
			 */
			romf->mirroring = (rom_mirroring_t)fixup->mirroring;
			romf->header->flag6.nes.fourscreen = 0;
			romf->header->flag6.nes.mirroring =
				(fixup->mirroring == VERTICAL_MIRRORING) ? 1 : 0;
			break;
		}

		return fixup;
	}

	return NULL;
}

/**
 * Load ROM file
 * @param [in] pathname Pathname to the ROM file
 * @param [out] rom ROM struct
 * @return int 0 on success, error code otherwise
 */
int load_rom(const char *pathname, rom_t **rom)
{
	ssize_t ret;
	int sflags, exp, mul;
	uint8_t *rom_file = NULL;
	rom_t *romf = NULL;

	/* Allocate ROM struct */
	romf = malloc(sizeof(rom_t));
	if (romf == NULL)
		return -ENOMEM;

	/* Load the ROM file */
	ret = load_file(pathname, &rom_file);
	if (ret < 0) {
		free(romf);
		*rom = NULL;
		return ret;
	}

	/* ROM header */
	romf->header = (rom_header_t *)rom_file;

	/* Detect ROM file */
	if (!(romf->header->magic[0] == 'N' && romf->header->magic[1] == 'E' &&
		  romf->header->magic[2] == 'S' && romf->header->magic[3] == 0x1a)) {
		/* Not a ROM file */
		free(romf);
		free(rom_file);
		*rom = NULL;
		return -EINVAL;
	}

	/* Detect ROM file format */
	sflags = romf->header->flag12.raw + romf->header->flag13.raw +
			 romf->header->flag14.raw + romf->header->flag15.raw;
	if ((romf->header->flag7.raw & 0xc) == 0 && sflags == 0)
		romf->rom_fmt = INES_FMT;
	else if ((romf->header->flag7.raw & 0xc) == 0xc)
		romf->rom_fmt = NES_FMT;
	else
		romf->rom_fmt = UNKNOWN_FMT;

	/* ROM Mirroring */
	romf->mirroring = romf->header->flag6.nes.mirroring;

	/* First we check the Nametable arrangement */
	if (romf->header->flag6.nes.mirroring == 1)
		romf->mirroring = VERTICAL_MIRRORING;

	/* It might be a 4 screen game */
	if (romf->header->flag6.nes.fourscreen == 1)
		romf->mirroring = FOUR_SCREEN;

	/* Calculate PRG ROM and CHR ROM size */
	if (romf->rom_fmt == INES_FMT) {
		romf->prg_size = romf->header->prg_size * (16 * 1024);
		romf->chr_size = romf->header->chr_size * (8 * 1024);
	} else {
		if (romf->header->flag9.nes.prgrom_size < 0xf) {
			/* Size in 16 KB units: MSB nibble (flag9) + LSB byte */
			romf->prg_size =
				((romf->header->flag9.nes.prgrom_size << 8) & 0xf00) |
				romf->header->prg_size;
			romf->prg_size *= (16 * 1024);
		} else {
			/* Get exponent and multiplier */
			exp = ((romf->header->prg_size & 0xfc) >> 2);
			mul = ((romf->header->prg_size & 0x3) * 2) + 1;
			/* Calculate total size in bytes */
			romf->prg_size = (1 << exp) * mul;
		}

		if (romf->header->flag9.nes.chrrom_size < 0xf) {
			/* Size in 8 KB units: MSB nibble (flag9) + LSB byte */
			romf->chr_size =
				((romf->header->flag9.nes.chrrom_size << 8) & 0xf00) |
				romf->header->chr_size;
			romf->chr_size *= (8 * 1024);
		} else {
			/* Get exponent and multiplier */
			exp = ((romf->header->chr_size & 0xfc) >> 2);
			mul = ((romf->header->chr_size & 0x3) * 2) + 1;
			/* Calculate total size in bytes */
			romf->chr_size = (1 << exp) * mul;
		}
	}

	/* Find pointers for PRG ROM, CHR ROM and Trainer (if present). When
	 * Trainer area is present, it follows the ROM header and it's always
	 * 512 bytes in size.
	 */
	if (romf->header->flag6.nes.trainer == 1) {
		romf->trainer = (uint8_t *)romf->header + sizeof(rom_header_t);
		romf->prg_rom = romf->trainer + 512;
	} else {
		romf->trainer = NULL;
		romf->prg_rom = (uint8_t *)romf->header + sizeof(rom_header_t);
	}
	romf->chr_rom = romf->prg_rom + romf->prg_size;

	/* Check mapper number (if present) */
	if (romf->rom_fmt == INES_FMT) {
		romf->mapper = ((romf->header->flag7.ines.mapper << 4) & 0xf0) |
					   (romf->header->flag6.nes.mapper & 0xf);
	} else {
		romf->mapper = ((romf->header->flag8.nes.mapper << 8) & 0xf00) |
					   ((romf->header->flag7.nes.mapper << 4) & 0xf0) |
					   (romf->header->flag6.nes.mapper & 0xf);
	}

	/* Last word goes to the fixup table, for the handful of dumps whose
	 * header describes a cartridge that never existed.
	 */
	apply_rom_fixup(romf, rom_file, (size_t)ret);

	*rom = romf;
	return 0;
}

/**
 * Unload ROM from the memory
 * @param [in,out] rom ROM struct
 * @return in 0 on success, error code otherwise
 */
int unload_rom(rom_t *rom)
{
	free(rom->header);
	free(rom);
	rom = NULL;
	return 0;
}

/**
 * Return ROM file format
 * @param [in] rom ROM struct
 * @return INES_FMT, NES_FMT or UNKNOWN_FMT
 */
rom_fmt_t get_rom_format(rom_t *rom)
{
	if (rom == NULL)
		return UNKNOWN_FMT;
	else
		return rom->rom_fmt;
}

/**
 * Return the default ROM mirroring mode
 * @param [in] rom ROM struct
 * @return HORIZONTAL_MIRRORING, VERTICAL_MIRRORING or FOUR_SCREEN
 */
rom_mirroring_t get_rom_mirroring(rom_t *rom)
{
	if (rom == NULL)
		return HORIZONTAL_MIRRORING;
	else
		return rom->mirroring;
}

/**
 * Return ROM video standard
 * @param [in] rom ROM struct
 * @return ROM_NTSC, ROM_PAL_M, ROM_MULTIPLE or ROM_DENDY
 */
rom_video_t get_rom_video_std(rom_t *rom)
{
	if (rom == NULL)
		return ROM_NTSC;

	if (rom->rom_fmt == INES_FMT) {
		if (rom->header->flag9.ines.TV_system == 0)
			return ROM_NTSC;
		else
			return ROM_PAL_M;
	} else {
		switch (rom->header->flag12.nes.cpu_ppu_timing) {
		case 0:
			return ROM_NTSC;
		case 1:
			return ROM_PAL_M;
		case 2:
			return ROM_MULTIPLE;
		case 3:
			return ROM_DENDY;
		default:
			return ROM_NTSC;
		}
	}
}
