/*
**  Z80.h
**
**  PIC32 Z80 emulator 
**  
**  lucio@dijasio.com
**
**  
*/

#ifndef Z80_H
#define Z80_H

#define DBGZ80
//#define DBGSTK
//#define Z80_COMPLETE_EMULATION

#define byte unsigned char

void Trap( int);

//-------------------------------------------------------------
// registers
#define B       0
#define C       1
#define D       2
#define E       3
#define H       4
#define L       5
#define PHL     6   // (HL)
#define A       7
#define IXH     8   // H+4
#define IXL     9
#define IYH     10  // H+6
#define IYL     11

// register pairs
#define BC      0
#define DE      1
#define HL      2
#define SP      3       // alternatively AF
#define IX      4       // HL+ixy
#define IY      5       // HL+ixy

//-------------------------------------------------------------
// FLAGS
#define FNZ	    0
#define FZ	    1
#define FNC	    2
#define FC	    3
#define FPO	    4
#define FPE	    5
#define FP	    6
#define FM      7

typedef struct {
    unsigned c;       // carry
    unsigned n;       // add or sub
    unsigned p;       // parity/overflow
    unsigned u3;      // ND
    unsigned h;       // half carry
    unsigned u5;      // ND
    unsigned z;       // zero
    unsigned s;       // sign
}    flags_t;
   


//-------------------------------------------------------------
// Z80 processor resources
//
#define ROMSIZE 16*1024
#define RAMSIZE 16*1024

// labels
#define EDITOR      0x0F2C  // editor main entry
#define CLS         0x0D6B  // clearing the screen
#define RAM_DONE    0x11EF  // RAM check
#define MAIN_1      0x12A9  // main execution loop
#define PO_MSG      0x0C0A  // print out a message
#define STKEND      0x5c63  // CALC Stack End
#define STKBOT      0x5c65  // CALC Stack Bottom

// Memory Map definitions
#define MAXROM      ROMSIZE
#define RAMSTART    MAXROM
#define MAXRAM      RAMSIZE+ROMSIZE

// Exceptions
#define EX_HALT 00  // HALT instruction
#define EX_ROM   1  // writing to ROM
#define EX_ROLL  2  // PC Roll Over 
#define EX_MEM   3  // un-implemented memory

extern const byte rom[];
extern byte ram[];

// public Z80 features exposed
extern volatile byte iff;       // interrupt flag
extern byte ei;                 // interrupt enable
extern int pc, sptr;            // program counter and stack pointer
extern byte reg[];              // main register set
extern flags_t flags;           // flags

// instructions decoding
typedef union {
    struct{
        unsigned z:3;  // lsb
        unsigned y:3;
        unsigned x:2;
    };
    byte opcode;    
} dcode;
extern dcode ir;

// debugging options
#ifdef DBGZ80
    // debugging support
    int bkpt, run;
    //byte oreg[12];     // value of registers before last instruction
    //flags_t oflags;      // alternate flag set
    char s[128];         // string workspace
    
    //#define DIS(x) strcat(s, x) uncomment if disassembly required
    #define DIS(x)
#else
    #define DIS(x) 
#endif

// stack debugging
#ifdef DBGSTK
    int Ci, Cp, Cd, Ccode; 
    byte Ca[5], Cb[5];
    union {
        float f;
        struct{
            unsigned m:23;  // mantissa
            unsigned e:8;   // exponent +127
            unsigned s:1;   // sign
        };
    }  ieee;         
#endif


#endif

  