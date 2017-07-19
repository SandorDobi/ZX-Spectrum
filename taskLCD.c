///////////////////////////////////////////////////////////////////
// taskLCD code

#include <Graphics/Graphics.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <LCDTerminal.h>
#include "ZXDisplay.h"
#include "taskLCD.h"

// input queue
xQueueHandle hLCDQueue;

void taskLCD(void* pvParameter)
{
	static LCD_MSG msg;
	
	initZXDisplay();        // put the display in ZX mode (window)
	
	// graphics task main loop
	while (1) {
		// block until we receive a new message from the LCD queue
		if (xQueueReceive(hLCDQueue, &msg, portMAX_DELAY) == pdTRUE) {
			// perform message specific processing
			switch (msg.cmd) {
				case MSG_LCD_Clear:
					// clear entire screen and home
					LCDClear();
					break;
				case MSG_LCD_AT:
					AT( msg.bVal[0], msg.bVal[1]);
					break;			
				case MSG_LCD_PutChar:
				    LCDPutChar( msg.bVal[0]);
					break;
			    case MSG_LCD_UP:
                    LCDShiftCursorUp();
			        break;
				case MSG_LCD_DOWN:
                    LCDShiftCursorDown();
				    break;
				case MSG_LCD_LEFT:
				    LCDShiftCursorLeft();
				    break;
				case MSG_LCD_RIGHT:
				    LCDShiftCursorRight();
				    break;
				
				case MSG_ZX_INIT:
				    initZXDisplay();
		            break;
		        case MSG_ZX_UPDATE:
		            ZXUpdate();
				    break;
				default:
					break;
			} // switch
		} // if			
	} // while
} // task LCD

