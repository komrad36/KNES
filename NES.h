/*******************************************************************
*   NES.h
*   KNES
*
*	Author: Kareem Omar
*	kareem.omar@uah.edu
*	https://github.com/komrad36
*
*	Last updated Dec 18, 2016
*******************************************************************/
//
// Lightweight but complete NES emulator. Straightforward implementation in a
// few thousand lines of C++.
//
// Written from scratch in a speedcoding challenge in just 72 hours.
// Intended to showcase low-level and 6502 emulation, basic game loop mechanics,
// audio, video, user interaction. Also provides a compact emulator
// fully open and free to study and modify.
//
// No external dependencies except for
// those needed for interfacing:
// 
// - PortAudio for sound (http://www.portaudio.com/)
// - GLFW for video (http://www.glfw.org/)
//
// If you compile GLFW yourself, be sure to specify
// shared build ('cmake -DBUILD_SHARED_LIBS=ON .')
// or you will enter dependency hell at link-time.
//
// Fully cross-platform. Tested on Windows and Linux.
//
// Fully playable, with CPU, APU, PPU emulated and 6 of the most common
// mappers supported (0, 1, 2, 3, 4, 7). Get a .nes v1 file and go!
//
// Written from scratch in a speedcoding challenge (72 hours!). This means
// the code is NOT terribly clean. Always loved the 6502 and wanted to try
// something crazy. Got it fully working, with 6 mappers, in 3 days.
//
// I tend not to like OO much, especially for speedcoding, so here it's pretty
// much only used for mapper polymorphism.
//
// Usage: KNES <rom_file>
//
// Keymap (modify as desired in 'main.cpp'):
// -------------------------------------
//  Up/Down/Left/Right   |  Arrow Keys
//  Start                |  Enter
//  Select               |  Right Shift
//  A                    |  Z
//  B                    |  X
//  Turbo A              |  S
//  Turbo B              |  D
// -------------------------------------
// Emulator keys:
//  Tilde                |  Fast-forward
//  Escape               |  Quit
//  ALT+F4               |  Quit
// -------------------------------------
//
// The display window can be freely resized at runtime.
// You can also set proper full-screen mode at the top
// of 'main.cpp', and also enable V-SYNC if you are
// experiencing tearing issues.
//
// I love the 6502 and am relatively confident in the CPU emulation
// but have much less knowledge about the PPU and APU
// and am sure at least a few things are wrong here and there.
//
// Feel free to correct and/or teach me about the PPU and APU!
//
// Major thanks to http://www.6502.org/ for CPU ref, and especially
// to http://nesdev.com/, which I basically spent the three days
// scouring every inch of, especially to figure out the mappers and PPU.
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "pa_ringbuffer.h"

constexpr int INES_MAGIC = 0x1a53454e;
constexpr double CPU_FREQ = 1789773.0;
constexpr double FRAME_CTR_FREQ = CPU_FREQ / 240.0;
constexpr double SAMPLE_RATE = CPU_FREQ / (44100.0 * 2.0);

enum Buttons {
	ButtonA = 0,
	ButtonB = 1,
	ButtonSelect = 2,
	ButtonStart = 3,
	ButtonUp = 4,
	ButtonDown = 5,
	ButtonLeft = 6,
	ButtonRight = 7
};

enum Interrupts {
	interruptNone = 1,
	interruptNMI = 2,
	interruptIRQ = 3
};

enum AddressingModes {
	modeAbsolute = 1,
	modeAbsoluteX = 2,
	modeAbsoluteY = 3,
	modeAccumulator = 4,
	modeImmediate = 5,
	modeImplied = 6,
	modeIndexedIndirect = 7,
	modeIndirect = 8,
	modeIndirectIndexed = 9,
	modeRelative = 10,
	modeZeroPage = 11,
	modeZeroPageX = 12,
	modeZeroPageY = 13
};

enum MirrorModes {
	MirrorHorizontal = 0,
	MirrorVertical = 1,
	MirrorSingle0 = 2,
	MirrorSingle1 = 3,
	MirrorFour = 4
};

struct iNESHeader {
	uint32_t magic;     // iNES cartridge file magic number
	uint8_t num_prg;    // number of 16k PRG-ROM banks
	uint8_t num_chr;    // number of 8k CHR-ROM banks
	uint8_t ctrl1;
	uint8_t ctrl2;
	uint8_t num_ram;    // number of 8k PRG-RAM banks
	uint8_t padding[7];
};

