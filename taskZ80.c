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
//  additional calculator stack debugging
//#ifdef DBGSTK
//    if ( pc==0x338e)
//    {
//        Cp=ram[sptr-0x4000]+(ram[sptr+1-0x4000]<<8)-1;
//        // selective breakpoint
//        if ((Cp>0x2e01)&& (Cp<0x2fE3))
//        {
//            Ccode=rom[Cp];
//    
//            Ci=rmem(STKEND)+(rmem(STKEND+1)<<8);    //STKEND
//            Cd=rmem(STKBOT)+(rmem(STKBOT+1)<<8);    //STKBOT
//
//            for( i=0;i<5;i++)
//                Ca[i]=rmem(Cd-5+i);
//                
//            // convert to ZX float to ieee745 float 
//            ieee.s=Ca[1]>>7;    // move the sign bit
//            ieee.e=Ca[0]-2;     // bias is only +127
//            ieee.m=((Ca[1]&0x7f)<<16)+(Ca[2]<<8)+(Ca[3]);
//            
//            if (Cd-Ci>5)
//            {
//                for( i=0;i<5;i++)
//                    Cb[i]=rmem(Cd-10+i);
//            }        
//            else
//                for(i=0;i<5;i++)
//                    Cb[i]=0xAA;
//            asm("nop");
//        } // in range    
//    }                
//#endif
//


#ifdef DBGZ80
const char hexdig[] = "0123456789abcdef";

// convert an integer into a (0x0000) ascii string
void strhex( char *s, int x)
{
    int i;
    *s++ = ' ';        // space 
    for( i=0; i<4; i++, x<<=4)
        *s++ = hexdig[ (x>>12)&0xf];
    *s = '\0';          // close the string
} // strhex    


// convert an ascii string (hex) into an integer
int htoi( char *s)
{
    int i, r=0;

    // skip leading spaces
    while ( *s == ' ')
        s++;
        
    // decode up to four successive hex digits        
    while ( *s)
    {
        r <<=4;
        for(i=0; i<16; i++)
            if (*s == hexdig[i])
                r+=i;
        s++; 
    }   
    return r;     
} // htoi    

//-------------------------------------------------------------
// debug commands interpreter
//
void debug( void)
{
    static int add = 0;
    int i, length;
    char *buffer;

    // as a default continue stepping after each comand
    run = 0;
    ei = 0;         // disable interrupts during debugging
        
    while( 1)
    {
        //send prompt
        putcUART2( '>');
        
        length = 127;
        buffer = s;
        // collect a command string
        while( length)                         /* read till length is 0 */
        {
            while( !DataRdyUART2());            // wait for character
    
            *buffer = getcUART2();            // get character
            putcUART2( *buffer);
            
            if ( *buffer == '\r')       // end of line
                break;
    
            if ( *buffer == 0x08)       // backspace
            {
                putcUART2( ' ');
                putcUART2( 0x08);
                if ( buffer > s)
                    buffer-=2;
            }
                
            length--;
            buffer++;
        }
        *buffer = '\0';             // terminate string
        putsUART2( "\n\r");
        
        // rewind string
        buffer = s;
        
        switch( *buffer++)         // first character gives 
        { 
          case 'b':     // set a breakpoint
            bkpt = htoi( buffer);    // get breakpoint address
            break;
            
          case 'r':     // run
            run = 1;
            ei = 1;     // re-enable interrupts
            return;            
            
          case 'c':     // dump CPU registers contents
            putsUART2( " BC ="); strhex(s, (reg[B]<<8)+reg[C]);  putsUART2( s); 
            putsUART2( "  DE ="); strhex(s, (reg[D]<<8)+reg[E]);  putsUART2( s); 
            putsUART2( "  HL ="); strhex(s, (reg[B]<<8)+reg[L]);  putsUART2( s); putsUART2( "\n\r");
            putsUART2( " IX ="); strhex(s, (reg[IXH]<<8)+reg[IXL]);  putsUART2( s); 
            putsUART2( "  IY ="); strhex(s, (reg[IYH]<<8)+reg[IYL]);  putsUART2( s); 
            putsUART2( "  SP ="); strhex(s,  sptr);  putsUART2( s); putsUART2( "\n\r");
            break;  
            
          case 'h':     // help
          case 'd':     // dump data
            if ( *buffer == ' ')
                add = htoi( buffer);    // get address
            if ( add <= 0xFFFF)
            {
                // print address
                strhex( s, add); putsUART2( s); putsUART2( " :"); 
                for(i=0; i<8; i++)
                {
                    if ( add >= RAMSTART)
                        strhex( s, ram[ add - RAMSTART +i]);
                    else 
                        strhex( s, rom[ add +i]);
                    putsUART2( s); 
                }    
                putsUART2( "\n\r"); 
            }    
            break;
          
          case 's':     // single step, return
            return;
            
          default:      // empty or unrecognized command
            break;        
        } // switch  
    } // while      
} // debug
#endif
    

