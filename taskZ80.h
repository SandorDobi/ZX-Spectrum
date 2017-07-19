/*
**  ZX Spectrum Emulator task 
**  
*/

#ifndef __TASK_Z80_H
#define __TASK_Z80_H

#define byte unsigned char

// public Z80 features exposed to other tasks
extern int pc;                  // the Z80 program counter
extern volatile byte iff;       // interrupt flag
extern volatile byte INK[8];    // IN values for keyboard 
extern volatile byte border;    // OUT value for screen border
extern byte ram[];              // RAM bank

extern void taskZ80(void* pvParameter);

#endif // __TASK_LCD_H