// APU Delta Modulation Channel
struct DMC {
	bool enabled;
	uint8_t value;
	uint16_t samp_addr;
	uint16_t samp_len;
	uint16_t cur_addr;
	uint16_t cur_len;
	uint8_t shift_reg;
	uint8_t bit_count;
	uint8_t tick_period;
	uint8_t tick_val;
	bool loop;
	bool irq;

	DMC() : enabled(false), value(0), samp_addr(0), samp_len(0), cur_addr(0), cur_len(0), shift_reg(0), bit_count(0), tick_period(0), tick_val(0), loop(false), irq(false) {}
};

// APU Pulse Channel
struct Pulse {
	bool enabled;
	uint8_t channel;
	bool length_enabled;
	uint8_t length_val;
	uint16_t timer_period;
	uint16_t timer_val;
	uint8_t duty_mode;
	uint8_t duty_val;
	bool sweep_reload;
	bool sweep_enabled;
	bool sweep_negate;
	uint8_t sweep_shift;
	uint8_t sweep_period;
	uint8_t sweep_val;
	bool envelope_enabled;
	bool envelope_loop;
	bool envelope_start;
	uint8_t envelope_period;
	uint8_t envelope_val;
	uint8_t envelope_vol;
	uint8_t const_vol;

	Pulse() : enabled(false), channel(0), length_enabled(false), length_val(0), timer_period(0), timer_val(0), duty_mode(0), duty_val(0), sweep_reload(false), sweep_enabled(false), sweep_negate(false), sweep_shift(0), sweep_period(0), sweep_val(0), envelope_enabled(false), envelope_loop(false), envelope_start(false), envelope_period(0), envelope_val(0), envelope_vol(0), const_vol(0) {}
};

// APU Triangle Channel
struct Triangle {
	bool enabled;
	bool length_enabled;
	uint8_t length_val;
	uint16_t timer_period;
	uint16_t timer_val;
	uint8_t duty_val;
	uint8_t counter_period;
	uint8_t counter_val;
	bool counter_reload;

	Triangle() : enabled(false), length_enabled(false), length_val(0), timer_period(0), timer_val(0), duty_val(0), counter_period(0), counter_val(0), counter_reload(false) {}
};

// APU Noise Channel
struct Noise {
	bool enabled;
	bool mode;
	uint16_t shift_reg;
	bool length_enabled;
	uint8_t length_val;
	uint16_t timer_period;
	uint16_t timer_val;
	bool envelope_enabled;
	bool envelope_loop;
	bool envelope_start;
	uint8_t envelope_period;
	uint8_t envelope_val;
	uint8_t envelope_vol;
	uint8_t const_vol;

	Noise() : enabled(false), mode(false), shift_reg(0), length_enabled(false), length_val(0), timer_period(0), timer_val(0), envelope_enabled(false), envelope_loop(false), envelope_start(false), envelope_period(0), envelope_val(0), envelope_vol(0), const_vol(0) {}
};

struct APU {
	PaUtilRingBuffer ring_buf; // ring buffer for communicating to PortAudio with low latency
	Pulse pulse1;
	Pulse pulse2;
	Triangle triangle;
	Noise noise;
	DMC dmc;
	uint64_t cycle;
	uint8_t frame_period;
	uint8_t frame_val;
	bool frame_IRQ;

	APU() : cycle(0), frame_period(0), frame_val(0), frame_IRQ(false) {
		void* ring_buf_storage = malloc(sizeof(float) << 13);
		if (ring_buf_storage != nullptr) {
			PaUtil_InitializeRingBuffer(&ring_buf, sizeof(float), 8192, ring_buf_storage);
		}
	}
};

struct Cartridge {
	uint8_t* PRG; // PRG-ROM banks
	int prg_size;
	uint8_t* CHR; // CHR-ROM banks
	int chr_size;
	uint8_t* SRAM; // Save RAM
	bool trainer_present;
	uint8_t* trainer;
	uint8_t mapper; // mapper type
	uint8_t mirror; // mirroring mode
	uint8_t battery_present; // battery present

