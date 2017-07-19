/*
**  ZX-Spectrum Emulator main.c
**
** Author: Lucio Di Jasio lucio@dijasio.com
**
** 03/14/10 v0.1 migrated from AV16/32 board to PIC32 MMB
** 03/21/10 v0.2 enh. keyboard, addded load and save from SD card
*/

#include <Graphics/Graphics.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include "taskUSB.h"
#include "taskLCD.h"
#include "taskZ80.h"

#include "GenericTypeDefs.h"
#include "HardwareProfile.h"
#include "usb_config.h"
#include "LCDTerminal.h"
#include "USB\usb.h"
#include "USB\usb_host_hid_parser.h"
#include "USB\usb_host_hid.h"
#include "MMB.h"
#include "JPEGImage.h"

//#define DEBUG_MODE

// redirect error messages to screen 
#ifndef DEBUG_MODE
    #define UART2PrintString    LCDPutString
#endif

extern const FONT_FLASH TerminalFont;
extern BITMAP_FLASH jpgKbd;


// *****************************************************************************
// *****************************************************************************
// Configuration Bits
// *****************************************************************************
// *****************************************************************************

// Configuration Bit settings

    #pragma config UPLLEN   = ON            // USB PLL Enabled
    #pragma config FPLLMUL  = MUL_20        // PLL Multiplier
    #pragma config UPLLIDIV = DIV_2         // USB PLL Input Divider
    #pragma config FPLLIDIV = DIV_2         // PLL Input Divider
    #pragma config FPLLODIV = DIV_1         // PLL Output Divider
    #pragma config FPBDIV   = DIV_1         // Peripheral Clock divisor
    #pragma config FWDTEN   = OFF           // Watchdog Timer
    #pragma config WDTPS    = PS1           // Watchdog Timer Postscale
    #pragma config FCKSM    = CSDCMD        // Clock Switching & Fail Safe Clock Monitor
    #pragma config OSCIOFNC = OFF           // CLKO Enable
    #pragma config POSCMOD  = HS            // Primary Oscillator
    #pragma config IESO     = OFF           // Internal/External Switch-over
    #pragma config FSOSCEN  = OFF           // Secondary Oscillator Enable (KLO was off)
    #pragma config FNOSC    = PRIPLL        // Oscillator Selection
    #pragma config CP       = OFF           // Code Protect
    #pragma config BWP      = OFF           // Boot Flash Write Protect
    #pragma config PWP      = OFF           // Program Flash Write Protect
    #pragma config ICESEL   = ICS_PGx2      // ICE/ICD Comm Channel Select
    #pragma config DEBUG    = ON            // Background Debugger Enable



//******************************************************************************
//******************************************************************************
// Main
//******************************************************************************
//******************************************************************************

int main (void)
{
    BYTE i;
 
    MMBInit();      // init MIkroE-MMB peripherals
    InitGraph();
    JPEGInit();     // Initialize JPEG

// Show Splash Screen
    {     
        SetColor( WHITE);
        SetFont( (void*)&TerminalFont);
        MMBCenterString( 5, "ZX Spectrum Emulator");
        MMBCenterString( 6, "v0.2 Lucio Di Jasio");
        
        JPEGPutImage(16,5,&jpgKbd); 
        MMBFadeIn( 250);
//        MMBGetKey();
        DelayMs(2000);

        SetColor( GRAY0);
        ClearDevice();
        
    } // splash screen
    
	// create a semaphore to control access to the USB system
	hUSBSemaphore = xSemaphoreCreateMutex();
	
	
	// create the queue used to send messages to the LCD system
	hLCDQueue = xQueueCreate(LCD_QUEUE_SIZE, sizeof(LCD_MSG));
			
	///////////////////////////////////////////////////////////////
	// create the USB task
	xTaskCreate(taskUSB, "USB", 480, NULL, 3, NULL);
	

	// create the LCD task
	xTaskCreate(taskLCD, "LCD", 240, NULL, 2, NULL);
	
	// create the Z80 task
	xTaskCreate(taskZ80, "Z80", 380, NULL, 2, NULL);

	// start the scheduler
	vTaskStartScheduler();
	
    return 0;
}  // main
        

/*********************************************************************
 * Function:        void vApplicationStackOverflowHook(void)
 *
 * Overview:        This function is called if a task overflows its
 *					stack, it can be used for diagnostics
 ********************************************************************/
void vApplicationStackOverflowHook( void )
{
	/* Look at pxCurrentTCB to see which task overflowed its stack. */
	while (1) {
		portNOP();
	}
}