void Trap( int trap)
{
    // this is triggered by the NONI-0 (0xED0z) 
    // where z = 0..7 code 
    // used to implement tape emulation et al.
    // code  
    
    static FSFILE *fp = NULL;
    char fname[ 16], *np;
    int i, k, base, lenght, dummy;
    
    switch( trap)
    {
      case 00: // save         
        // pre-load return address
        pc = 0x053F; // SA/LD-RET
        // pre-load error
        flags.c = 0;

            
        // get the block base address 
        base = (reg[IXH]<<8)+reg[IXL]- RAMSTART;
        
        // if header block (A=00)
        if ( reg[A] == 0)
        { // header 
            // mount an sd card, 10 quick tries
            for (i=0; i<10; i++) 
                if ( FSInit()) break;  
            if ( i == 10) 
                return;     // card not found/initialized

            // form legal file name (compressing spaces)
            np = fname;
            for( i=0; i<8; i++)
            {
                *np = ram[ base+1+i];
                if ( *np != ' ') np++;
            }    
            *np = '\0'; strcat( np, ".TAP");

            // open file 
            fp = FSfopen( fname, "w");
            if ( fp == NULL) return;
        } 

        // write header/data block lenght
        lenght = ( (reg[D]<<8) + reg[E] +2) ;
        FSfwrite( &lenght, 1, 2, fp);
        
        // get block lenght
        lenght = (reg[D]<<8)+reg[E];
        
        // write first the flag byte 
        FSfwrite( &reg[ A], 1, 1, fp);
        // write the header/block
        FSfwrite( &ram[ base], 1, lenght, fp);
        // write the checksum (dummy for now)
        FSfwrite( &dummy, 1, 1, fp);
        
        // if data block (A!=00), close file 
        if ( reg[A])
        {
            FSfclose( fp);
        }    
        
        // return with success
        flags.c = 1;
        break;
        
      case 01:  // LOAD 
        // pre-load return address
        pc = 0x806; // loading error
        // pre-load error
        flags.c = 0;

        // get the block base address 
        base = (reg[IXH]<<8)+reg[IXL] - RAMSTART;
        
        // if looking for a header block (A=00)
        if (( reg[A] == 0) && ( fp == NULL))
        { // header 
            // mount an sd card, 10 quick tries
            for (i=0; i<10; i++) 
                if ( FSInit()) break;  
            if ( i == 10) 
                return;     // card not found/initialized
            
            // form legal file name
            np = fname; 
            
            for(i=0, k=0; i<8; i++)
            {
                if ( k==10) break;  
                do {
                    *np = toupper( ram[ base+1-17+k++]);
                } while ( !isalnum( *np) && (k<10));  
                
                if ( isalnum( *np))     
                    np++; 
            }    
            *np = '\0'; strcat( np, ".TAP");

            // open file for reading
            fp = FSfopen( fname, "r");
            if ( fp == NULL)
            { 
                return;    // file not found
            }    
        } // if looking for header
        
        lenght = 0;    
        // read the TAP block lenght 
        FSfread( &lenght, 1, 2, fp);
        
        dummy = 0;
        // read the block flag (first byte)
        FSfread( &dummy, 1, 1, fp);
        
        // check if the block type is a match
        if ( dummy == reg[A])
        { // it can be loaded
            // compare the block (expected) lenght
            if ( lenght == (reg[D]<<8) + reg[E]+2)
            {
                // read a header/data block
                FSfread( &ram[ base], 1, lenght-2, fp);
                // read the checksum (last byte) 
                FSfread( &dummy, 1, 1, fp);
            }
            else    //  skip the (broken) block 
            {
                FSfseek( fp, lenght-1, 0);
                return;         // return with error
            }                                    
        } // if proper type
                
        // if TAP finished, close file 
        if ( FSfeof( fp))
        {
            FSfclose( fp);
            fp = NULL;
        }    
        
        // return with success
        pc = 0x053F; // SA/LD-RET
        flags.c = 1;
        break;
        
      default:  
            // NOP
        break;
    } // switch        
    
} // Trap    


//-------------------------------------------------------------
// main emulator loop
//
void taskZ80( void* pvParameter)
{
    int i;
    
#ifdef DBGZ80    
    MMBUARTInit( 9600);

    // clear all registers
    for( i=0; i<12; i++)
        reg[ i] =i;;
    bkpt= 0xffff;
    run = 1;
    
    putsUART2( "\n\n\rZ80 Emulator: running\n\r");
#endif

    // init CPU
    pc = 0;         // program counter
    sptr = MAXRAM;  // stack pointer
    ei = 0;         // interrupt enable flag
    iff= 0;         // interrupt flag

    // enable audio - remember to set the J1/2 jumpers to position 2-3
    _TRISD0 = 0;    // EAR output to pin RD0     
    
//-------------------------------------------------------------
  
    // main loop
    while( 1)
    {

#ifdef DBGZ80
    if ( pc == bkpt)
        run = 0;

    // if stepping, print address before decoding
    if ( !run)
    {
        // print the current PC
        strhex( s, pc);
        putsUART2( s);
    }
#endif
    

    // check if interrupts enabled and interrupt pending
    if (ei)
    {
        if (iff)
        {
            push( pc);  // save pc on stack
            pc = 0x38;  // MODE1 maskable interrupt vector 
            iff = 0;    // clear interrupt flag
        }
    }
            
    // execution
    decode();

#ifdef DBGZ80
    if (!run) // single stepping
    {
        // show executed instruction //disassembly
        strhex( s, ir.opcode);
        putsUART2( s);   
    
        putsUART2( "  A ="); strhex(s, reg[A]);  putsUART2( s); 
        putsUART2( "  F = ("); 
        putcUART2( ( flags.c) ? 'C' : ' ');
        putcUART2( ( flags.z) ? 'Z' : ' ');
        putcUART2( ( flags.h) ? 'H' : ' ');
        putcUART2( ( flags.n) ? 'N' : ' ');
        putcUART2( ( flags.p) ? 'P' : ' ');
        putcUART2( ( flags.s) ? 'S' : ' ');
        putsUART2( ")\n\r");
        
        // wait for a command
        debug();
    }
    else // runnning
    { // check if a command is incoming 
        if ( DataRdyUART2())
            debug();
    }        
#endif                
    
    } // main simulation loop       
} //main taskZ80
  
  