	Cartridge(const char* path, const char* SRAM_path) {
		FILE* fp = fopen(path, "rb");
		if (fp == nullptr) {
			std::cerr << "ERROR: failed to open ROM file!" << std::endl;
			return;
		}

		iNESHeader header;
		size_t elems_read = fread(&header, sizeof(header), 1, fp);
		if (elems_read != 1) {
			std::cerr << "ERROR: failed to read ROM header!" << std::endl;
			return;
		}

		if (header.magic != INES_MAGIC) {
			std::cerr << "ERROR: invalid .nes file!" << std::endl;
			return;
		}

		int mapper1 = header.ctrl1 >> 4;
		int mapper2 = header.ctrl2 >> 4;
		mapper = static_cast<uint8_t>(mapper1 | (mapper2 << 4));

		int mirror1 = header.ctrl1 & 1;
		int mirror2 = (header.ctrl1 >> 3) & 1;
		mirror = static_cast<uint8_t>(mirror1 | (mirror2 << 1));

		battery_present = static_cast<uint8_t>((header.ctrl1 >> 1) & 1);

		trainer_present = false;
		if ((header.ctrl1 & 4) == 4) {
			trainer_present = true;
			trainer = new uint8_t[512];
			elems_read = fread(trainer, 512, 1, fp);
			if (elems_read != 1) {
				std::cerr << "ERROR: failed to read trainer!" << std::endl;
				return;
			}
		}

		prg_size = static_cast<int>(header.num_prg) << 14;
		PRG = new uint8_t[prg_size];
		elems_read = fread(PRG, prg_size, 1, fp);
		if (elems_read != 1) {
			std::cerr << "ERROR: failed to read PRG-ROM!" << std::endl;
			return;
		}

		chr_size = static_cast<int>(header.num_chr) << 13;
		if (chr_size == 0) {
			chr_size = 8192;
			CHR = new uint8_t[8192];
			memset(CHR, 0, 8192);
		}
		else {
			CHR = new uint8_t[chr_size];
			elems_read = fread(CHR, chr_size, 1, fp);
			if (elems_read != 1) {
				std::cerr << "ERROR: failed to read CHR-ROM!" << std::endl;
				return;
			}
		}

		fclose(fp);

		SRAM = new uint8_t[8192];

		memset(SRAM, 0, 8192);
		if (battery_present) {
			// try to read saved SRAM
			std::cout << "Attempting to read previously saved SRAM..." << std::endl;
			fp = fopen(SRAM_path, "rb");
			if (fp == nullptr || (fread(SRAM, 8192, 1, fp) != 1)) {
				std::cout << "WARN: failed to open SRAM file!" << std::endl;
			}
			else {
				fclose(fp);
			}
		}
	}
};

struct CPU {
	uint64_t cycles;
	uint16_t PC;       // program counter
	uint8_t SP;        // stack pointer
	uint8_t A;         // accumulator
	uint8_t X;         // X register
	uint8_t Y;         // Y register
	uint8_t flags;     // flags register
	uint8_t interrupt; // interrupt type
	int stall;

	CPU() : cycles(0), PC(0), SP(0), A(0), X(0), Y(0), flags(0), interrupt(0), stall(0) {}
};

struct PPU {
	int cycle; // 0-340
	int scanline; // 0-261. 0-239 is visible, 240 is postline, 241-260 is the v_blank interval, 261 is preline
	uint64_t frame;

	uint8_t palette_tbl[32];
	uint8_t name_tbl[2048];
	uint8_t oam_tbl[256];

	uint32_t* front;
	uint32_t* back;

	// regs
	uint16_t v; // vram address
	uint16_t t; // temp vram address
	uint8_t x;  // fine x scroll
	uint8_t w;  // write flag
	uint8_t f;  // even/odd flag

	uint8_t reg;

	bool nmi_occurred;
	bool nmi_out;
	bool nmi_last;
	uint8_t nmi_delay;

	uint8_t name_tbl_u8;
	uint8_t attrib_tbl_u8;
	uint8_t low_tile_u8;
	uint8_t high_tile_u8;
	uint64_t tile_data;

	int sprite_cnt;
	uint32_t sprite_patterns[8];
	uint8_t sprite_pos[8];
	uint8_t sprite_priorities[8];
	uint8_t sprite_idx[8];

