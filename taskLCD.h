// LCD task

#ifndef __TASK_LCD_H
#define __TASK_LCD_H

// FreeRTOS includes
#include <GenericTypeDefs.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

///////////////////////////////////////////////////////////////////
// objects to be passed to the graphics display routines are
// encapsulated in this data type. All incoming messages are
// expressed using this structure and then passed onto the
// incoming queue
typedef struct{
	WORD	cmd;
	union {
		BYTE		bVal[8];
		WORD		wVal[4];
		DWORD		dVal[2];
//		GOL_MSG 	golMsg;
	} ;
} LCD_MSG;

// the queue used to send messages to the graphics task
extern xQueueHandle hLCDQueue;


// number of entries in this queue
#define LCD_QUEUE_SIZE	16

///////////////////////////////////////////////////////////////////
// Defines for the possible messages to be sent
#define MSG_DEFAULT					0	// -
#define MSG_LCD_Clear               1	// -
#define MSG_LCD_AT                  2   // AT X, Y
#define MSG_LCD_PutChar				3	// C
#define MSG_LCD_UP		            4   // -
#define MSG_LCD_DOWN                5   // -
#define MSG_LCD_LEFT                6   // -
#define MSG_LCD_RIGHT               7   // -

#define MSG_ZX_INIT                 100 
#define MSG_ZX_UPDATE               101


// Graphics task itself
extern void taskLCD(void* pvParameter);

#endif // __TASK_LCD_H
