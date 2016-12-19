/*******************************************************************
*   cpu.cpp
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

// set zero flag if 'value' is zero
void setZ(CPU* cpu, uint8_t value) {
	cpu->flags = (cpu->flags & (~(1 << 1))) | (static_cast<uint8_t>(value == 0) << 1);
}

// get zero flag
uint8_t getZ(CPU* cpu) {
	return (cpu->flags & 2) >> 1;
}

// set negative flag if 'value' is negative
void setN(CPU* cpu, uint8_t value) {
	cpu->flags = (cpu->flags & (~(1 << 7))) | (value & 128);
}

// get negative flag
uint8_t getN(CPU* cpu) {
	return (cpu->flags & 128) >> 7;
}

// set zero and negative flags according to 'value'
void setZN(CPU* cpu, uint8_t value) {
	setZ(cpu, value);
	setN(cpu, value);
}

// set carry flag if 'value' is true
void setC(CPU* cpu, bool value) {
	cpu->flags = (cpu->flags & (~(1))) | static_cast<uint8_t>(value);
}

// get carry flag
uint8_t getC(CPU* cpu) {
	return cpu->flags & 1;
}

// set interrupt disable flag if 'value' is true
void setI(CPU* cpu, bool value) {
	cpu->flags = (cpu->flags & (~(1 << 2))) | (static_cast<uint8_t>(value) << 2);
}

// get interrupt disable flag
uint8_t getI(CPU* cpu) {
	return (cpu->flags & 4) >> 2;
}

// set decimal flag if 'value' is true
void setD(CPU* cpu, bool value) {
	cpu->flags = (cpu->flags & (~(1 << 3))) | (static_cast<uint8_t>(value) << 3);
}

// get decimal flag
uint8_t getD(CPU* cpu) {
	return (cpu->flags & 8) >> 3;
}

// set software interrupt flag if 'value' is true
void setB(CPU* cpu, bool value) {
	cpu->flags = (cpu->flags & (~(1 << 4))) | (static_cast<uint8_t>(value) << 4);
}

// get software interrupt flag
uint8_t getB(CPU* cpu) {
	return (cpu->flags & 16) >> 4;
}

// set overflow flag if 'value' is true
void setV(CPU* cpu, bool value) {
	cpu->flags = (cpu->flags & (~(1 << 6))) | (static_cast<uint8_t>(value) << 6);
}

// get overflow flag
uint8_t getV(CPU* cpu) {
	return (cpu->flags & 64) >> 6;
}

// perform compare operation on a and b, setting
// flags Z, N, C accordingly
void compare(CPU* cpu, uint8_t a, uint8_t b) {
	setZN(cpu, a - b);
	setC(cpu, a >= b);
}

// push uint8_t onto stack
void push(NES* nes, uint8_t value) {
	CPU* cpu = nes->cpu;
	writeByte(nes, 0x100 | static_cast<uint16_t>(cpu->SP), value);
	--cpu->SP;
}

// pop uint8_t from stack
uint8_t pop(NES* nes) {
	CPU* cpu = nes->cpu;
	++cpu->SP;
	return readByte(nes, 0x100 | static_cast<uint16_t>(cpu->SP));
}

// push uint16_t onto stack
void push16(NES* nes, uint16_t value) {
	push(nes, static_cast<uint8_t>(value >> 8));
	push(nes, static_cast<uint8_t>(value));
}

// pop uint16_t onto stack
uint16_t pop16(NES* nes) {
	const uint8_t lo = static_cast<uint16_t>(pop(nes));
	const uint8_t hi = static_cast<uint16_t>(pop(nes));
	return (hi << 8) | lo;
}

// do addresses represent different pages?
bool pagesDiffer(uint16_t a, uint16_t b) {
	return (a & 0xFF00) != (b & 0xFF00);
}

// famous 6502 memory indirect jump bug: only the low byte wraps on an xxFF read instead of the whole word incrementing
uint16_t read16_ff_bug(NES* nes, uint16_t address) {
	const uint16_t a = address;
	const uint16_t b = (a & 0xFF00) | static_cast<uint16_t>(static_cast<uint8_t>(static_cast<uint8_t>(a) + 1));
	const uint8_t lo = readByte(nes, a);
	const uint8_t hi = readByte(nes, b);
	return (static_cast<uint16_t>(hi) << 8) | static_cast<uint16_t>(lo);
}

uint16_t read16(NES* nes, uint16_t address) {
	const uint8_t lo = static_cast<uint16_t>(readByte(nes, address));
	const uint8_t hi = static_cast<uint16_t>(readByte(nes, address + 1));
	return (hi << 8) | lo;
}

// 1 cycle for taking a branch
// 1 cycle if the branch is to a new page
void branchDelay(CPU* cpu, uint16_t address, uint16_t pc) {
	++cpu->cycles;
	if (pagesDiffer(pc, address)) {
		++cpu->cycles;
	}
}

// ADC - ADd with Carry
void adc(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t a = cpu->A;
	const uint8_t b = readByte(nes, address);
	const uint8_t c = getC(cpu);
	cpu->A = a + b + c;
	setZN(cpu, cpu->A);
	setC(cpu, static_cast<int>(a) + static_cast<int>(b) + static_cast<int>(c) > 0xFF);
	setV(cpu, ((a^b) & 0x80) == 0 && ((a^cpu->A) & 0x80) != 0);
}

// AND - logical AND
// Nonstandard name to disambiguate from 'and' label
void and_instruction(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	cpu->A &= readByte(nes, address);
	setZN(cpu, cpu->A);
}

// ASL - Arithmetic Shift Left
void asl(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	if (mode == modeAccumulator) {
		setC(cpu, (cpu->A >> 7) & 1);
		cpu->A <<= 1;
		setZN(cpu, cpu->A);
	}
	else {
		uint8_t value = readByte(nes, address);
		setC(cpu, (value >> 7) & 1);
		value <<= 1;
		writeByte(nes, address, value);
		setZN(cpu, value);
	}
}

// BIT - BIt Test
void bit(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t value = readByte(nes, address);
	setV(cpu, (value >> 6) & 1);
	setZ(cpu, value & cpu->A);
	setN(cpu, value);
}

// CMP - CoMPare
void cmp(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t value = readByte(nes, address);
	compare(cpu, cpu->A, value);
}

// CPX - ComPare X register
void cpx(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t value = readByte(nes, address);
	compare(cpu, cpu->X, value);
}

// CPY - ComPare Y register
void cpy(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t value = readByte(nes, address);
	compare(cpu, cpu->Y, value);
}

// DEC - DECrement memory
void dec(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t value = readByte(nes, address) - 1;
	writeByte(nes, address, value);
	setZN(cpu, value);
}


// EOR - Exclusive OR
void eor(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	cpu->A ^= readByte(nes, address);
	setZN(cpu, cpu->A);
}

// INC - INCrement memory
void inc(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t value = readByte(nes, address) + 1;
	writeByte(nes, address, value);
	setZN(cpu, value);
}

// JMP - JuMP
void jmp(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	cpu->PC = address;
}

// LDA - LoaD Accumulator
void lda(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	cpu->A = readByte(nes, address);
	setZN(cpu, cpu->A);
}

// LDX - LoaD X register
void ldx(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	cpu->X = readByte(nes, address);
	setZN(cpu, cpu->X);
}

// LDY - LoaD Y register
void ldy(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	cpu->Y = readByte(nes, address);
	setZN(cpu, cpu->Y);
}

// LSR - Logical Shift Right
void lsr(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	if (mode == modeAccumulator) {
		setC(cpu, cpu->A & 1);
		cpu->A >>= 1;
		setZN(cpu, cpu->A);
	}
	else {
		uint8_t value = readByte(nes, address);
		setC(cpu, value & 1);
		value >>= 1;
		writeByte(nes, address, value);
		setZN(cpu, value);
	}
}

// ORA - logical OR with Accumulator
void ora(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	cpu->A |= readByte(nes, address);
	setZN(cpu, cpu->A);
}

// PHP - PusH Processor status
void php(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(address);
	static_cast<void>(mode);
	push(nes, cpu->flags | 0x10);
}

// ROL - ROtate Left
void rol(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	if (mode == modeAccumulator) {
		const uint8_t c = getC(cpu);
		setC(cpu, (cpu->A >> 7) & 1);
		cpu->A = (cpu->A << 1) | c;
		setZN(cpu, cpu->A);
	}
	else {
		const uint8_t c = getC(cpu);
		uint8_t value = readByte(nes, address);
		setC(cpu, (value >> 7) & 1);
		value = (value << 1) | c;
		writeByte(nes, address, value);
		setZN(cpu, value);
	}
}

// ROR - ROtate Right
void ror(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	if (mode == modeAccumulator) {
		const uint8_t c = getC(cpu);
		setC(cpu, cpu->A & 1);
		cpu->A = (cpu->A >> 1) | (c << 7);
		setZN(cpu, cpu->A);
	}
	else {
		const uint8_t c = getC(cpu);
		uint8_t value = readByte(nes, address);
		setC(cpu, value & 1);
		value = (value >> 1) | (c << 7);
		writeByte(nes, address, value);
		setZN(cpu, value);
	}
}

// SBC - SuBtract with Carry
void sbc(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	const uint8_t a = cpu->A;
	const uint8_t b = readByte(nes, address);
	const uint8_t c = getC(cpu);
	cpu->A = a - b - (1 - c);
	setZN(cpu, cpu->A);
	setC(cpu, static_cast<int>(a) - static_cast<int>(b) - static_cast<int>(1 - c) >= 0);
	setV(cpu, ((a^b) & 0x80) != 0 && ((a^cpu->A) & 0x80) != 0);
}

// SEI - SEt Interrupt disable
void sei(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	setI(cpu, true);
}

// STA - STore Accumulator
void sta(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	writeByte(nes, address, cpu->A);
}

// STX - Store X Register
void stx(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	writeByte(nes, address, cpu->X);
}

// STY - STore Y Register
void sty(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	writeByte(nes, address, cpu->Y);
}

// BRK - force interrupt BReaK
void brk(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	push16(nes, cpu->PC);
	php(cpu, nes, address, mode);
	sei(cpu, nes, address, mode);
	cpu->PC = read16(nes, 0xFFFE);
}

// BPL - Branch if PLus (i.e. if positive)
void bpl(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getN(cpu) == 0) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// CLC - CLear Carry flag
void clc(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	setC(cpu, false);
}

// JSR - Jump to SubRoutine   
void jsr(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(mode);
	push16(nes, cpu->PC - 1);
	cpu->PC = address;
}

// PLP - PuLl Processor status
void plp(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->flags = (pop(nes) & 0xEF) | 0x20;
}

// BMI - Branch if MInus (i.e. if negative)
void bmi(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getN(cpu)) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// SEC - SEt Carry flag
void sec(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	setC(cpu, true);
}

// RTI - ReTurn from Interrupt
void rti(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->flags = (pop(nes) & 0xEF) | 0x20;
	cpu->PC = pop16(nes);
}

// BVC - Branch if oVerflow Clear
void bvc(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getV(cpu) == 0) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// PHA - PusH Accumulator
void pha(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(address);
	static_cast<void>(mode);
	push(nes, cpu->A);
}

// CLI - CLear Interrupt disable
void cli(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	setI(cpu, false);
}

// RTS - ReTurn from Subroutine
void rts(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->PC = pop16(nes) + 1;
}

// PLA - PuLl Accumulator
void pla(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->A = pop(nes);
	setZN(cpu, cpu->A);
}

// BVS - Branch if oVerflow Set
void bvs(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getV(cpu)) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// DEY - DEcrement Y register
void dey(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	--cpu->Y;
	setZN(cpu, cpu->Y);
}

// TXA - Transfer X to Accumulator
void txa(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->A = cpu->X;
	setZN(cpu, cpu->A);
}

// BCC - Branch if Carry Clear
void bcc(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getC(cpu) == 0) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// TYA - Transfer Y to Accumulator
void tya(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->A = cpu->Y;
	setZN(cpu, cpu->A);
}

// BCS - Branch if Carry Set
void bcs(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getC(cpu)) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// TAY - Transfer Accumulator to Y
void tay(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->Y = cpu->A;
	setZN(cpu, cpu->Y);
}

// TXS - Transfer X to Stack pointer
void txs(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->SP = cpu->X;
}

// TAX - Transfer Accumulator to X
void tax(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->X = cpu->A;
	setZN(cpu, cpu->X);
}

// CLV - CLear oVerflow flag
void clv(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	setV(cpu, false);
}

// TSX - Transfer Stack pointer to X
void tsx(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	cpu->X = cpu->SP;
	setZN(cpu, cpu->X);
}

// INY - INcrement Y register
void iny(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	++cpu->Y;
	setZN(cpu, cpu->Y);
}

// DEX - DEcrement X register
void dex(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	--cpu->X;
	setZN(cpu, cpu->X);
}

// BNE - Branch if Not Equal
void bne(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getZ(cpu) == 0) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// CLD - CLear Decimal mode
void cld(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	setD(cpu, false);
}

// INX - INcrement X register
void inx(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	++cpu->X;
	setZN(cpu, cpu->X);
}

// BEQ - Branch if EQual
void beq(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(mode);
	if (getZ(cpu)) {
		const uint16_t pc = cpu->PC;
		cpu->PC = address;
		branchDelay(cpu, address, pc);
	}
}

// SED - SEt Decimal flag
void sed(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
	setD(cpu, true);
}

// NOP - No OPeration
void nop(CPU* cpu, NES* nes, uint16_t address, uint8_t mode) {
	static_cast<void>(cpu);
	static_cast<void>(nes);
	static_cast<void>(address);
	static_cast<void>(mode);
}

constexpr Instruction instructions[256] = {
	{ 0  , "BRK", brk, 6, 1, 7, 0 },
	{ 1  , "ORA", ora, 7, 2, 6, 0 },
	{ 2  , "KIL", nop, 6, 0, 2, 0 },
	{ 3  , "SLO", nop, 7, 0, 8, 0 },
	{ 4  , "NOP", nop, 11, 2, 3, 0 },
	{ 5  , "ORA", ora, 11, 2, 3, 0 },
	{ 6  , "ASL", asl, 11, 2, 5, 0 },
	{ 7  , "SLO", nop, 11, 0, 5, 0 },
	{ 8  , "PHP", php, 6, 1, 3, 0 },
	{ 9  , "ORA", ora, 5, 2, 2, 0 },
	{ 10 , "ASL", asl, 4, 1, 2, 0 },
	{ 11 , "ANC", nop, 5, 0, 2, 0 },
	{ 12 , "NOP", nop, 1, 3, 4, 0 },
	{ 13 , "ORA", ora, 1, 3, 4, 0 },
	{ 14 , "ASL", asl, 1, 3, 6, 0 },
	{ 15 , "SLO", nop, 1, 0, 6, 0 },
	{ 16 , "BPL", bpl, 10, 2, 2, 1 },
	{ 17 , "ORA", ora, 9, 2, 5, 1 },
	{ 18 , "KIL", nop, 6, 0, 2, 0 },
	{ 19 , "SLO", nop, 9, 0, 8, 0 },
	{ 20 , "NOP", nop, 12, 2, 4, 0 },
	{ 21 , "ORA", ora, 12, 2, 4, 0 },
	{ 22 , "ASL", asl, 12, 2, 6, 0 },
	{ 23 , "SLO", nop, 12, 0, 6, 0 },
	{ 24 , "CLC", clc, 6, 1, 2, 0 },
	{ 25 , "ORA", ora, 3, 3, 4, 1 },
	{ 26 , "NOP", nop, 6, 1, 2, 0 },
	{ 27 , "SLO", nop, 3, 0, 7, 0 },
	{ 28 , "NOP", nop, 2, 3, 4, 1 },
	{ 29 , "ORA", ora, 2, 3, 4, 1 },
	{ 30 , "ASL", asl, 2, 3, 7, 0 },
	{ 31 , "SLO", nop, 2, 0, 7, 0 },
	{ 32 , "JSR", jsr, 1, 3, 6, 0 },
	{ 33 , "AND", and_instruction, 7, 2, 6, 0 },
	{ 34 , "KIL", nop, 6, 0, 2, 0 },
	{ 35 , "RLA", nop, 7, 0, 8, 0 },
	{ 36 , "BIT", bit, 11, 2, 3, 0 },
	{ 37 , "AND", and_instruction, 11, 2, 3, 0 },
	{ 38 , "ROL", rol, 11, 2, 5, 0 },
	{ 39 , "RLA", nop, 11, 0, 5, 0 },
	{ 40 , "PLP", plp, 6, 1, 4, 0 },
	{ 41 , "AND", and_instruction, 5, 2, 2, 0 },
	{ 42 , "ROL", rol, 4, 1, 2, 0 },
	{ 43 , "ANC", nop, 5, 0, 2, 0 },
	{ 44 , "BIT", bit, 1, 3, 4, 0 },
	{ 45 , "AND", and_instruction, 1, 3, 4, 0 },
	{ 46 , "ROL", rol, 1, 3, 6, 0 },
	{ 47 , "RLA", nop, 1, 0, 6, 0 },
	{ 48 , "BMI", bmi, 10, 2, 2, 1 },
	{ 49 , "AND", and_instruction, 9, 2, 5, 1 },
	{ 50 , "KIL", and_instruction, 6, 0, 2, 0 },
	{ 51 , "RLA", nop, 9, 0, 8, 0 },
	{ 52 , "NOP", nop, 12, 2, 4, 0 },
	{ 53 , "AND", nop, 12, 2, 4, 0 },
	{ 54 , "ROL", rol, 12, 2, 6, 0 },
	{ 55 , "RLA", nop, 12, 0, 6, 0 },
	{ 56 , "SEC", sec, 6, 1, 2, 0 },
	{ 57 , "AND", and_instruction, 3, 3, 4, 1 },
	{ 58 , "NOP", nop, 6, 1, 2, 0 },
	{ 59 , "RLA", nop, 3, 0, 7, 0 },
	{ 60 , "NOP", nop, 2, 3, 4, 1 },
	{ 61 , "AND", and_instruction, 2, 3, 4, 1 },
	{ 62 , "ROL", rol, 2, 3, 7, 0 },
	{ 63 , "RLA", nop, 2, 0, 7, 0 },
	{ 64 , "RTI", rti, 6, 1, 6, 0 },
	{ 65 , "EOR", eor, 7, 2, 6, 0 },
	{ 66 , "KIL", nop, 6, 0, 2, 0 },
	{ 67 , "SRE", nop, 7, 0, 8, 0 },
	{ 68 , "NOP", nop, 11, 2, 3, 0 },
	{ 69 , "EOR", eor, 11, 2, 3, 0 },
	{ 70 , "LSR", lsr, 11, 2, 5, 0 },
	{ 71 , "SRE", nop, 11, 0, 5, 0 },
	{ 72 , "PHA", pha, 6, 1, 3, 0 },
	{ 73 , "EOR", eor, 5, 2, 2, 0 },
	{ 74 , "LSR", lsr, 4, 1, 2, 0 },
	{ 75 , "ALR", nop, 5, 0, 2, 0 },
	{ 76 , "JMP", jmp, 1, 3, 3, 0 },
	{ 77 , "EOR", eor, 1, 3, 4, 0 },
	{ 78 , "LSR", lsr, 1, 3, 6, 0 },
	{ 79 , "SRE", nop, 1, 0, 6, 0 },
	{ 80 , "BVC", bvc, 10, 2, 2, 1 },
	{ 81 , "EOR", eor, 9, 2, 5, 1 },
	{ 82 , "KIL", nop, 6, 0, 2, 0 },
	{ 83 , "SRE", nop, 9, 0, 8, 0 },
	{ 84 , "NOP", nop, 12, 2, 4, 0 },
	{ 85 , "EOR", eor, 12, 2, 4, 0 },
	{ 86 , "LSR", lsr, 12, 2, 6, 0 },
	{ 87 , "SRE", nop, 12, 0, 6, 0 },
	{ 88 , "CLI", cli, 6, 1, 2, 0 },
	{ 89 , "EOR", eor, 3, 3, 4, 1 },
	{ 90 , "NOP", nop, 6, 1, 2, 0 },
	{ 91 , "SRE", nop, 3, 0, 7, 0 },
	{ 92 , "NOP", nop, 2, 3, 4, 1 },
	{ 93 , "EOR", eor, 2, 3, 4, 1 },
	{ 94 , "LSR", lsr, 2, 3, 7, 0 },
	{ 95 , "SRE", nop, 2, 0, 7, 0 },
	{ 96 , "RTS", rts, 6, 1, 6, 0 },
	{ 97 , "ADC", adc, 7, 2, 6, 0 },
	{ 98 , "KIL", nop, 6, 0, 2, 0 },
	{ 99 , "RRA", nop, 7, 0, 8, 0 },
	{ 100, "NOP", nop, 11, 2, 3, 0 },
	{ 101, "ADC", adc, 11, 2, 3, 0 },
	{ 102, "ROR", ror, 11, 2, 5, 0 },
	{ 103, "RRA", nop, 11, 0, 5, 0 },
	{ 104, "PLA", pla, 6, 1, 4, 0 },
	{ 105, "ADC", adc, 5, 2, 2, 0 },
	{ 106, "ROR", ror, 4, 1, 2, 0 },
	{ 107, "ARR", nop, 5, 0, 2, 0 },
	{ 108, "JMP", jmp, 8, 3, 5, 0 },
	{ 109, "ADC", adc, 1, 3, 4, 0 },
	{ 110, "ROR", ror, 1, 3, 6, 0 },
	{ 111, "RRA", nop, 1, 0, 6, 0 },
	{ 112, "BVS", bvs, 10, 2, 2, 1 },
	{ 113, "ADC", adc, 9, 2, 5, 1 },
	{ 114, "KIL", nop, 6, 0, 2, 0 },
	{ 115, "RRA", nop, 9, 0, 8, 0 },
	{ 116, "NOP", nop, 12, 2, 4, 0 },
	{ 117, "ADC", adc, 12, 2, 4, 0 },
	{ 118, "ROR", ror, 12, 2, 6, 0 },
	{ 119, "RRA", nop, 12, 0, 6, 0 },
	{ 120, "SEI", sei, 6, 1, 2, 0 },
	{ 121, "ADC", adc, 3, 3, 4, 1 },
	{ 122, "NOP", nop, 6, 1, 2, 0 },
	{ 123, "RRA", nop, 3, 0, 7, 0 },
	{ 124, "NOP", nop, 2, 3, 4, 1 },
	{ 125, "ADC", adc, 2, 3, 4, 1 },
	{ 126, "ROR", ror, 2, 3, 7, 0 },
	{ 127, "RRA", nop, 2, 0, 7, 0 },
	{ 128, "NOP", nop, 5, 2, 2, 0 },
	{ 129, "STA", sta, 7, 2, 6, 0 },
	{ 130, "NOP", nop, 5, 0, 2, 0 },
	{ 131, "SAX", nop, 7, 0, 6, 0 },
	{ 132, "STY", sty, 11, 2, 3, 0 },
	{ 133, "STA", sta, 11, 2, 3, 0 },
	{ 134, "STX", stx, 11, 2, 3, 0 },
	{ 135, "SAX", nop, 11, 0, 3, 0 },
	{ 136, "DEY", dey, 6, 1, 2, 0 },
	{ 137, "NOP", nop, 5, 0, 2, 0 },
	{ 138, "TXA", txa, 6, 1, 2, 0 },
	{ 139, "XAA", nop, 5, 0, 2, 0 },
	{ 140, "STY", sty, 1, 3, 4, 0 },
	{ 141, "STA", sta, 1, 3, 4, 0 },
	{ 142, "STX", stx, 1, 3, 4, 0 },
	{ 143, "SAX", nop, 1, 0, 4, 0 },
	{ 144, "BCC", bcc, 10, 2, 2, 1 },
	{ 145, "STA", sta, 9, 2, 6, 0 },
	{ 146, "KIL", nop, 6, 0, 2, 0 },
	{ 147, "AHX", nop, 9, 0, 6, 0 },
	{ 148, "STY", sty, 12, 2, 4, 0 },
	{ 149, "STA", sta, 12, 2, 4, 0 },
	{ 150, "STX", stx, 13, 2, 4, 0 },
	{ 151, "SAX", nop, 13, 0, 4, 0 },
	{ 152, "TYA", tya, 6, 1, 2, 0 },
	{ 153, "STA", sta, 3, 3, 5, 0 },
	{ 154, "TXS", txs, 6, 1, 2, 0 },
	{ 155, "TAS", nop, 3, 0, 5, 0 },
	{ 156, "SHY", nop, 2, 0, 5, 0 },
	{ 157, "STA", sta, 2, 3, 5, 0 },
	{ 158, "SHX", nop, 3, 0, 5, 0 },
	{ 159, "AHX", nop, 3, 0, 5, 0 },
	{ 160, "LDY", ldy, 5, 2, 2, 0 },
	{ 161, "LDA", lda, 7, 2, 6, 0 },
	{ 162, "LDX", ldx, 5, 2, 2, 0 },
	{ 163, "LAX", nop, 7, 0, 6, 0 },
	{ 164, "LDY", ldy, 11, 2, 3, 0 },
	{ 165, "LDA", lda, 11, 2, 3, 0 },
	{ 166, "LDX", ldx, 11, 2, 3, 0 },
	{ 167, "LAX", nop, 11, 0, 3, 0 },
	{ 168, "TAY", tay, 6, 1, 2, 0 },
	{ 169, "LDA", lda, 5, 2, 2, 0 },
	{ 170, "TAX", tax, 6, 1, 2, 0 },
	{ 171, "LAX", nop, 5, 0, 2, 0 },
	{ 172, "LDY", ldy, 1, 3, 4, 0 },
	{ 173, "LDA", lda, 1, 3, 4, 0 },
	{ 174, "LDX", ldx, 1, 3, 4, 0 },
	{ 175, "LAX", nop, 1, 0, 4, 0 },
	{ 176, "BCS", bcs, 10, 2, 2, 1 },
	{ 177, "LDA", lda, 9, 2, 5, 1 },
	{ 178, "KIL", nop, 6, 0, 2, 0 },
	{ 179, "LAX", nop, 9, 0, 5, 1 },
	{ 180, "LDY", ldy, 12, 2, 4, 0 },
	{ 181, "LDA", lda, 12, 2, 4, 0 },
	{ 182, "LDX", ldx, 13, 2, 4, 0 },
	{ 183, "LAX", nop, 13, 0, 4, 0 },
	{ 184, "CLV", clv, 6, 1, 2, 0 },
	{ 185, "LDA", lda, 3, 3, 4, 1 },
	{ 186, "TSX", tsx, 6, 1, 2, 0 },
	{ 187, "LAS", nop, 3, 0, 4, 1 },
	{ 188, "LDY", ldy, 2, 3, 4, 1 },
	{ 189, "LDA", lda, 2, 3, 4, 1 },
	{ 190, "LDX", ldx, 3, 3, 4, 1 },
	{ 191, "LAX", nop, 3, 0, 4, 1 },
	{ 192, "CPY", cpy, 5, 2, 2, 0 },
	{ 193, "CMP", cmp, 7, 2, 6, 0 },
	{ 194, "NOP", nop, 5, 0, 2, 0 },
	{ 195, "DCP", nop, 7, 0, 8, 0 },
	{ 196, "CPY", cpy, 11, 2, 3, 0 },
	{ 197, "CMP", cmp, 11, 2, 3, 0 },
	{ 198, "DEC", dec, 11, 2, 5, 0 },
	{ 199, "DCP", nop, 11, 0, 5, 0 },
	{ 200, "INY", iny, 6, 1, 2, 0 },
	{ 201, "CMP", cmp, 5, 2, 2, 0 },
	{ 202, "DEX", dex, 6, 1, 2, 0 },
	{ 203, "AXS", nop, 5, 0, 2, 0 },
	{ 204, "CPY", cpy, 1, 3, 4, 0 },
	{ 205, "CMP", cmp, 1, 3, 4, 0 },
	{ 206, "DEC", dec, 1, 3, 6, 0 },
	{ 207, "DCP", nop, 1, 0, 6, 0 },
	{ 208, "BNE", bne, 10, 2, 2, 1 },
	{ 209, "CMP", cmp, 9, 2, 5, 1 },
	{ 210, "KIL", nop, 6, 0, 2, 0 },
	{ 211, "DCP", nop, 9, 0, 8, 0 },
	{ 212, "NOP", nop, 12, 2, 4, 0 },
	{ 213, "CMP", cmp, 12, 2, 4, 0 },
	{ 214, "DEC", dec, 12, 2, 6, 0 },
	{ 215, "DCP", nop, 12, 0, 6, 0 },
	{ 216, "CLD", cld, 6, 1, 2, 0 },
	{ 217, "CMP", cmp, 3, 3, 4, 1 },
	{ 218, "NOP", nop, 6, 1, 2, 0 },
	{ 219, "DCP", nop, 3, 0, 7, 0 },
	{ 220, "NOP", nop, 2, 3, 4, 1 },
	{ 221, "CMP", cmp, 2, 3, 4, 1 },
	{ 222, "DEC", dec, 2, 3, 7, 0 },
	{ 223, "DCP", nop, 2, 0, 7, 0 },
	{ 224, "CPX", cpx, 5, 2, 2, 0 },
	{ 225, "SBC", sbc, 7, 2, 6, 0 },
	{ 226, "NOP", nop, 5, 0, 2, 0 },
	{ 227, "ISC", nop, 7, 0, 8, 0 },
	{ 228, "CPX", cpx, 11, 2, 3, 0 },
	{ 229, "SBC", sbc, 11, 2, 3, 0 },
	{ 230, "INC", inc, 11, 2, 5, 0 },
	{ 231, "ISC", nop, 11, 0, 5, 0 },
	{ 232, "INX", inx, 6, 1, 2, 0 },
	{ 233, "SBC", sbc, 5, 2, 2, 0 },
	{ 234, "NOP", nop, 6, 1, 2, 0 },
	{ 235, "SBC", sbc, 5, 0, 2, 0 },
	{ 236, "CPX", cpx, 1, 3, 4, 0 },
	{ 237, "SBC", sbc, 1, 3, 4, 0 },
	{ 238, "INC", inc, 1, 3, 6, 0 },
	{ 239, "ISC", nop, 1, 0, 6, 0 },
	{ 240, "BEQ", beq, 10, 2, 2, 1 },
	{ 241, "SBC", sbc, 9, 2, 5, 1 },
	{ 242, "KIL", nop, 6, 0, 2, 0 },
	{ 243, "ISC", nop, 9, 0, 8, 0 },
	{ 244, "NOP", nop, 12, 2, 4, 0 },
	{ 245, "SBC", sbc, 12, 2, 4, 0 },
	{ 246, "INC", inc, 12, 2, 6, 0 },
	{ 247, "ISC", nop, 12, 0, 6, 0 },
	{ 248, "SED", sed, 6, 1, 2, 0 },
	{ 249, "SBC", sbc, 3, 3, 4, 1 },
	{ 250, "NOP", nop, 6, 1, 2, 0 },
	{ 251, "ISC", nop, 3, 0, 7, 0 },
	{ 252, "NOP", nop, 2, 3, 4, 1 },
	{ 253, "SBC", sbc, 2, 3, 4, 1 },
	{ 254, "INC", inc, 2, 3, 7, 0 },
	{ 255, "ISC", nop, 2, 0, 7, 0 }
};

void execute(NES* nes, uint8_t opcode) {
	const Instruction& instruction = instructions[opcode];
	CPU* cpu = nes->cpu;

	uint16_t address = 0;
	bool page_crossed = false;
	uint16_t offset;

	switch (instruction.mode) {
	case modeAbsolute:
		address = read16(nes, cpu->PC + 1);
		break;
	case modeAbsoluteX:
		address = read16(nes, cpu->PC + 1) + static_cast<uint16_t>(cpu->X);
		page_crossed = pagesDiffer(address - static_cast<uint16_t>(cpu->X), address);
		break;
	case modeAbsoluteY:
		address = read16(nes, cpu->PC + 1) + static_cast<uint16_t>(cpu->Y);
		page_crossed = pagesDiffer(address - static_cast<uint16_t>(cpu->Y), address);
		break;
	case modeAccumulator:
		address = 0;
		break;
	case modeImmediate:
		address = cpu->PC + 1;
		break;
	case modeImplied:
		address = 0;
		break;
	case modeIndexedIndirect:
		address = read16_ff_bug(nes, static_cast<uint16_t>(static_cast<uint8_t>(readByte(nes, cpu->PC + 1) + cpu->X)));
		break;
	case modeIndirect:
		address = read16_ff_bug(nes, read16(nes, cpu->PC + 1));
		break;
	case modeIndirectIndexed:
		address = read16_ff_bug(nes, static_cast<uint16_t>(readByte(nes, cpu->PC + 1))) + static_cast<uint16_t>(cpu->Y);
		page_crossed = pagesDiffer(address - static_cast<uint16_t>(cpu->Y), address);
		break;
	case modeRelative:
		offset = static_cast<uint16_t>(readByte(nes, cpu->PC + 1));
		address = cpu->PC + 2 + offset - ((offset >= 128) << 8);
		break;
	case modeZeroPage:
		address = static_cast<uint16_t>(readByte(nes, cpu->PC + 1));
		break;
	case modeZeroPageX:
		address = static_cast<uint16_t>(static_cast<uint8_t>(readByte(nes, cpu->PC + 1) + cpu->X));
		break;
	case modeZeroPageY:
		address = static_cast<uint16_t>(static_cast<uint8_t>(readByte(nes, cpu->PC + 1) + cpu->Y));
		break;
	}

	cpu->PC += static_cast<uint16_t>(instruction.size);
	cpu->cycles += static_cast<uint64_t>(instruction.cycles);
	if (page_crossed) {
		cpu->cycles += static_cast<uint64_t>(instruction.page_cross_cycles);
	}

	instruction.dispatch(cpu, nes, address, instruction.mode);
}