	// $2000 PPUCTRL
	uint8_t flag_name_tbl;       // 0: $2000.  1: $2400. 2: $2800. 3: $2C00
	uint8_t flag_increment;      // 0: add 1.  1: add 32
	uint8_t flag_sprite_tbl;     // 0: $0000.  1: $1000
	uint8_t flag_background_tbl; // 0: $0000.  1: $1000
	uint8_t flag_sprite_size;    // 0: 8x8.    1: 8x16
	uint8_t flag_rw;             // 0: read.   1: write

	// $2001 PPUMASK
	uint8_t flag_gray;                 // 0: color.   1: grayscale
	uint8_t flag_show_left_background; // 0: hide.    1: show
	uint8_t flag_show_left_sprites;    // 0: hide.    1: show
	uint8_t flag_show_background;      // 0: hide.    1: show
	uint8_t flag_show_sprites;         // 0: hide.    1: show
	uint8_t flag_red_tint;             // 0: normal.  1: emphasized
	uint8_t flag_green_tint;           // 0: normal.  1: emphasized
	uint8_t flag_blue_tint;            // 0: normal.  1: emphasized

	// $2002 PPUSTATUS
	uint8_t flag_sprite_zero_hit;
	uint8_t flag_sprite_overflow;

	// $2003 OAMADDR
	uint8_t oam_addr;

	// $2007 PPUDATA
	uint8_t buffered_data;

	PPU() : cycle(0), scanline(0), frame(0), v(0), t(0), x(0), w(0), f(0), reg(0), nmi_occurred(false), nmi_out(false), nmi_last(false),
		nmi_delay(0), name_tbl_u8(0), attrib_tbl_u8(0), low_tile_u8(0), high_tile_u8(0), tile_data(0), sprite_cnt(0), flag_name_tbl(0), flag_increment(0),
		flag_sprite_tbl(0), flag_background_tbl(0), flag_sprite_size(0), flag_rw(0), flag_gray(0), flag_show_left_background(0), flag_show_left_sprites(0),
		flag_show_background(0), flag_show_sprites(0), flag_red_tint(0), flag_green_tint(0), flag_blue_tint(0), flag_sprite_zero_hit(0), flag_sprite_overflow(0),
		oam_addr(0), buffered_data(0)
	{
		memset(palette_tbl, 0, 32);
		memset(name_tbl, 0, 2048);
		memset(oam_tbl, 0, 256);
		memset(sprite_patterns, 0, 32);
		memset(sprite_pos, 0, 8);
		memset(sprite_priorities, 0, 8);
		memset(sprite_idx, 0, 8);
	}
};

void writePPUCtrl(PPU* ppu, uint8_t x);
void writePPUMask(PPU* ppu, uint8_t x);

struct Controller {
	uint8_t buttons;
	uint8_t index;
	uint8_t strobe;

	Controller() : buttons(0), index(0), strobe(0) {}
};

struct Mapper {
	virtual uint8_t read(Cartridge* cartridge, uint16_t address) = 0;
	virtual void write(Cartridge* cartridge, uint16_t address, uint8_t value) = 0;
	virtual void updateCounter(CPU* cpu) = 0;
};

struct Mapper1 : public Mapper {
	uint8_t shift_reg;
	uint8_t control;
	uint8_t prg_mode;
	uint8_t chr_mode;
	uint8_t prg_bank;
	uint8_t chr_bank0;
	uint8_t chr_bank1;
	int prg_offsets[2];
	int chr_offsets[2];

	int prgBankOffset(Cartridge* c, int index);
	int chrBankOffset(Cartridge* cartridge, int index);
	void updateOffsets(Cartridge* cartridge);
	void writeCtrl(Cartridge* cartridge, uint8_t value);

	uint8_t read(Cartridge* cartridge, uint16_t address) {
		if (address < 0x2000) {
			const uint16_t bank = address >> 12;
			const uint16_t offset = address & 4095;
			return cartridge->CHR[chr_offsets[bank] + static_cast<int>(offset)];
		}
		else if (address >= 0x8000) {
			address -= 0x8000;
			const uint16_t bank = address >> 14;
			const uint16_t offset = address & 16383;
			return cartridge->PRG[prg_offsets[bank] + static_cast<int>(offset)];
		}
		else if (address >= 0x6000) {
			return cartridge->SRAM[static_cast<int>(address) - 0x6000];
		}
		else {
			std::cerr << "ERROR: Mapper1 encountered unrecognized read (address 0x" << std::hex << address << std::dec << ')' << std::endl;
			return 0;
		}
	}

