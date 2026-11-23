/* Derived from xrip/pico-286, 572890a. See LICENSE.pico-286. */
#pragma once

#define reges 0
#define regcs 1
#define regss 2
#define regds 3
#define regfs 4
#define reggs 5

// eax
#define regal 0
#define regah 1
// 2, 3

// ecx
#define regcl 4
#define regch 5
// 6, 7

// edx
#define regdl 8
#define regdh 9
// 10, 11

// ebx
#define regbl 12
#define regbh 13
// 14, 14



// Memory at a linear segment base plus an offset
#define getmem8(base, offset)  mem_read8((base) + (offset))
#define getmem16(base, offset) mem_read16((base) + (offset))
#define putmem8(base, offset, value)  mem_write8((base) + (offset), value)
#define putmem16(base, offset, value) mem_write16((base) + (offset), value)
#define signext(value)  (int16_t)(int8_t)(value)
#define getreg16(regid) wordregs[(regid) << 1]
#define getreg8(regid)  byteregs[byteregtable[regid]]
#define putreg16(regid, writeval) wordregs[(regid) << 1] = writeval
#define putreg8(regid, writeval)  byteregs[byteregtable[regid]] = writeval
#define getsegreg(regid)            segregs[regid]

// The arithmetic flags may still be in the last ALU operation: flags_now() (emulator.h) puts them in
#define cf  flags_now()->bits.CF
#define pf  flags_now()->bits.PF
#define af  flags_now()->bits.AF
#define zf  flags_now()->bits.ZF
#define sf  flags_now()->bits.SF
#define of  flags_now()->bits.OF
#define tf  x86_flags.bits.TF
#define ifl x86_flags.bits.IF
#define df  x86_flags.bits.DF
#define iopl x86_flags.bits.IOPL
#define nt  x86_flags.bits.NT
#define v86 x86_flags.bits.VM

#define XRIP_FL_CF    cf
#define XRIP_FL_PF    pf
#define XRIP_FL_AF    af
#define XRIP_FL_ZF    zf
#define XRIP_FL_SF    sf
#define XRIP_FL_TF    tf
#define XRIP_FL_IFL   ifl
#define XRIP_FL_DF    df
#define XRIP_FL_OF    of

#define FLAG_CF_OF_MASK ((1u << 11) | 1)
#define FLAG_CF_AF_MASK ((1u << 4) | 1)

#define XRIP_CS    segregs[regcs]
#define XRIP_DS    segregs[regds]
#define XRIP_ES    segregs[reges]
#define XRIP_SS    segregs[regss]
#define XRIP_FS    segregs[regfs]
#define XRIP_GS    segregs[reggs]

// The base address the CPU keeps for each segment register (real mode: selector * 16)
#define CS_BASE   segment_base[regcs]
#define DS_BASE   segment_base[regds]
#define ES_BASE   segment_base[reges]
#define SS_BASE   segment_base[regss]
#define FS_BASE   segment_base[regfs]
#define GS_BASE   segment_base[reggs]

#define XRIP_AX    wordregs[regax << 1]
#define XRIP_BX    wordregs[regbx << 1]
#define XRIP_CX    wordregs[regcx << 1]
#define XRIP_DX    wordregs[regdx << 1]
#define XRIP_SI    wordregs[regsi << 1]
#define XRIP_DI    wordregs[regdi << 1]
#define XRIP_BP    wordregs[regbp << 1]
#define XRIP_SP    wordregs[regsp << 1]
// The CPU state used at every instruction (cpu.c). One struct, so the RP2350 code reaches all of it
// from one base register instead of loading each address. Macros keep the short names; cpu.c and
// fpu.c add the ones for the decoder fields.
typedef struct {
    uint32_t ip32;
    uint8_t *fast_memory; // conventional RAM, read and written directly below fast_memory_size
    uint32_t fast_memory_size; // 0 (while paging is on, and in sst_test): always read86/write86
    uint8_t *extended_memory; // the memory from 1 MB (HMA_START), the same way while A20 is on
    uint32_t extended_memory_size; // 0 (while paging is on, and in sst_test): always read86/write86
    uint32_t useseg; // base of the segment used for memory operands
    uint32_t ea; // linear address of the memory operand
    uint32_t firstip; // EIP of the current instruction, with its prefixes
    uint32_t first_esp; // ESP before the current instruction: a fault puts it back
    uint32_t flags_result, flags_dst, flags_src; // the last ALU operation, for its flags (cpu.c)
    uint16_t disp16;
    uint8_t mode, reg, rm; // the ModR/M byte
    uint8_t reptype, segoverride;
    uint8_t opsize32, addrsize32; // operand and address size of the current instruction: 1 = 32-bit
    uint8_t code32, stack32; // D bit of CS (default size) and B bit of SS (the stack uses ESP)
    uint8_t flags_op; // kind and size of that operation; 0: x86_flags has all the flags
    uint8_t flags_cf; // its CF
    uint8_t a20_enabled;
    unsigned long retired; // one increment per executed instruction
} cpu_state_t;
extern cpu_state_t cpu_state;
#define ip32 cpu_state.ip32
#define fast_memory cpu_state.fast_memory
#define fast_memory_size cpu_state.fast_memory_size
#define extended_memory cpu_state.extended_memory
#define extended_memory_size cpu_state.extended_memory_size
#define a20_enabled cpu_state.a20_enabled
#define XRIP_IP    ip32
#define xrip_ip        ip32

#define XRIP_AL    byteregs[regal]
#define XRIP_BL    byteregs[regbl]
#define XRIP_CL    byteregs[regcl]
#define XRIP_DL    byteregs[regdl]
#define XRIP_AH    byteregs[regah]
#define XRIP_BH    byteregs[regbh]
#define XRIP_CH    byteregs[regch]
#define XRIP_DH    byteregs[regdh]

static void modregrm(void);
