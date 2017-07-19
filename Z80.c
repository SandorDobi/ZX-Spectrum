/*
**  PIC32 Z80 emulator 
**  
**  lucio@dijasio.com
**
**  02/09/08 v1.0 Z80 emulation 
**  03/14/10 v1.1 flags optimization, save/load traps
*/

#include <plib.h>
#include <stdio.h>
#include <string.h>
#include "MDD File System\FSIO.h"
#include "taskZ80.h"    
#include "z80.h"  

//-------------------------------------------------------------
// decoding support macros
//
#define GETZ()  flags.z=((r&0xFF)==0)
#define GETS()  flags.s=((r&0x80)!=0)
#define GETC()  flags.c=((r>255)||(r<0))
#define GETCC() flags.c=((r>65535)||(r<0))
#define GETSS() flags.s=((r&0x8000)!=0)
#define GETZZ() flags.z=((r&0xFFFF)==0)

//-------------------------------------------------------------
// additional flag support
//
#ifdef Z80_COMPLETE_EMULATION
    #define GETVV() flags.p=((r>32767) || (r<-32768))
    #define GETH()  flags.h=((r&0xf)>9) this is incorrect
    #define GETP()  flags.p=(pt[r>>4]+ pt[r&0xf])
    #define GETV()  flags.p=((r>127) || (r<-128))
    #define GETHH() flags.h=
#else // reduced emulation of flags 
    #define GETVV() 
    #define GETH()  
    #define GETP()  
    #define GETV()  
    #define GETHH() 
#endif

//-------------------------------------------------------------
// ALU operations
#define ADDA	0
#define ADCA	1
#define SUB	    2
#define SBCA	3
#define AND	    4
#define XOR	    5
#define OR	    6
#define CP      7

// rotations
#define RLC	    0
#define RRC	    1
#define RL	    2
#define RR	    3
#define SLA	    4
#define SRA	    5
#define SLL	    6
#define SRL     7


//-------------------------------------------------------------
// Z80 resources
flags_t flags, flags2;
byte reg[12], reg2[8];  // CPU regs and alternate pair
byte ei;                // interrupt enable (MODE1)
volatile byte iff;      // interrupt flag 
dcode ir;               // the instruction decoding register
byte rr;                // R the refresh register
byte ii;                // I the interrupt high address reg
byte im;                // the interrupt mode (not simulated)
int pc;                 // the program counter
int sptr;               // the stack pointer
byte ixy;               // flag DD/FD prefix registers offset
byte icb;               // DDCB/FDCB ofs prefetch flag
signed char ofs;        // IX/IY offset
int dummy;              // used to provide a valid destination

//-------------------------------------------------------------
// Spectrum resources
//
// RAM memory
byte ram[ RAMSIZE];

// OUT 0xFE border color
volatile byte border;
// IN keyboard simulation
volatile byte INK[8]={ 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};