	void write(Cartridge* cartridge, uint16_t address, uint8_t value) {
		if (address < 0x2000) {
			const uint16_t bank = address >> 12;
			const uint16_t offset = address & 4095;
			cartridge->CHR[chr_offsets[bank] + static_cast<int>(offset)] = value;
		}
		else if (address >= 0x8000) {
			if ((value & 0x80) == 0x80) {
				shift_reg = 0x10;
				writeCtrl(cartridge, control | 0x0C);
				updateOffsets(cartridge);
			}
			else {
				const bool complete = (shift_reg & 1) == 1;
				shift_reg >>= 1;
				shift_reg |= (value & 1) << 4;
				if (complete) {
					if (address <= 0x9FFF) {
						writeCtrl(cartridge, shift_reg);
					}
					else if (address <= 0xBFFF) {
						// CHRbank 0 ($A000-$BFFF)
						chr_bank0 = shift_reg;
					}
					else if (address <= 0xDFFF) {
						// CHRbank 1 ($C000-$DFFF)
						chr_bank1 = shift_reg;
					}
					else {
						// PRGbank ($E000-$FFFF)
						prg_bank = shift_reg & 0x0F;
					}
					updateOffsets(cartridge);
					shift_reg = 0x10;
				}
			}
		}
		else if (address >= 0x6000) {
			cartridge->SRAM[static_cast<int>(address) - 0x6000] = value;
		}
		else {
			std::cerr << "ERROR: Mapper1 encountered unrecognized write (address 0x" << std::hex << address << std::dec << ')' << std::endl;
		}
	}

	void updateCounter(CPU* cpu) {
		static_cast<void>(cpu);
	}

	Mapper1() : shift_reg(0), control(0), prg_mode(0), chr_mode(0), prg_bank(0), chr_bank0(0), chr_bank1(0), prg_offsets{ 0, 0 }, chr_offsets{ 0, 0 } {}
};

struct Mapper2 : public Mapper {
	int prg_banks;
	int prg_bank1;
	int prg_bank2;

	uint8_t read(Cartridge* cartridge, uint16_t address) {
		if (address < 0x2000) {
			return cartridge->CHR[address];
		}
		else if (address >= 0xC000) {
			const int index = (prg_bank2 << 14) + static_cast<int>(address - 0xC000);
			return cartridge->PRG[index];
		}
		else if (address >= 0x8000) {
			const int index = (prg_bank1 << 14) + static_cast<int>(address - 0x8000);
			return cartridge->PRG[index];
		}
		else if (address >= 0x6000) {
			const int index = static_cast<int>(address) - 0x6000;
			return cartridge->SRAM[index];
		}
		else {
			std::cerr << "ERROR: Mapper2 encountered unrecognized read (address 0x" << std::hex << address << std::dec << ')' << std::endl;
			return 0;
		}
	}

	void write(Cartridge* cartridge, uint16_t address, uint8_t value) {
		if (address < 0x2000) {
			cartridge->CHR[address] = value;
		}
		else if (address >= 0x8000) {
			prg_bank1 = static_cast<int>(value) % prg_banks;
		}
		else if (address >= 0x6000) {
			const int index = static_cast<int>(address) - 0x6000;
			cartridge->SRAM[index] = value;
		}
		else {
			std::cerr << "ERROR: Mapper2 encountered unrecognized write (address 0x" << std::hex << address << std::dec << ')' << std::endl;
		}
	}

	void updateCounter(CPU* cpu) {
		static_cast<void>(cpu);
	}

	Mapper2(int _prgBanks, int _prgBank1, int _prgBank2) : prg_banks(_prgBanks), prg_bank1(_prgBank1), prg_bank2(_prgBank2) {}
};

struct Mapper3 : public Mapper {
	int chr_bank;
	int prg_bank1;
	int prg_bank2;

