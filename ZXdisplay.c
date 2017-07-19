/*
** ZXdisplay.c
**
** ZX Spectrum display emulation
**
**  Graphic page for PIC32MX
**  8/02/07 v2.2 
**  10/23/07 v3.0 72MHz operation
**  11/20/07 v3.1 removed OC3 interrupt
**  08/05/09 v.4.0  TG port, Output on QVGA display
**  01/04/10 v.5.0  256x200 4-bit color on PIC32MMB
**  03/14/10 v 6.0  256x192 ZX Spectrum emulation
*/

#include <plib.h>
#include <FreeRTOS.h>
#include <task.h>

#include "ZXDisplay.h"              // 
#include "GenericTypeDefs.h"
#include "Graphics/Graphics.h"      
#include "Graphics/HX8347.h"        // SetReg()
#include "taskZ80.h"                // ram[], border

#define WIDTH   256
#define HEIGHT  192
#define _SC (( 320-WIDTH)/2)
#define _SR (( 240-HEIGHT)/2)

 
// video RAM start at offset 0x0000 in ram[] array

// color look up table ZXSpectrum -> 16-bit 565 color 
static WORD LUT[ 16] = {
    //0      1    2       3      4     5       6      7           
    BLACK, BLUE, RED, MAGENTA, GREEN, CYAN, YELLOW,  GRAY0,
    // 8,      9      A    B     C     D    E    F
    DARKGRAY, BRIGHTBLUE, BRIGHTRED, BRIGHTMAGENTA, BRIGHTGREEN, BRIGHTCYAN, BRIGHTYELLOW,  WHITE
    };


typedef union {
    struct {
        unsigned fore:3;    // background color
        unsigned back:3;    // foreground color
        unsigned high:1;    // bright
        unsigned flash:1;   // flashing
    };
    char b;
} zx_attribute;    


void initZXDisplay( void)
{
    // define TFT display sub window (HX8347-D specific!)
    SetReg(0x03, _SC);
    SetReg(0x04,( WIDTH-1 + _SC)>>8);   // HRESH  
    SetReg(0x05, (WIDTH-1 + _SC));      // HRESL

    SetReg(0x07, _SR);
    SetReg(0x08,0);                     // VRESH
    SetReg(0x09, (HEIGHT-1 + _SR));     // VRESL    
} // initZXDisplay

void initQVGADisplay( void)
{
    // define TFT display sub window (HX8347-D specific!)
    SetReg(0x03, 0);
    SetReg(0x04,( 320-1)>>8);       // HRESH  
    SetReg(0x05, (320-1 + 0));      // HRESL

    SetReg(0x07, 0);
    SetReg(0x08, 0);                // VRESH
    SetReg(0x09, (240-1 + 0));      // VRESL    
} // initZXDisplay
    


#define VMARGIN (240-192)/2
#define HMARGIN (320-256)/2

void ZXUpdate( void)
{
    static byte fCount = 0;
    static byte fMask = 0;
    static byte oborder;
    int p, l, y, x, pix;
    char b, *pV, *pA;
    zx_attribute a;
    int ink, paper;
           
           
           
    // increment the flash counter
    fCount++;
    if ( fCount >= 12)  // ~1Hz flashing
    {
        fCount = 0;
        fMask ^= 0xFF;
    }
        
        
    // further reduce the refresh rate    
    if ( fCount & 1)
        return;
        
    // check if border changed 
//    if (oborder!=border)    
//    {
//        oborder = border;   // update border color 
//
//        SetColor( LUT[ (border>>3)&7]);
//        initQVGADisplay();  // get access to entire screen
//        // draw top margins
//        Bar( 0, 0, 319, VMARGIN-1);
//        // draw side margins
//        Bar( 0,             VMARGIN, HMARGIN-1, 239-VMARGIN);
//        Bar( 320-HMARGIN,   VMARGIN, 319,       239-VMARGIN);
//        // draw bottom margins
//        Bar( 0, 240-VMARGIN, 319, 239);
//        // return to ZX Window
//        initZXDisplay();
//    }
//        
//        
    // refresh the main screen
    CS_LAT_BIT = 0;
    SetAddress( _SC, _SR);
    RS_LAT_BIT = 1;             
    
    pV = &ram[ 0];
    pA = &ram[ 192*32];
    // split screen in three parts
    for( p=0; p<3; p++)
    {
        // eight lines each
        for( l=0; l<8; l++)
        {
            // 8 sub lines each 
            for( y=0; y<8; y++)
            {        
                // 32- characters
                for (x=0; x<32; x++)
                {
                    // get the pixel map 
                    b = *pV++;
                    // get the attributes for this character block
                    a.b = *pA++;
                    if ( a.flash) 
                        b ^= fMask;
                    ink = a.high * 8 + a.fore;
                    paper = a.high * 8 + a.back;
                    
                    // 8 bit/pixel per character
                    for( pix=0; pix<8; pix++, b<<=1)
                    {
                        if ( b & 0x80) 
                          {
                            WriteData( LUT[ ink]);
                          }  
                        else 
                          {
                            WriteData( LUT[ paper]);
                          }  
                    } // next pix
                } // next x
                pA -= 32;
                pV += 7*32;
            } // next y
            // advance to the next sub line (skipping 8-lines)
            pA += 32;
            pV -= (63*32);
        } // next l
        //advance to the next line (returning back)
        pV += 7*8*32;
        taskYIELD( );
    } // next p    
    CS_LAT_BIT = 1;
        
} // ZXUpdate

