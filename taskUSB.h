///////////////////////////////////////////////////////////////////
// USB KBD task header file

#define IPR     2       // 2 * 10ms USB interrupt

// prototypes
extern xSemaphoreHandle hUSBSemaphore;
extern void taskUSB(void* pvParameter);