	uint8_t read(Cartridge* cartridge, uint16_t address) {
		if (address < 0x2000) {
			const int index = chr_bank * 0x2000 + static_cast<int>(address);
			return cartridge->CHR[index];
		}
		else if (address >= 0xC000) {
			const int index = prg_bank2 * 0x4000 + static_cast<int>(address - 0xC000);
			return cartridge->PRG[index];
		}
		else if (address >= 0x8000) {
			const int index = prg_bank1 * 0x4000 + static_cast<int>(address - 0x8000);
			return cartridge->PRG[index];
		}
		else if (address >= 0x6000) {
			const int index = int(address) - 0x6000;
			return cartridge->SRAM[index];
		}
		else {
			std::cerr << "ERROR: Mapper3 encountered unrecognized read (address 0x" << std::hex << address << std::dec << ')' << std::endl;
			return 0;
		}
	}

	void write(Cartridge* cartridge, uint16_t address, uint8_t value) {
		if (address < 0x2000) {
			const int index = chr_bank * 0x2000 + static_cast<int>(address);
			cartridge->CHR[index] = value;
		}
		else if (address >= 0x8000) {
			chr_bank = static_cast<int>(value & 3);
		}
		else if (address >= 0x6000) {
			const int index = static_cast<int>(address) - 0x6000;
			cartridge->SRAM[index] = value;
		}
		else {
			std::cerr << "ERROR: Mapper3 encountered unrecognized write (address 0x" << std::hex << address << std::dec << ')' << std::endl;
		}
	}
	void updateCounter(CPU* cpu) {
		static_cast<void>(cpu);
	}

	Mapper3(int _chrBank, int _prgBank1, int _prgBank2) : chr_bank(_chrBank), prg_bank1(_prgBank1), prg_bank2(_prgBank2) {}
};

struct Mapper4 : public Mapper {
	uint8_t reg;
	uint8_t regs[8];
	uint8_t prg_mode;
	uint8_t chr_mode;
	int prg_offsets[4];
	int chr_offsets[8];
	uint8_t reload;
	uint8_t counter;
	bool IRQ_enable;

	int prgBankOffset(Cartridge* c, int index);
	int chrBankOffset(Cartridge* cartridge, int index);
	void updateOffsets(Cartridge* cartridge);

	uint8_t read(Cartridge* cartridge, uint16_t address) {
		if (address < 0x2000) {
			const uint16_t bank = address >> 10;
			const uint16_t offset = address & 1023;
			return cartridge->CHR[chr_offsets[bank] + static_cast<int>(offset)];
		}
		else if (address >= 0x8000) {
			address -= 0x8000;
			const uint16_t bank = address >> 13;
			const uint16_t offset = address & 8191;
			return cartridge->PRG[prg_offsets[bank] + static_cast<int>(offset)];
		}
		else if (address >= 0x6000) {
			return cartridge->SRAM[static_cast<int>(address) - 0x6000];
		}
		else {
			std::cerr << "ERROR: Mapper4 encountered unrecognized read (address 0x" << std::hex << address << std::dec << ')' << std::endl;
			return 0;
		}
	}

	void write(Cartridge* cartridge, uint16_t address, uint8_t value) {
		if (address < 0x2000) {
			const uint16_t bank = address >> 10;
			const uint16_t offset = address & 1023;
			cartridge->CHR[chr_offsets[bank] + static_cast<int>(offset)] = value;
		}
		else if (address >= 0x8000) {
			if (address <= 0x9FFF && (address & 1) == 0) {
				// bank select
				prg_mode = (value >> 6) & 1;
				chr_mode = (value >> 7) & 1;
				reg = value & 7;
				updateOffsets(cartridge);
			}
			else if (address <= 0x9FFF && (address & 1)) {
				// bank data
				regs[reg] = value;
				updateOffsets(cartridge);
			}
			else if (address <= 0xBFFF && (address & 1) == 0) {
				switch (value & 1) {
				case 0:
					cartridge->mirror = MirrorVertical;
					break;
				case 1:
					cartridge->mirror = MirrorHorizontal;
					break;
				}
			}
			else if (address <= 0xBFFF && (address & 1)) {
				// TODO
			}
			else if (address <= 0xDFFF && (address & 1) == 0) {
				// IRQ latch
				reload = value;
			}
			else if (address <= 0xDFFF && (address & 1)) {
				// IRQ reload
				counter = 0;
			}
			else if ((address & 1) == 0) {
				// IRQ disable
				IRQ_enable = false;
			}
			else {
				// IRQ enable
				IRQ_enable = true;
			}
		}
		else if (address >= 0x6000) {
			cartridge->SRAM[static_cast<int>(address) - 0x6000] = value;
		}
		else {
			std::cerr << "ERROR: Mapper4 encountered unrecognized write (address 0x" << std::hex << address << std::dec << ')' << std::endl;
		}
	}

