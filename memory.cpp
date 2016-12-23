/*******************************************************************
*   memory.cpp
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

#include "NES.h"

constexpr uint8_t length_tbl[] = {
	10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
	12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

constexpr uint8_t dmc_tbl[] = {
	214, 190, 170, 160, 143, 127, 113, 107, 95, 80, 71, 64, 53, 42, 36, 27
};

constexpr uint16_t noise_tbl[] = {
	4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

constexpr uint16_t mirror_tbl[5][4] = {
	{ 0, 0, 1, 1 },
	{ 0, 1, 0, 1 },
	{ 0, 0, 0, 0 },
	{ 1, 1, 1, 1 },
	{ 0, 1, 2, 3 }
};

uint8_t readPPURegister(NES* nes, uint16_t address) {
	PPU* ppu = nes->ppu;
	// PPUSTATUS
	if (address == 0x2002) {
		uint8_t status = ppu->reg & 0x1F;
		status |= ppu->flag_sprite_overflow << 5;
		status |= ppu->flag_sprite_zero_hit << 6;
		if (ppu->nmi_occurred) {
			status |= 1 << 7;
		}
		ppu->nmi_occurred = false;
		PPUnmiShift(ppu);
		ppu->w = 0;
		return status;
	}
	else if (address == 0x2004) {
		return ppu->oam_tbl[ppu->oam_addr];
	}
	else if (address == 0x2007) {
		uint8_t value = readPPU(nes, ppu->v);
		// buffered read
		if ((ppu->v & 16383) < 0x3F00) {
			uint8_t buffered = ppu->buffered_data;
			ppu->buffered_data = value;
			value = buffered;
		}
		else {
			ppu->buffered_data = readPPU(nes, ppu->v - 0x1000);
		}

		ppu->v += ppu->flag_increment == 0 ? 1 : 32;
		return value;
	}
	return 0;
}

uint8_t readController(Controller* c) {
	const uint8_t value = (c->index < 8 && ((c->buttons >> c->index) & 1));
	++c->index;
	if ((c->strobe & 1) == 1 ){
		c->index = 0;
	}
	return value;
}

uint8_t readByte(NES* nes, uint16_t address) {
	if (address < 0x2000) {
		return nes->RAM[address & 2047];
	}
	else if (address < 0x4000) {
		return readPPURegister(nes, 0x2000 + (address & 7));
	}
	else if (address == 0x4014) {
		return readPPURegister(nes, address);
	}
	else if (address == 0x4015) {
		// apu reg read
		APU* apu = nes->apu;
		uint8_t read_status = 0;
		if (apu->pulse1.length_val > 0) {
			read_status |= 1;
		}
		if (apu->pulse2.length_val > 0) {
			read_status |= 2;
		}
		if (apu->triangle.length_val > 0) {
			read_status |= 4;
		}
		if (apu->noise.length_val > 0) {
			read_status |= 8;
		}
		if (apu->dmc.cur_len > 0) {
			read_status |= 16;
		}
		return 0;
	}
	else if (address == 0x4016) {
		return readController(nes->controller1);
	}
	else if (address == 0x4017) {
		return readController(nes->controller2);
	}
	else if (address < 0x6000) {
		// I/O registers
	}
	else if (address >= 0x6000) {
		return nes->mapper->read(nes->cartridge, address);
	}
	else {
		std::cerr << "ERROR: CPU encountered unrecognized read (address 0x" << std::hex << address << std::dec << ')' << std::endl;
	}
	return 0;
}

void writeController(Controller* c, uint8_t value) {
	c->strobe = value;
	if ((c->strobe & 1) == 1) {
		c->index = 0;
	}
}

void pulseWriteControl(Pulse* p, uint8_t value) {
	p->duty_mode = (value >> 6) & 3;
	p->length_enabled = ((value >> 5) & 1) == 0;
	p->envelope_loop = ((value >> 5) & 1) == 1;
	p->envelope_enabled = ((value >> 4) & 1) == 0;
	p->envelope_period = value & 15;
	p->const_vol = value & 15;
	p->envelope_start = true;
}

void pulseWriteSweep(Pulse* p, uint8_t value) {
	p->sweep_enabled = ((value >> 7) & 1) == 1;
	p->sweep_period = ((value >> 4) & 7) + 1;
	p->sweep_negate = ((value >> 3) & 1) == 1;
	p->sweep_shift = value & 7;
	p->sweep_reload = true;
}

void pulseWriteTimerHigh(Pulse* p, uint8_t value) {
	p->length_val = length_tbl[value >> 3];
	p->timer_period = (p->timer_period & 0x00FF) | (static_cast<uint16_t>(value & 7) << 8);
	p->envelope_start = true;
	p->duty_val = 0;
}

void writeRegisterAPU(APU* apu, uint16_t address, uint8_t value) {
	switch (address) {
	case 0x4000:
		pulseWriteControl(&apu->pulse1, value);
		break;
	case 0x4001:
		pulseWriteSweep(&apu->pulse1, value);
		break;
	case 0x4002:
		apu->pulse1.timer_period = (apu->pulse1.timer_period & 0xFF00) | static_cast<uint16_t>(value);
		break;
	case 0x4003:
		pulseWriteTimerHigh(&apu->pulse1, value);
		break;
	case 0x4004:
		pulseWriteControl(&apu->pulse2, value);
		break;
	case 0x4005:
		pulseWriteSweep(&apu->pulse2, value);
		break;
	case 0x4006:
		apu->pulse2.timer_period = (apu->pulse2.timer_period & 0xFF00) | static_cast<uint16_t>(value);
		break;
	case 0x4007:
		pulseWriteTimerHigh(&apu->pulse2, value);
		break;
	case 0x4008:
		apu->triangle.length_enabled = ((value >> 7) & 1) == 0;
		apu->triangle.counter_period = value & 0x7F;
		break;
	case 0x4009:
	case 0x4010:
		apu->dmc.irq = (value & 0x80) == 0x80;
		apu->dmc.loop = (value & 0x40) == 0x40;
		apu->dmc.tick_period = dmc_tbl[value & 0x0F];
		break;
	case 0x4011:
		apu->dmc.value = value & 0x7F;
		break;
	case 0x4012:
		apu->dmc.samp_addr = 0xC000 | (static_cast<uint16_t>(value) << 6);
		break;
	case 0x4013:
		apu->dmc.samp_len = (static_cast<uint16_t>(value) << 4) | 1;
		break;
	case 0x400A:
		apu->triangle.timer_period = (apu->triangle.timer_period & 0xFF00) | static_cast<uint16_t>(value);
		break;
	case 0x400B:
		apu->triangle.length_val = length_tbl[value >> 3];
		apu->triangle.timer_period = (apu->triangle.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 7) << 8);
		apu->triangle.timer_val = apu->triangle.timer_period;
		apu->triangle.counter_reload = true;
		break;
	case 0x400C:
		apu->noise.length_enabled = ((value >> 5) & 1) == 0;
		apu->noise.envelope_loop = ((value >> 5) & 1) == 1;
		apu->noise.envelope_enabled = ((value >> 4) & 1) == 0;
		apu->noise.envelope_period = value & 15;
		apu->noise.const_vol = value & 15;
		apu->noise.envelope_start = true;
		break;
	case 0x400D:
	case 0x400E:
		apu->noise.mode = (value & 0x80) == 0x80;
		apu->noise.timer_period = noise_tbl[value & 0x0F];
		break;
	case 0x400F:
		apu->noise.length_val = length_tbl[value >> 3];
		apu->noise.envelope_start = true;
		break;
	case 0x4015:
		apu->pulse1.enabled = (value & 1) == 1;
		apu->pulse2.enabled = (value & 2) == 2;
		apu->triangle.enabled = (value & 4) == 4;
		apu->noise.enabled = (value & 8) == 8;
		apu->dmc.enabled = (value & 16) == 16;
		if (!apu->pulse1.enabled) {
			apu->pulse1.length_val = 0;
		}
		if (!apu->pulse2.enabled) {
			apu->pulse2.length_val = 0;
		}
		if (!apu->triangle.enabled) {
			apu->triangle.length_val = 0;
		}
		if (!apu->noise.enabled) {
			apu->noise.length_val = 0;
		}
		if (!apu->dmc.enabled) {
			apu->dmc.cur_len = 0;
		}
		else {
			if (apu->dmc.cur_len == 0) {
				dmcRestart(&apu->dmc);
			}
		}
		break;
	case 0x4017:
		apu->frame_period = 4 + ((value >> 7) & 1);
		apu->frame_IRQ = ((value >> 6) & 1) == 0;
		if (apu->frame_period == 5) {
			tickEnvelope(apu);
			tickSweep(apu);
			tickLength(apu);
		}
		break;
	}
}

int Mapper1::prgBankOffset(Cartridge* c, int index) {
	if (index >= 0x80) {
		index -= 0x100;
	}
	index %= c->prg_size >> 14;
	int offset = index << 14;
	if (offset < 0) {
		offset += c->prg_size;
	}
	return offset;
}

int Mapper1::chrBankOffset(Cartridge* cartridge, int index) {
	if (index >= 0x80) {
		index -= 0x100;
	}
	index %= cartridge->chr_size >> 12;
	int offset = index << 12;
	if (offset < 0) {
		offset += cartridge->chr_size;
	}
	return offset;
}

// PRG ROM bank mode  0-1: switch 32k at $8000,     ignore low bit of bank number
//                    2  : fix first bank at $8000, switch 16k bank at $C000
//                    3  : fix last bank at $C000,  switch 16k bank at $8000
//
// CHR ROM bank mode  0  : switch 8k
//                    1  : switch two 4k banks
void Mapper1::updateOffsets(Cartridge* cartridge) {
	switch (prg_mode) {
	case 0:
	case 1:
		prg_offsets[0] = prgBankOffset(cartridge, static_cast<int>(prg_bank & 0xFE));
		prg_offsets[1] = prgBankOffset(cartridge, static_cast<int>(prg_bank | 0x01));
		break;
	case 2:
		prg_offsets[0] = 0;
		prg_offsets[1] = prgBankOffset(cartridge, static_cast<int>(prg_bank));
		break;
	case 3:
		prg_offsets[0] = prgBankOffset(cartridge, static_cast<int>(prg_bank));
		prg_offsets[1] = prgBankOffset(cartridge, -1);
		break;
	}

	switch (chr_mode) {
	case 0:
		chr_offsets[0] = chrBankOffset(cartridge, static_cast<int>(chr_bank0 & 0xFE));
		chr_offsets[1] = chrBankOffset(cartridge, static_cast<int>(chr_bank0 | 0x01));
		break;
	case 1:
		chr_offsets[0] = chrBankOffset(cartridge, static_cast<int>(chr_bank0));
		chr_offsets[1] = chrBankOffset(cartridge, static_cast<int>(chr_bank1));
		break;
	}
}

// Control ($8000-$9FFF)
void Mapper1::writeCtrl(Cartridge* cartridge, uint8_t value) {
	control = value;
	chr_mode = (value >> 4) & 1;
	prg_mode = (value >> 2) & 3;
	uint8_t mirror = value & 3;
	switch (mirror) {
	case 0:
		cartridge->mirror = MirrorSingle0;
		break;
	case 1:
		cartridge->mirror = MirrorSingle1;
		break;
	case 2:
		cartridge->mirror = MirrorVertical;
		break;
	case 3:
		cartridge->mirror = MirrorHorizontal;
		break;
	}
}

int Mapper4::prgBankOffset(Cartridge* c, int index) {
	if (index >= 0x80) {
		index -= 0x100;
	}
	index %= c->prg_size >> 13;
	int offset = index << 13;
	if (offset < 0) {
		offset += c->prg_size;
	}
	return offset;
}

int Mapper4::chrBankOffset(Cartridge* cartridge, int index) {
	if (index >= 0x80) {
		index -= 0x100;
	}
	index %= cartridge->chr_size >> 10;
	int offset = index << 10;
	if (offset < 0) {
		offset += cartridge->chr_size;
	}
	return offset;
}

void Mapper4::updateOffsets(Cartridge* cartridge) {
	switch (prg_mode) {
	case 0:
		prg_offsets[0] = prgBankOffset(cartridge, static_cast<int>(regs[6]));
		prg_offsets[1] = prgBankOffset(cartridge, static_cast<int>(regs[7]));
		prg_offsets[2] = prgBankOffset(cartridge, -2);
		prg_offsets[3] = prgBankOffset(cartridge, -1);
		break;
	case 1:
		prg_offsets[0] = prgBankOffset(cartridge, -2);
		prg_offsets[1] = prgBankOffset(cartridge, static_cast<int>(regs[7]));
		prg_offsets[2] = prgBankOffset(cartridge, static_cast<int>(regs[6]));
		prg_offsets[3] = prgBankOffset(cartridge, -1);
		break;
	}

	switch (chr_mode) {
	case 0:
		chr_offsets[0] = chrBankOffset(cartridge, static_cast<int>(regs[0] & 0xFE));
		chr_offsets[1] = chrBankOffset(cartridge, static_cast<int>(regs[0] | 0x01));
		chr_offsets[2] = chrBankOffset(cartridge, static_cast<int>(regs[1] & 0xFE));
		chr_offsets[3] = chrBankOffset(cartridge, static_cast<int>(regs[1] | 0x01));
		chr_offsets[4] = chrBankOffset(cartridge, static_cast<int>(regs[2]));
		chr_offsets[5] = chrBankOffset(cartridge, static_cast<int>(regs[3]));
		chr_offsets[6] = chrBankOffset(cartridge, static_cast<int>(regs[4]));
		chr_offsets[7] = chrBankOffset(cartridge, static_cast<int>(regs[5]));
		break;
	case 1:
		chr_offsets[0] = chrBankOffset(cartridge, static_cast<int>(regs[2]));
		chr_offsets[1] = chrBankOffset(cartridge, static_cast<int>(regs[3]));
		chr_offsets[2] = chrBankOffset(cartridge, static_cast<int>(regs[4]));
		chr_offsets[3] = chrBankOffset(cartridge, static_cast<int>(regs[5]));
		chr_offsets[4] = chrBankOffset(cartridge, static_cast<int>(regs[0] & 0xFE));
		chr_offsets[5] = chrBankOffset(cartridge, static_cast<int>(regs[0] | 0x01));
		chr_offsets[6] = chrBankOffset(cartridge, static_cast<int>(regs[1] & 0xFE));
		chr_offsets[7] = chrBankOffset(cartridge, static_cast<int>(regs[1] | 0x01));
		break;
	}
}

NES::NES(const char* path, const char* SRAM_path) : initialized(false) {
	std::cout << "Initializing cartridge..." << std::endl;
	cartridge = new Cartridge(path, SRAM_path);
	if (!cartridge->initialized) return;

	std::cout << "Initializing controllers..." << std::endl;
	controller1 = new Controller;
	controller2 = new Controller;

	RAM = new uint8_t[2048];
	memset(RAM, 0, 2048);

	std::cout << "Initializing mapper..." << std::endl;
	if (cartridge->mapper == 0) {
		const int prg_banks = cartridge->prg_size >> 14;
		mapper = new Mapper2(prg_banks, 0, prg_banks - 1);
	}
	else if (cartridge->mapper == 1) {
		Mapper1* m = new Mapper1();
		m->shift_reg = 0x10;
		m->prg_offsets[1] = m->prgBankOffset(cartridge, -1);
		mapper = m;
	}
	else if (cartridge->mapper == 2) {
		const int prg_banks = cartridge->prg_size >> 14;
		mapper = new Mapper2(prg_banks, 0, prg_banks - 1);
	}
	else if (cartridge->mapper == 3) {
		const int prg_banks = cartridge->prg_size >> 14;
		mapper = new Mapper3(0, 0, prg_banks - 1);
	}
	else if (cartridge->mapper == 4) {
		Mapper4* m = new Mapper4();
		m->prg_offsets[0] = m->prgBankOffset(cartridge, 0);
		m->prg_offsets[1] = m->prgBankOffset(cartridge, 1);
		m->prg_offsets[2] = m->prgBankOffset(cartridge, -2);
		m->prg_offsets[3] = m->prgBankOffset(cartridge, -1);
		mapper = m;
	}
	else if (cartridge->mapper == 7) {
		mapper = new Mapper7();
	}
	else {
		std::cerr << "ERROR: cartridge uses Mapper " << static_cast<int>(cartridge->mapper) << ", which isn't currently supported by KNES!" << std::endl;
		return;
	}

	std::cout << "Mapper " << static_cast<int>(cartridge->mapper) << " activated." << std::endl;

	std::cout << "Initializing NES CPU..." << std::endl;
	cpu = new CPU();

	cpu->PC = read16(this, 0xFFFC);
	cpu->SP = 0xFD;
	cpu->flags = 0x24;

	std::cout << "Initializing NES APU..." << std::endl;
	apu = new APU();
	apu->noise.shift_reg = 1;
	apu->pulse1.channel = 1;
	apu->pulse2.channel = 2;

	std::cout << "Initializing NES PPU..." << std::endl;
	ppu = new PPU();
	ppu->front = new uint32_t[256 * 240];
	ppu->back = new uint32_t[256 * 240];
	ppu->cycle = 340;
	ppu->scanline = 250;
	ppu->frame = 0;

	writePPUCtrl(ppu, 0);
	writePPUMask(ppu, 0);
	ppu->oam_addr = 0;
	initialized = true;
}

uint16_t mirrorAddress(uint8_t mode, uint16_t address) {
	address = (address - 0x2000) & 4095;
	const uint16_t table = address >> 10;
	const uint16_t offset = address & 1023;
	return 0x2000 + (mirror_tbl[mode][table] << 10) + offset;
}

void writePPU(NES* nes, uint16_t address, uint8_t value) {
	address &= 16383;
	if (address < 0x2000) {
		nes->mapper->write(nes->cartridge, address, value);
	}
	else if (address < 0x3F00) {
		const uint8_t mode = nes->cartridge->mirror;
		nes->ppu->name_tbl[mirrorAddress(mode, address) & 2047] = value;
	}
	else if (address < 0x4000) {
		// palette
		address &= 31;
		if (address >= 16 && (address & 3) == 0) {
			address -= 16;
		}
		nes->ppu->palette_tbl[address] = value;
	}
	else {
		std::cerr << "ERROR: PPU encountered unrecognized write (address 0x" << std::hex << address << std::dec << ')' << std::endl;
	}
}

void writeRegisterPPU(NES* nes, uint16_t address, uint8_t value) {
	PPU* ppu = nes->ppu;
	ppu->reg = value;
	switch (address) {
	case 0x2000:
		writePPUCtrl(ppu, value);
		break;
	case 0x2001:
		writePPUMask(ppu, value);
		break;
	case 0x2003:
		ppu->oam_addr = value;
		break;
	case 0x2004:
		ppu->oam_tbl[ppu->oam_addr] = value;
		++ppu->oam_addr;
		break;
	case 0x2005:
		// scroll
		if (ppu->w == 0) {
			ppu->t = (ppu->t & 0xFFE0) | (static_cast<uint16_t>(value) >> 3);
			ppu->x = value & 7;
			ppu->w = 1;
		}
		else {
			ppu->t = (ppu->t & 0x8FFF) | ((static_cast<uint16_t>(value) & 0x07) << 12);
			ppu->t = (ppu->t & 0xFC1F) | ((static_cast<uint16_t>(value) & 0xF8) << 2);
			ppu->w = 0;
		}
		break;
	case 0x2006:
		if (ppu->w == 0) {
			ppu->t = (ppu->t & 0x80FF) | ((static_cast<uint16_t>(value) & 0x3F) << 8);
			ppu->w = 1;
		}
		else {
			ppu->t = (ppu->t & 0xFF00) | static_cast<uint16_t>(value);
			ppu->v = ppu->t;
			ppu->w = 0;
		}
		break;
	case 0x2007:
		writePPU(nes, ppu->v, value);
		ppu->v += ppu->flag_increment == 0 ? 1 : 32;
		break;
	case 0x4014:
		// DMA
		CPU* cpu = nes->cpu;
		address = static_cast<uint16_t>(value) << 8;
		for (int i = 0; i < 256; ++i) {
			ppu->oam_tbl[ppu->oam_addr] = readByte(nes, address);
			++ppu->oam_addr;
			++address;
		}
		cpu->stall += 513;
		if (cpu->cycles & 1) {
			++cpu->stall;
		}
	}
}

void writeByte(NES* nes, uint16_t address, uint8_t value) {
	if (address < 0x2000) {
		nes->RAM[address & 2047] = value;
	}
	else if (address < 0x4000) {
		writeRegisterPPU(nes, 0x2000 + (address & 7), value);
	}
	else if (address < 0x4014) {
		writeRegisterAPU(nes->apu, address, value);
	}
	else if (address == 0x4014) {
		writeRegisterPPU(nes, address, value);
	}
	else if (address == 0x4015) {
		writeRegisterAPU(nes->apu, address, value);
	}
	else if (address == 0x4016) {
		writeController(nes->controller1, value);
		writeController(nes->controller2, value);
	}
	else if (address == 0x4017) {
		writeRegisterAPU(nes->apu, address, value);
	}
	else if (address < 0x6000) {
		// I/O registers
	}
	else if (address >= 0x6000) {
		nes->mapper->write(nes->cartridge, address, value);
	}
	else {
		std::cerr << "ERROR: CPU encountered unrecognized write (address 0x" << std::hex << address << std::dec << ')' << std::endl;
	}
}

uint8_t readPPU(NES* nes, uint16_t address) {
	address &= 16383;
	if (address < 0x2000) {
		return nes->mapper->read(nes->cartridge, address);
	}
	else if (address < 0x3F00) {
		uint8_t mode = nes->cartridge->mirror;
		return nes->ppu->name_tbl[mirrorAddress(mode, address) & 2047];
	}
	else if (address < 0x4000) {
		return readPalette(nes->ppu, address & 31);
	}
	else {
		std::cerr << "ERROR: PPU encountered unrecognized read (address 0x" << std::hex << address << std::dec << ')' << std::endl;
	}
	return 0;
}

uint8_t readPalette(PPU* ppu, uint16_t address) {
	if (address >= 16 && (address & 3) == 0) {
		address -= 16;
	}
	return ppu->palette_tbl[address];
}

// $2000: PPUCTRL
void writePPUCtrl(PPU* ppu, uint8_t value) {
	ppu->flag_name_tbl = value & 3;
	ppu->flag_increment = (value >> 2) & 1;
	ppu->flag_sprite_tbl = (value >> 3) & 1;
	ppu->flag_background_tbl = (value >> 4) & 1;
	ppu->flag_sprite_size = (value >> 5) & 1;
	ppu->flag_rw = (value >> 6) & 1;
	ppu->nmi_out = ((value >> 7) & 1) == 1;
	PPUnmiShift(ppu);
	ppu->t = (ppu->t & 0xF3FF) | ((static_cast<uint16_t>(value) & 3) << 10);
}

// $2001: PPUMASK
void writePPUMask(PPU* ppu, uint8_t value) {
	ppu->flag_gray = value & 1;
	ppu->flag_show_left_background = (value >> 1) & 1;
	ppu->flag_show_left_sprites = (value >> 2) & 1;
	ppu->flag_show_background = (value >> 3) & 1;
	ppu->flag_show_sprites = (value >> 4) & 1;
	ppu->flag_red_tint = (value >> 5) & 1;
	ppu->flag_green_tint = (value >> 6) & 1;
	ppu->flag_blue_tint = (value >> 7) & 1;
}
