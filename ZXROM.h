/*
**  ZXROM 
**  2/3/08 ZX Spectrum 48K ROM image
*/

// labels
#define EDITOR      0x0F2C  // editor main entry
#define CLS         0x0D6B  // clearing the screen
#define RAM_DONE    0x11EF  // RAM check
#define MAIN_1      0x12A9  // main execution loop
#define PO_MSG      0x0C0A  // print out a message
#define STKEND      0x5c63  // CALC Stack End
#define STKBOT      0x5c65  // CALC Stack Bottom

extern const byte rom[];