	void updateCounter(CPU* cpu);

	Mapper4() : reg(0), regs{ 0, 0, 0, 0, 0, 0, 0, 0 }, prg_mode(0), chr_mode(0), prg_offsets{ 0, 0, 0, 0 }, chr_offsets{ 0, 0, 0, 0, 0, 0, 0, 0 }, reload(0), counter(0), IRQ_enable(false) {}
};

struct Mapper7 : public Mapper {
	int prg_bank;

	uint8_t read(Cartridge* cartridge, uint16_t address) {
		if (address < 0x2000) {
			return cartridge->CHR[address];
		}
		else if (address >= 0x8000) {
			const int index = (prg_bank << 15) + static_cast<int>(address - 0x8000);
			return cartridge->PRG[index];
		}
		else if (address >= 0x6000) {
			const int index = static_cast<int>(address) - 0x6000;
			return cartridge->SRAM[index];
		}
		else {
			std::cerr << "ERROR: Mapper7 encountered unrecognized read (address 0x" << std::hex << address << std::dec << ')' << std::endl;
			return 0;
		}
	}

	void write(Cartridge* cartridge, uint16_t address, uint8_t value) {
		if (address < 0x2000) {
			cartridge->CHR[address] = value;
		}
		else if (address >= 0x8000) {
			prg_bank = static_cast<int>(value & 7);
			switch (value & 0x10) {
			case 0x00:
				cartridge->mirror = MirrorSingle0;
				break;
			case 0x10:
				cartridge->mirror = MirrorSingle1;
				break;
			}
		}
		else if (address >= 0x6000) {
			int index = static_cast<int>(address) - 0x6000;
			cartridge->SRAM[index] = value;
		}
		else {
			std::cerr << "ERROR: Mapper7 encountered unrecognized write (address 0x" << std::hex << address << std::dec << ')' << std::endl;
		}
	}

	void updateCounter(CPU* cpu) {
		static_cast<void>(cpu);
	}

	Mapper7() : prg_bank(0) {}
};

struct NES {
	CPU* cpu;
	APU* apu;
	PPU* ppu;
	Cartridge* cartridge;
	Controller* controller1;
	Controller* controller2;
	Mapper* mapper;
	uint8_t* RAM;

	NES(const char* path, const char* SRAM_path);
};

struct Instruction {
	const uint8_t opcode;
	const char* name;
	void(*dispatch)(CPU*, NES*, uint16_t, uint8_t);
	const uint8_t mode;
	const uint8_t size;
	const uint8_t cycles;
	const uint8_t page_cross_cycles;

	constexpr Instruction(const uint8_t _opcode, const char _name[4], void(*_dispatch)(CPU*, NES*, uint16_t, uint8_t), const uint8_t _mode, const uint8_t _size, const uint8_t _cycles, const uint8_t _page_crossed_cycles) : opcode(_opcode), name(_name), dispatch(_dispatch), mode(_mode), size(_size), cycles(_cycles), page_cross_cycles(_page_crossed_cycles) {}
};

void PPUnmiShift(PPU* ppu);
void dmcRestart(DMC* d);

uint8_t readPalette(PPU* ppu, uint16_t address);
uint8_t readPPU(NES* nes, uint16_t address);
uint8_t readByte(NES* nes, uint16_t address);
void push16(NES* nes, uint16_t value);
void php(CPU* cpu, NES* nes, uint16_t address, uint8_t mode);

uint16_t read16(NES* nes, uint16_t address);
void execute(NES* nes, uint8_t opcode);
void writeByte(NES* nes, uint16_t address, uint8_t value);
void emulate(NES* nes, double seconds);

void setI(CPU* cpu, bool value);
uint8_t getI(CPU* cpu);

void tickEnvelope(APU* apu);
void tickSweep(APU* apu);
void tickLength(APU* apu);