//-------------------------------------------------------------
// parity tables 
const byte pt[]={ 
         // 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
            0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

void Exception( int code)
{
    //capture errors and special events
    reg[A]=reg[A];    
 //   while(1);
} //Exception


byte rmem( unsigned int add)
{
    if ( add<MAXROM)
        return rom[ add];
    else if ( add<MAXRAM)
        return ram[ add-RAMSTART];
    else 
        return 0xFF;    // unimplemented memory
//        Exception( EX_MEM);    
} // rmem

byte *dmem( unsigned int add)
{
    if (( add<RAMSTART) || (add>=MAXRAM))
//        Exception(EX_ROM); debugging hook 
        return (void*) &dummy;  
    else 
        return &ram[ add-RAMSTART];
} // dmem

void wmem( int add, byte b)
{
    byte *p = dmem( add);
    if ( p!=NULL)
        *p = b;
} // wmem


byte fetch( void)
{
    byte i;
    i = rmem( pc++);    // fetch next byte and increment PC
    rr++;               // increment refresh register
    pc &=0xffff;        // roll over address (danger?)
    return  i;
} // fetch


byte get8op( int i)
{
    byte r;
    
    if (i==PHL)
    {
        if (ixy==0)
            r=rmem( (reg[H]<<8)+reg[L]);
        else 
        {   // DD or FD codes
            if (icb==0)
                ofs=fetch();  // DDCB and FDCB are pre-fetched
            icb=0;
            if (ixy==4)
                r=rmem( (reg[IXH]<<8)+reg[IXL]+ofs);
            else
                r=rmem( (reg[IYH]<<8)+reg[IYL]+ofs);

            // consume the ixy
            ixy=0;    
        }    
    }
    else if ((i&0xE)==H)
    {
        r=reg[i+ixy];
        ixy=0;
    }    
    else 
        r=reg[i];
    return r;
} // get8op


byte *dreg( int i)
// get the destination address of index i
{
    byte *r;
    
    if (i==PHL)
    {
        if (ixy==0)
            r=dmem( (reg[H]<<8)+reg[L]);
        else 
        {   // DD or FD codes
            if (icb==0)
                ofs=fetch();  // DDCB and FDCB are pre-fetched
            icb=0;
            if (ixy==4)
                r=dmem( (reg[IXH]<<8)+reg[IXL]+ofs);
            else            
                r=dmem( (reg[IYH]<<8)+reg[IYL]+ofs);
            // consume ixy
            ixy=0;
        }
    }
    else if ((i&0xE)==H)
    {
        r=&reg[i+ixy];
        ixy=0;
    }    
    else 
        r=&reg[i];
    return r;
} // dreg

    
void set8op( int i, byte v)
{
    byte *p=dreg( i);
    if (p==NULL)
        Exception(EX_MEM);
     else
        *p= v;
} //set8op

int get16op( int i)
{   
    switch( i&3)
    {
      case BC:       // BC
        return (reg[B]<<8)+reg[C];
      case DE:       // DE
        return (reg[D]<<8)+reg[E];
      case HL:       // HL
        return (reg[H+ixy]<<8)+reg[L+ixy];
      case SP:       // SP
        return sptr;
    }
} // get16op


void set16op( int i, int v)
{
    switch( i&3)
    {
      case BC:      // BC
        reg[B] = v>>8;
        reg[C] = v&0xff;
        break;  
      case DE:      // DE
        reg[D] = v>>8;
        reg[E] = v&0xff;
        break;  
      case HL:      // HL
        reg[H+ixy] = v>>8;
        reg[L+ixy] = v&0xff;
        break;  
      case SP:      // SP    
        sptr = v;
     }
} // set16op
      
BOOL gflag( int i)
{
    switch( i&7)      
    {
      case FNZ:
        return !( flags.z);
      case FZ:
        return ( flags.z);
      case FNC:
        return !( flags.c);
      case FC:
        return ( flags.c);
      case FPO:
        return !( flags.p);
      case FPE:
        return ( flags.p);
      case FP:
        return !( flags.s);
      case FM:
        return ( flags.s);
    }
} //gflag


void Alu( int op, int v)
{
    int r=reg[A];
    switch( op)
    {
      case ADDA:
        DIS("ADDA");
        reg[A]= (r+=v);
        flags.n=0;            
        GETV();
        GETC();
        GETH();
        break;
      case ADCA:
        DIS("ADCA");
        r+=(v+flags.c);
        flags.n=0;
        GETV();
        GETC();            
        flags.h=((reg[A]&0xf)+((v+flags.c)&0xf)>0xf);
        reg[A]=r;
        break;            
      case SUB:
        DIS("SUB");
        reg[A]= (r-=v);
        flags.n=1;
        GETV();
        GETC();            
        GETH();
        break;
      case SBCA:
        DIS("SBCA");
        reg[A]= r-=(v+flags.c);
        flags.n=1;            
        GETV();
        GETC();
        GETH();
        break;
      case AND:
        DIS("AND");
        reg[A]= r&=v;
        flags.n=0;            
        flags.h=1;
        flags.c=0;
        GETP();
        break;
      case XOR:
        DIS("XOR");
        reg[A]= r^=v;
        flags.c=0;
        flags.n=0;
        flags.h=0;
        GETP();            
        break;
      case OR:
        DIS("OR");
        reg[A]= r|=v;
        flags.c=0;
        flags.n=0;            
        flags.h=0;
        GETP();
        break;
      case CP:
        DIS("CP");
        r-=v;
        flags.n=1;
        GETV();            
        GETC();
        GETH();
        break;
    }
    // common flags S and Z
    GETS();
    GETZ();
} // Alu

int pop( void)
{
    int nn;
    nn=rmem( sptr++);  
    nn+=rmem( sptr++)<<8;
    return nn;
} //pop    

void push( int nn)
{
    wmem( --sptr, nn>>8);
    wmem( --sptr, nn&0xFF);
} //push    

int fetch16( void)
{
    int nn=fetch();
    return nn+(fetch()<<8);
} // fetch16    


int getIN( byte ha, byte la)
{
    int r=0xFF;
    
    if (la==0xFE)       // emulate keyboard input 
    {
        switch( ha)     // check high address
        {   
          // bits           0 1,2,3,4
          case 0xFE:    // sh,Z,X,C,V
            r=INK[0];
            break;
          case 0xFD:    //  A S D F G
            r=INK[1];
            break;
          case 0xFB:    //  Q W E R T
            r=INK[2];
            break;
          case 0xF7:    //  1 2 3 4 5
            r=INK[3];
            break;
          case 0xEF:    //  0 9 8 7 6
            r=INK[4];
            break;
          case 0xDF:    //  P O I U Y
            r=INK[5];
            break;
          case 0xBF:    // En L K J H
            r=INK[6];
            break;
          case 0x7f:    // _ Ctr M N B
            r=INK[7];
            break;
          default:      
            r=0xFF;
        }
    }    
    return r;
} // getIN


void decode( void)
{
    int t, r, n, nn;
    flags_t tf;
    byte *p;
    
    // get the instruction currently pointed by pc
    ir.opcode =  fetch();
    
    // unprefixed codes decoding
    switch( ir.x) 
    {
      //------------------------------------------------------  
      case 0:
        switch( ir.z)
        {
          case 0: // z=0 relative jumps and assorted ops
            switch( ir.y)
            {
              case 0:   // NOP
                DIS("NOP");
                break;
              case 1:   // EX AF AF'
                DIS("EX AF,AF'");
                t = reg[A]; reg[A]=reg2[A]; reg2[A]=t;
                tf=flags; flags=flags2; flags2=tf;            
                break;
              case 2:   // DJNZ
                DIS("DJNZ");
                r = get8op(B)-1;    // dec B
                set8op(B, r);
                ofs = fetch();
                if (r!=0)           // jump if NZ
                    pc += ofs;
                GETS(); 
                GETZ();
                GETH();
                flags.n=1;
                break;
              case 3:   // JR+d
                DIS("JR");
                ofs = fetch();
                pc += ofs;
                break;
              case 4:   // JR NZ
                DIS("JR NZ");
                ofs = fetch();
                if (gflag(FNZ))           // jump if NZ
                    pc += ofs;
                break;
              case 5:   // JR Z
                DIS("JR Z");
                ofs = fetch();
                if (gflag(FZ))           // jump if Z
                    pc += ofs;
                break;
              case 6:   // JR NC
                DIS("JR NC");
                ofs = fetch();
                if (gflag(FNC))           // jump if NC
                    pc += ofs;
                break;
              case 7:   // JR C
                DIS("JR C");
                ofs = fetch();
                if (gflag(FC))           // jump if C
                    pc += ofs;
                break;
            } // switch y
            break;
            
          case 1:   // z=1 16-bit immediate ld and add
            if (ir.y&1)    // ADD HL, pair
            {
                DIS("ADD HL,rr");
                set16op( HL, r=get16op(HL)+get16op(ir.y>>1));
                GETCC();
                flags.n=0;
            }    
            else        // LD pair, nn
            {
                nn=fetch16();
                DIS("LD rr,nn");
                set16op( ir.y>>1, nn);
            }    
            break;
            
          case 2:   // z=2 indirect loading
            switch( ir.y)
            {
              case 0:   // LD (BC),A
                DIS("LD (BC),A");
                wmem( get16op(BC), reg[A]);
                break;
              case 1:   // LD A,(BC)
                DIS("LD A,(BC)");
                reg[A]=rmem( get16op(BC));
                break;
              case 2:   // LD (DE),A
                DIS("LD (DE),A");
                wmem( get16op(DE), reg[A]);
                break;
              case 3:   // LD A,(DE)
                DIS("LD A,(DE)");
                reg[A]=rmem( get16op(DE));
                break;
              case 4:   // LD (nn),HL
                DIS("LD (nn),HL");
                nn=fetch16();
                t=ixy;                  // need ixy twice
                wmem( nn++, get8op(L));
                ixy=t;                  // restore it
                wmem( nn, get8op(H));
                break;
              case 5:   // LD HL,(nn)
                DIS("LD HL,(nn)");
                nn=fetch16();
                t=ixy;                  // need ixy twice
                set8op(L, rmem( nn++));
                ixy=t;                  // restore it
                set8op(H, rmem( nn));
                break;
              case 6:   // LD (nn),A
                DIS("LD (nn),A");
                nn=fetch16();
                wmem( nn, reg[A]);
                break;
              case 7:   // LD A,(nn)
                DIS("LD A,(nn)");
                nn=fetch16();
                reg[A]=rmem( nn);
                break;
            }
            break;    

          case 3:   // z=3 16-bit INC and DEC
            if (ir.y&1) 
            {
                DIS("DEC rr");
                t=ixy;
                r=get16op(ir.y>>1)-1;
                ixy=t;
                set16op( ir.y>>1, r);
            }    
            else
            {
                DIS("INC rr");
                t=ixy;
                r=get16op(ir.y>>1)+1;
                ixy=t;
                set16op( ir.y>>1, r);
            }    
            break;
            
          case 4:   // z=4 8-bit INC
            DIS("INC r");
            p=dreg(ir.y);
            r=(*p)+=1;
            flags.n=0;
            GETS();
            GETZ();
            GETH();
            GETV();
            break;
            
          case 5:   // z=5 8-bit DEC
            DIS("DEC r");
            p=dreg(ir.y);
            r= (*p)-=1;
            flags.n=1;
            GETS();
            GETZ();
            GETH();
            GETV();
            break;
            
          case 6:   // z=6 LD r,N 
            {
              DIS("LD r,N");
              p=dreg( ir.y);        // fetch the ixy ofs
              if (p==NULL)
                Exception(EX_MEM);
              else
                *p=fetch();         // before the immediate
            }
            break;
            
          case 7:   // z=7 assorted op on acc and flags
            switch( ir.y)
            {
              case 0:   // RLCA 8-bit
                DIS("RLCA");
                t=reg[A];
                flags.c=(t>>7);
                reg[A]= r=(t<<1)+(t>>7);
                flags.n=0;
                flags.h=0;
                break;                
              case 1:   // RRCA 8-bit
                DIS("RRCA");
                t=reg[A];
                flags.c=(t&1);
                reg[A]= r=(t>>1)+(t<<7);
                flags.n=0;
                flags.h=0;
                break;
              case 2:   // RLA 9-bit
                DIS("RLA");
                t=reg[A];
                reg[A]=(reg[A]<<1)+flags.c;
                flags.c=(t>>7);
                flags.n=0;
                flags.h=0;
                break;
              case 3:   // RRA 9-bit
                DIS("RRA");
                t=reg[A];
                reg[A]=(t>>1)+(flags.c<<7);
                flags.c=(t&1);
                flags.n=0;
                flags.h=0;
                break;
              case 4:   // DAA
                DIS("DAA");
                r=reg[A];
                if (flags.n) 
                {
	                if (flags.h||(reg[A]&0xf)>9) 
	                    r-=6;
                	if (flags.c||r>0x99) 
                	    r-=0x60;
                }
                else 
                {
                	if (flags.h||(reg[A]&0xf)>9) 
                	    r+=6;
                	if (flags.c||reg[A]>0x99) 
                	    r+=0x60;
                }
                flags.c=flags.c||(r>0x99)||(r>255);
                reg[A]=r;
                GETZ(); 
                GETS();
                break;
              case 5:   // CPL
                DIS("CPL");
                reg[A]^=0xff;
                flags.n=1;
                flags.h=1;
                break;
              case 6:   // SCF
                DIS("SCF");
                flags.c=1;
                flags.h=0;
                flags.n=0;
                break;
              case 7:   // CCF
                DIS("CCF");
                flags.c=1-flags.c;
                flags.n=0;
                break;
            }
            break;    
        }// switch z
        break;
        
      //------------------------------------------------------
      case 1:   // 8-bit loading    LD r1,r2
        if (ir.z==PHL && ir.y==PHL)     // 
        {
            DIS("HALT");
            while( !iff);   // wait for an interrupt
        }    
        else    
        { // give priority to (IX+N) over IXH or IXL
        DIS("LD r, r");
            if (ir.y==PHL) 
            {
                p =dreg( ir.y);
                t=get8op( ir.z);
            }
            else 
            {
                t=get8op( ir.z);
                p=dreg( ir.y);
            }    
        *p=t;
        }    
        break;
            
      //-------------------------------------------------------
      case 2:   // arithmetic and logic with reg
        Alu( ir.y, get8op(ir.z));
        break;
        
      //-------------------------------------------------------
      case 3:   // miscellaneous group
        switch( ir.z)
        {
          case 0:   // RET cc
            DIS("RET cc");
            if ( gflag( ir.y))
                pc=pop();
            break;
          case 1:   // pop & various ops
            switch( ir.y)
            {
              case 0:   // pop BC
                DIS("POP BC");
                set16op( BC, pop());
                break;
              case 1:   // RET
                DIS("RET");
                pc=pop();
                break;
              case 2:   // pop DE
                DIS("POP DE");
                set16op( DE, pop());
                break;
              case 3:   // EXX
                DIS("EXX");
                for( n=0; n<6; n++)
                    { t=reg[n]; reg[n]=reg2[n]; reg2[n]=t;}
                break;
              case 4:   // pop HL
                DIS("POP HL");
                set16op( HL, pop());
                break;                
              case 5:   // JP HL
                DIS("JP HL");
                nn=get16op( HL);
                pc=nn;
                break;
              case 6:   // pop AF
                DIS("POP AF");
                nn=pop();
                reg[A]= nn>>8;
                // disassemble all flags from a single byte value
                flags.c = nn&1;  nn>>=1;
                flags.n = nn&1;  nn>>=1;
                flags.p = nn&1;  nn>>=1;
                flags.u3= nn&1;  nn>>=1;
                flags.h = nn&1;  nn>>=1;
                flags.u5= nn&1;  nn>>=1;
                flags.z = nn&1;  nn>>=1;
                flags.s = nn&1;  nn>>=1;
                break;
              case 7:   // LD SP, HL
                DIS("LD SP,HL");
                set16op( SP, get16op( HL));
                break;
            }   
            break; 
          case 2:   // JP cc,nn
            DIS("JP cc,nn");
            nn=fetch16();
            if ( gflag( ir.y))
                pc=nn;
            break;
          case 3:   // assorted op
            switch( ir.y)
            {
              case 0:   // JP nn
                DIS("JP nn");
                pc=fetch16();
                break;

              case 1:   // CB prefix
                if (ixy>0)          // DDCB
                {
                    icb=1;          // pre-fetch ofs
                    ofs=fetch();    // ofs before the opcode
                }
                ir.opcode=fetch();  // get opcode
                switch(ir.x)
                {
                  case 0:   // rot r[z]
                    p=dreg( ir.z);      // get operand
                    t=*p;               // get initial value
                    switch( ir.y)
                    {
                      case 0:   // RLC 8-bit
                        DIS("RLC");
                        *p= r=(t<<1)+(t>>7);
                        flags.c=(t>>7);
                        break;
                      case 1:   // RRC 8-bit
                        DIS("RRC");
                        *p= r=(t>>1)+(t<<7);
                        flags.c=(t&1);
                        break;
                      case 2:   // RL 9-bit
                        DIS("RL");
                        *p= r=(t<<1)+flags.c;
                        flags.c=(t>>7);
                        break;
                      case 3:   // RR 9-bit
                        DIS("RR");
                        *p= r=(t>>1)+(flags.c<<7);
                        flags.c=(t&1);
                        break;
                      case 4:   // SLA
                        DIS("SLA");
                        *p= r=(t<<1);
                        flags.c=(t>>7);
                        break;
                      case 5:   // SRA
                        DIS("SRA");
                        *p= r=(t>>1)+(t&0x80);
                        flags.c=(t&1);
                        break;
                      case 6:   // SLL
                        DIS("SLL");
                        *p= r=(t<<1)+1;
                        flags.c=(t>>7);
                        break;
                      case 7:   // SRL
                        DIS("SRL");
                        *p= r=(t>>1);
                        flags.c=(t&1);
                        break;
                    }    
                    flags.n=0;
                    flags.h=0;
                    GETP();
                    GETZ();
                    GETS();
                    break;
                  case 1:   // BIT y,r[z]
                    DIS("BIT");
                    t=get8op( ir.z);      // get operand
                    flags.z=((t & (1<<ir.y))==0);
                    flags.h=1;
                    flags.n=0;
                    break;
                  case 2:   // RES y,r[z]
                    DIS("RES");
                    p=dreg( ir.z);      // get operand
                    *p &= ~(1<<ir.y);
                    break;
                  case 3:   // SET y,r[z]
                    DIS("SET");
                    p=dreg( ir.z);      // get operand
                    *p|=(1<<ir.y);
                    break;
                }    
                break; // CB
                
              case 2:   // OUT (n),A
                DIS("OUT (n),A");
                n=fetch();
                if (n==0xFE)    // screen border
                {
                    _RD0 = (reg[A]>>4); // output EAR
                }    
                border = reg[A];
                break;
              case 3:   // IN A,(n)
                DIS("IN A,(n)");
                n=fetch();
                reg[A]= r=getIN( reg[A], n);
                GETZ();
                GETS();
                GETP();
                flags.n=0;
                flags.h=0;
                break;
              case 4:   // EX (SP),HL
                DIS("EX (SP),HL");
                p=dreg(L);
                t=rmem( sptr); wmem( sptr, *p); *p=t;
                sptr++; p--;
                t=rmem( sptr); wmem( sptr, *p); *p=t;
                sptr--;
                break;
              case 5:   // EX DE,HL
                DIS("EX DE,HL");
                t=get16op( DE); 
                set16op(DE, get16op(HL));
                set16op(HL, t);
                break;
              case 6:   // DI
                DIS("DI");
                ei=0;
                break;
              case 7:   // EI
                DIS("EI");
                ei=1;
                break;
            }    
            break;
          case 4:   // call cc,nn
            DIS("CALL cc,nn");
            nn=fetch16();
            if ( gflag( ir.y))
            {
                push( pc);
                pc=nn;
            }   
            break;
          case 5:   // push call and other
            switch( ir.y)
            {
              case 0:   // PUSH BC
                DIS("PUSH BC");
                push( get16op( BC));
                break;
              case 1:   // CALL nn
                DIS("CALL nn");
                nn=fetch16();
                push( pc);
                pc=nn;            
                break;
              case 2:   // PUSH DE
                DIS("PUSH DE");
                push( get16op( DE));
                break;
              case 3:   // DD IX+n prefix
                DIS("DD->");
                ixy=4;   
                decode();
                break;
              case 4:   // PUSH HL
                DIS("PUSH HL");
                push( get16op( HL));
                break;  
              case 5:   // ED
                ir.opcode=fetch();
                switch( ir.x)
                {
                  case 0:   // NONI 0
                    DIS("NONI");
                    // here used as a trap to capture special memory locations
                    Trap( ir.z);
                    break;
                  case 1:
                    switch( ir.z)
                    {
                      case 0:   // IN r,(C)
                        DIS("IN r,(c)");
                        set8op( ir.y, r=getIN( reg[B], reg[C]));
                        GETS();
                        GETZ();
                        GETP();
                        flags.n=0;
                        flags.h=0;
                        break;
                      case 1:   // OUT (C),r
                        DIS("OUT (c),r");
//                        Exception( EX_OUT);
                        break;
                      case 2:   
                        if (ir.y&1) // ADC HL,rr
                        {
                            DIS("ADC HL,rr");
                            t = get16op( ir.y>>1)+flags.c;
                            set16op( HL, r=get16op(HL)+ t);
                            flags.n=0;
                            GETCC();
                            GETVV();
                            GETSS();
                            GETZZ();
                            GETHH();
                        }    
                        else        // SBC HL,rr
                        {
                            DIS("SBC HL,rr");
                            t = get16op( ir.y>>1)+flags.c;
                            set16op( HL, r=get16op(HL)- t);
                            flags.n=1;
                            GETCC();
                            GETSS();
                            GETVV();
                            GETHH();
                            GETZZ();
                        }    
                        break;
                      case 3:   
                        nn=fetch16();
                        if (ir.y&1) // LD rr,(nn)
                        {
                            DIS("LD rr,(nn)");
                            t=rmem(nn)+(rmem(nn+1)<<8);
                            set16op( ir.y>>1, t);
                        }    
                        else        // LD (nn),rr
                        {
                            DIS("LD (nn),rr");
                            t=get16op( ir.y>>1);
                            wmem( nn, t&0xFF);
                            wmem( nn+1, t>>8);
                        }    
                        break;
                      case 4:   // NEG
                        DIS("NEG");
                        reg[A]= r=-reg[A];
                        GETS();
                        GETH();
                        GETZ();
                        GETV();
                        GETC();
                        flags.n=1;
                        break;
                      case 5:  
                        DIS("RETI/N");
                        t = pop();
                        pc = t;
//                        if (ir.y==1)    // RETI
//                        else            // RETN
                        break;
                      case 6:   // IM y
                        DIS("IM y");
                        im=ir.y;                      
                        break;
                      case 7:
                        switch( ir.y)
                        {
                          case 0:   // LD I,A
                            DIS("LD I,A");
                            ii=reg[A];
                            break;                          
                          case 1:   // LD R,A
                            DIS("LD R,A");
                            rr=reg[A];
                            break;                          
                          case 2:   // LD A,I
                            DIS("LD A,I");
                            reg[A]=ii;
                            break;                          
                          case 3:   // LD A,R
                            DIS("LD A,R");
                            reg[A]=rr;
                            break;                          
                          case 4:   // RRD
                            DIS("***RRD***");
                            p=dmem( get16op(HL));
                            n=*p; t=reg[A];
                            reg[A]= r=(t&0xf0)+(n&0xf);
                            *p= ((n>>4)&0xf)+((t&0xf)<<4);
                            flags.n=0;
                            flags.h=0;
                            GETS();
                            GETZ();
                            GETP();
                            break;                          
                          case 5:   // RLD
                            DIS("***RLD***");
                            p=dmem( get16op(HL));
                            n=*p; t=reg[A];
                            reg[A]= r=(t&0xf0)+(n>>4);
                            *p= ((n<<4)&0xf0)+(t&0xf);
                            flags.n=0;
                            flags.h=0;
                            GETS();
                            GETZ();
                            GETP();
                            break;                          
                          case 6:   // NOP
                            DIS("NOP");
                            break;                          
                          case 7:   // NOP
                            DIS("NOP");
                            break;                          
                        }
                        break;
                    }    
                    break;
                  case 2:
                    DIS("LDIR/CP/IN/OUT...");
                    if ((ir.z<=3)&&(ir.y>=4))
                    {
                        int ss=get16op(HL);
                        int dd=get16op(DE);
                        int cc=get16op(BC);
                        r=1;
                        do{
                            switch( ir.z)
                            {
                              case 0:   // LDxx
                                wmem( dd, rmem(ss));
                                break;
                              case 1:   // CPxx
                                r=(rmem(ss)-reg[A]);
                                break;
                              case 2:   // INxx
                                break;  
                              case 3:   // OUTxx
                                break;
                            }
                            //xx is based on (ir.y-4) bits
                            t=ir.y-4;   // 0..3
                            if (t&1)    // DEC
                            {
                                ss--; dd--;
                            }    
                            else        // INC
                            {
                                ss++;   dd++;
                            }    
                            cc--;
                        } while ( r && (t&2) && (cc>0));
                        // update pointers and counters
                        set16op( DE, dd);
                        set16op( HL, ss);
                        set16op( BC, cc);
                        
                    } // if 
                    break;
                  case 3:   // NONI
                    DIS("NONI-3");
                    break;
                }    
                break;
              case 6:   // PUSH AF
                DIS("PUSH AF");
                // assemble all flags in a single byte value
                n=0;
                n += (flags.s) ? 1 : 0;  n<<=1;
                n += (flags.z) ? 1 : 0;  n<<=1;
                n += (flags.u5)? 1 : 0;  n<<=1;
                n += (flags.h) ? 1 : 0;  n<<=1;
                n += (flags.u3)? 1 : 0;  n<<=1;
                n += (flags.p) ? 1 : 0;  n<<=1;
                n += (flags.n) ? 1 : 0;  n<<=1;
                n += (flags.c) ? 1 : 0;  
                push( ( reg[A]<<8)+ n);
                break;
              case 7:   // FD IY+n prefix
                DIS("FD->");
                ixy=6;  
                decode();
                break;
            }    
            break;
          case 6:   // alu + immediate
            Alu( ir.y, fetch());
            break;
          case 7:   // RST nn
            DIS("RST nn");
            push( pc);
            pc=ir.y*8;  
            break;
        }    
        break;
    } // switch x
        
    // clear prefix before next instruction
    ixy=0;    
} // decode    



