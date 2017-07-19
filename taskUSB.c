///////////////////////////////////////////////////////////////////
// taskUSBKeyboard code

#include <plib.h>
#include "USB\usb.h"
#include "USB\usb_host_hid_parser.h"
#include "USB\usb_host_hid.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include "taskUSB.h"
#include "taskLCD.h"
#include "taskZ80.h"        
#include "hardwareprofile.h"


// local prototypes
void LCDDisplayString(BYTE* data, BYTE lineNum);
void LCD_Display_Routine(BYTE data, BYTE HIDData);


// local variables
int iCount;     
volatile BOOL deviceAttached;
xSemaphoreHandle hUSBSemaphore;
static LCD_MSG msg;

// *****************************************************************************
// *****************************************************************************
// Data Structures
// *****************************************************************************
// *****************************************************************************

typedef enum _APP_STATE
{
    DEVICE_NOT_CONNECTED,
    DEVICE_CONNECTED, /* Device Enumerated  - Report Descriptor Parsed */
    READY_TO_TX_RX_REPORT,
    GET_INPUT_REPORT, /* perform operation on received report */
    INPUT_REPORT_PENDING,
    SEND_OUTPUT_REPORT, /* Not needed in case of mouse */
    OUTPUT_REPORT_PENDING,
    ERROR_REPORTED 
} APP_STATE;

typedef struct _HID_REPORT_BUFFER
{
    WORD  Report_ID;
    WORD  ReportSize;
    BYTE* ReportData;
    WORD  ReportPollRate;
}   HID_REPORT_BUFFER;

typedef struct _HID_LED_REPORT_BUFFER
{
    BYTE  NUM_LOCK      : 1;
    BYTE  CAPS_LOCK     : 1;
    BYTE  SCROLL_LOCK   : 1;
    BYTE  UNUSED        : 5;
}   HID_LED_REPORT_BUFFER;



// *****************************************************************************
// *****************************************************************************
// Internal Function Prototypes
// *****************************************************************************
// *****************************************************************************
BYTE App_HID2ASCII(BYTE a); //convert USB HID code (buffer[2 to 7]) to ASCII code
void AppInitialize(void);
BOOL AppGetParsedReportDetails(void);
void App_Detect_Device(void);
void App_ProcessInputReport(void);
void App_PrepareOutputReport(void);
void InitializeTimer(void);
void App_Clear_Data_Buffer(void);
BOOL App_CompareKeyPressedPrevBuf(BYTE data);
void App_CopyToShadowBuffer(void);
BOOL USB_HID_DataCollectionHandler(void);

// *****************************************************************************
// *****************************************************************************
// Macros
// *****************************************************************************
// *****************************************************************************
#define MAX_ALLOWED_CURRENT             (500)         // Maximum power we can supply in mA
#define MINIMUM_POLL_INTERVAL           (0x0A)        // Minimum Polling rate for HID reports is 10ms

#define USAGE_PAGE_LEDS                 (0x08)

#define USAGE_PAGE_KEY_CODES            (0x07)
#define USAGE_MIN_MOD_KEY               (0xE0)
#define USAGE_MAX_MOD_KEY               (0xE7)

#define USAGE_MIN_NORMAL_KEY            (0x00)
#define USAGE_MAX_NORMAL_KEY            (0xFF)

/* Array index for modifier keys */
#define MOD_LEFT_CONTROL           (0)
#define MOD_LEFT_SHIFT             (1)
#define MOD_LEFT_ALT               (2)
#define MOD_LEFT_GUI               (3)
#define MOD_RIGHT_CONTROL          (4)
#define MOD_RIGHT_SHIFT            (5)
#define MOD_RIGHT_ALT              (6)
#define MOD_RIGHT_GUI              (7)

#define HID_CAPS_LOCK_VAL               (0x39)
#define HID_NUM_LOCK_VAL                (0x53)

#define MAX_ERROR_COUNTER               (10)


#define LCD_LINE_ONE                    (1)
#define LCD_LINE_TWO                    (2)

//******************************************************************************
//  macros to identify special charaters(other than Digits and Alphabets)
//******************************************************************************
#define Symbol_Exclamation              (0x1E)
#define Symbol_AT                       (0x1F)
#define Symbol_Pound                    (0x20)
#define Symbol_Dollar                   (0x21)
#define Symbol_Percentage               (0x22)
#define Symbol_Cap                      (0x23)
#define Symbol_AND                      (0x24)
#define Symbol_Star                     (0x25)
#define Symbol_NormalBracketOpen        (0x26)
#define Symbol_NormalBracketClose       (0x27)

#define Symbol_Return                   (0x28)
#define Symbol_Escape                   (0x29)
#define Symbol_Backspace                (0x2A)
#define Symbol_Tab                      (0x2B)
#define Symbol_Space                    (0x2C)
#define Symbol_HyphenUnderscore         (0x2D)
#define Symbol_EqualAdd                 (0x2E)
#define Symbol_BracketOpen              (0x2F)
#define Symbol_BracketClose             (0x30)
#define Symbol_BackslashOR              (0x31)
#define Symbol_SemiColon                (0x33)
#define Symbol_InvertedComma            (0x34)
#define Symbol_Tilde                    (0x35)
#define Symbol_CommaLessThan            (0x36)
#define Symbol_PeriodGreaterThan        (0x37)
#define Symbol_FrontSlashQuestion       (0x38)

// *****************************************************************************
// *****************************************************************************
// Global Variables
// *****************************************************************************
// *****************************************************************************

volatile BOOL deviceAttached;

APP_STATE App_State_Keyboard = DEVICE_NOT_CONNECTED;

HID_DATA_DETAILS Appl_LED_Indicator;


HID_DATA_DETAILS Appl_ModifierKeysDetails;
HID_DATA_DETAILS Appl_NormalKeysDetails;

HID_USER_DATA_SIZE ModKeys[8];
HID_USER_DATA_SIZE NormalKeys[6];
HID_USER_DATA_SIZE Appl_ShadowBuffer1[6];

HID_REPORT_BUFFER     report;
HID_LED_REPORT_BUFFER led_buffer;

BYTE ErrorDriver;
BYTE ErrorCounter;
BYTE NumOfBytesRcvd;

BOOL ReportBufferUpdated;
BOOL LED_Key_Pressed = FALSE;
BOOL DisplayConnectOnce = FALSE;
BOOL DisplayDeatachOnce = FALSE;
BYTE CAPS_Lock_Pressed = 0;
BYTE NUM_Lock_Pressed = 0;
BYTE HeldKeyCount = 0;
BYTE HeldKey;

BYTE currCharPos;
BYTE FirstKeyPressed ;

void taskUSB(void* pvParameter)
{	
	int i;
	
    //Initialize the USB stack
    USBInitialize(0);
    
    // delay startup of the USB stack to phase the power
    vTaskDelay( 500 / portTICK_RATE_MS);

    while(1)
    {
		// periodically perform USB activity
		LD2 = 1;
		vTaskDelay(10 / portTICK_RATE_MS);
		
		LD2 = 0;
		xSemaphoreTake(hUSBSemaphore, portMAX_DELAY);		
		USBTasks();
		xSemaphoreGive(hUSBSemaphore);
		       
        App_Detect_Device();
        switch(App_State_Keyboard)
        {
          case DEVICE_NOT_CONNECTED:
            if(DisplayDeatachOnce == FALSE)
            {
                LD1 = 0;    // turn on LD1 to signal missing/unrecognized keyboard
                DisplayDeatachOnce = TRUE;
            }
            if(USBHostHID_ApiDeviceDetect()) /* True if report descriptor is parsed with no error */
            {
                App_State_Keyboard = DEVICE_CONNECTED;
                DisplayConnectOnce = FALSE;
            }
            break;
        
          case DEVICE_CONNECTED:
            App_State_Keyboard = READY_TO_TX_RX_REPORT;
            if(DisplayConnectOnce == FALSE)
            {
                LD1 = 1;        // turn off LED1 to signal keyboard detected
                DisplayConnectOnce = TRUE;
                DisplayDeatachOnce = FALSE;
            }
            InitializeTimer(); // start 10ms timer to schedule input reports
            break;
            
          case READY_TO_TX_RX_REPORT:
             if(!USBHostHID_ApiDeviceDetect())
             {
                App_State_Keyboard = DEVICE_NOT_CONNECTED;
             }
             else
                App_State_Keyboard = GET_INPUT_REPORT;
            break;
          
          case GET_INPUT_REPORT:
              if(USBHostHID_ApiGetReport(report.Report_ID,Appl_ModifierKeysDetails.interfaceNum,
                                        report.ReportSize, report.ReportData))
            {
               /* Host may be busy/error -- keep trying */
            }
            else
            {
                App_State_Keyboard = INPUT_REPORT_PENDING;
            }
            break;
        
          case INPUT_REPORT_PENDING:
               if(USBHostHID_ApiTransferIsComplete(&ErrorDriver,&NumOfBytesRcvd))
                {
                    if(ErrorDriver ||(NumOfBytesRcvd !=     report.ReportSize ))
                    {
                        ErrorCounter++ ; 
                        if(MAX_ERROR_COUNTER <= ErrorDriver)
                            App_State_Keyboard = ERROR_REPORTED;
                        else
                            App_State_Keyboard = READY_TO_TX_RX_REPORT;
                    }
                    else
                    {
                        ErrorCounter = 0; 
                        ReportBufferUpdated = TRUE;
                        App_State_Keyboard = READY_TO_TX_RX_REPORT;

                      if(DisplayConnectOnce == TRUE)
                            for(i=0;i<report.ReportSize;i++)
                                if(report.ReportData[i] != 0)
                                    DisplayConnectOnce = FALSE;

                        App_ProcessInputReport();
                        App_PrepareOutputReport();
                    }
                }
            break;

          case SEND_OUTPUT_REPORT: /* Will be done while implementing Keyboard */
            if(USBHostHID_ApiSendReport(Appl_LED_Indicator.reportID,Appl_LED_Indicator.interfaceNum, Appl_LED_Indicator.reportLength,
               (BYTE*)&led_buffer))
            {
                /* Host may be busy/error -- keep trying */
            }
            else
            {
                App_State_Keyboard = OUTPUT_REPORT_PENDING;
            }
                     
            break;
        
          case OUTPUT_REPORT_PENDING:
            if(USBHostHID_ApiTransferIsComplete(&ErrorDriver,&NumOfBytesRcvd))
            {
                if(ErrorDriver)
                {
                    ErrorCounter++ ; 
                    if(MAX_ERROR_COUNTER <= ErrorDriver)
                        App_State_Keyboard = ERROR_REPORTED;
                }
                else
                {
                    ErrorCounter = 0; 
                    App_State_Keyboard = READY_TO_TX_RX_REPORT;
                }
            }
            break;

          case ERROR_REPORTED:
            break;
        
          default:
            break;
        } // switch
    } // while
} // task 



//******************************************************************************
//******************************************************************************
// USB Support Functions
//******************************************************************************
//******************************************************************************

BOOL USB_ApplicationEventHandler( BYTE address, USB_EVENT event, void *data, DWORD size )
{
    switch( event )
    {
        case EVENT_VBUS_REQUEST_POWER:
            // The data pointer points to a byte that represents the amount of power
            // requested in mA, divided by two.  If the device wants too much power,
            // we reject it.
            if (((USB_VBUS_POWER_EVENT_DATA*)data)->current <= (MAX_ALLOWED_CURRENT / 2))
            {
                return TRUE;
            }
            else
            {
              //  UART2PrintString( "\r\n***** USB Error - device requires too much current *****\r\n" );
            }
            break;

        case EVENT_VBUS_RELEASE_POWER:
            // Turn off Vbus power.
            deviceAttached = FALSE;
            return TRUE;
            break;

        case EVENT_HUB_ATTACH:
            //UART2PrintString( "\r\n***** USB Error - hubs are not supported *****\r\n" );
            return TRUE;
            break;

        case EVENT_UNSUPPORTED_DEVICE:
            //UART2PrintString( "\r\n***** USB Error - device is not supported *****\r\n" );
            return TRUE;
            break;

        case EVENT_CANNOT_ENUMERATE:
            //UART2PrintString( "\r\n***** USB Error - cannot enumerate device *****\r\n" );
            return TRUE;
            break;

        case EVENT_CLIENT_INIT_ERROR:
            //UART2PrintString( "\r\n***** USB Error - client driver initialization error *****\r\n" );
            return TRUE;
            break;

        case EVENT_OUT_OF_MEMORY:
            //UART2PrintString( "\r\n***** USB Error - out of heap memory *****\r\n" );
            return TRUE;
            break;

        case EVENT_UNSPECIFIED_ERROR:   // This should never be generated.
            //UART2PrintString( "\r\n***** USB Error - unspecified *****\r\n" );
            return TRUE;
            break;

		case EVENT_HID_RPT_DESC_PARSED:
			 #ifdef APPL_COLLECT_PARSED_DATA
			     return(APPL_COLLECT_PARSED_DATA());
		     #else
				 return TRUE;
			 #endif
			break;

        default:
            break;
    }
    return FALSE;
}

/****************************************************************************
  Function:
    void App_PrepareOutputReport(void)

  Description:
    This function schedules output report if any LED indicator key is pressed.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    None
***************************************************************************/
void App_PrepareOutputReport(void)
{
//    if((READY_TO_TX_RX_REPORT == App_State_Keyboard) && (ReportBufferUpdated == TRUE))
    if(ReportBufferUpdated == TRUE)
    {
        ReportBufferUpdated = FALSE;
        if(LED_Key_Pressed)
        {
            App_State_Keyboard = SEND_OUTPUT_REPORT;
            LED_Key_Pressed = FALSE;
        }
     }
}


typedef union {
    struct {
        unsigned key:8;
        unsigned row:3;
        unsigned :2;
        unsigned ext:1;
        unsigned sym:1;
        unsigned shift:1;
    };
    short val;
} scan_code;
        
// ZXSpecturm keyboard codes 
//---------------------------------------------------------------------------
//        0     1     2     3     4 -Bits-  4     3     2     1     0
//3  [ 1 ] [ 2 ] [ 3 ] [ 4 ] [ 5 ]  |  [ 6 ] [ 7 ] [ 8 ] [ 9 ] [ 0 ]   4
//2  [ Q ] [ W ] [ E ] [ R ] [ T ]  |  [ Y ] [ U ] [ I ] [ O ] [ P ]   5
//1  [ A ] [ S ] [ D ] [ F ] [ G ]  |  [ H ] [ J ] [ K ] [ L ] [ ENT ] 6
//0  [SHI] [ Z ] [ X ] [ C ] [ V ]  |  [ B ] [ N ] [ M ] [sym] [ SPC ] 7
//     1     2     4     8     10       10     8     4     2      1                                    

const short HID2KBD[ 0x58] = {
//00     X,      X,      X,      X,      A,      B,      C,      D,  
    0x0000, 0x0000, 0x0000, 0x0000, 0x0101, 0x0710, 0x0008, 0x0104,
//08     E,      F,      G,      H,      I,      J,      K,      L,  
    0x0204, 0x0108, 0x0110, 0x0610, 0x0504, 0x0608, 0x0604, 0x0602,
//10     M,      N,      O,      P,      Q,      R,      S,      T,  
    0x0704, 0x0708, 0x0502, 0x0501, 0x0201, 0x0208, 0x0102, 0x0210,
//18     U,      V,      W,      X,      Y,      Z,      1,      2,  
    0x0508, 0x0010, 0x0202, 0x0004, 0x0510, 0x0002, 0x0301, 0x0302,
//20     3,      4,      5,      6,      7,      8,      9,      0,  
    0x0304, 0x0308, 0x0310, 0x0410, 0x0408, 0x0404, 0x0402, 0x0401,
//28   RET,    ESC,   BKSP,    TAB,  SPACE,      -,      =,      [,  
    0x0601, 0x0000, 0x8401, 0x0000, 0x0701, 0x0000, 0x0000, 0x0000,
//30     ],      |,  EURO1,      ;,      ",      ~,      <,      >,  
    0x0000, 0x0000, 0x0000, 0x4502, 0x4501, 0x0000, 0x4708, 0x4704,
//38     ?,   CAPS,     F1,     F2,     F3,     F4,     F5,     F6,  
    0x0000, 0x8302, 0x8301, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
//40    F7,     F8,     F9,    F10,    F11,    F12,  PRINT, SCROLL,  
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
//48 BREAK,    INS,   HOME,  PG UP,    DEL,    END,  PG DN,  RIGHT,  
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x8404,    
//50  LEFT,   DOWN,     UP,    NUM,  KPD /,  KPD *,  KPD -,  KPD +,  
    0x8310, 0x8410, 0x8408, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
}; // HID2KBD

   
/****************************************************************************/
short ConvertHID2KBD(BYTE a) 
{
    short code;
    // if code is found in table convert to ZX KBD position code
    if ( a < (sizeof( HID2KBD)/sizeof(short)))
    {
        return HID2KBD[ a];        
    }
    else
        return 0;
     
} // ConvertHID2KBD
   

///* Array index for modifier keys */
//#define MOD_LEFT_CONTROL           (0)
//#define MOD_LEFT_SHIFT             (1)
//#define MOD_LEFT_ALT               (2)
//#define MOD_LEFT_GUI               (3)
//#define MOD_RIGHT_CONTROL          (4)
//#define MOD_RIGHT_SHIFT            (5)
//#define MOD_RIGHT_ALT              (6)
//#define MOD_RIGHT_GUI              (7)

/****************************************************************************
  Function:
    void App_ProcessInputReport(void)

  Description:
    This function processes input report received from HID device.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    None
***************************************************************************/
void App_ProcessInputReport(void)
{
    int     i;
    scan_code code;
    
   /* process input report received from device */
    USBHostHID_ApiImportData(report.ReportData, report.ReportSize
                          ,ModKeys, &Appl_ModifierKeysDetails);
    USBHostHID_ApiImportData(report.ReportData, report.ReportSize
                          ,NormalKeys, &Appl_NormalKeysDetails);

    // clear the ZX IN register (INK[] array)
    for( i=0; i<8; i++)
        INK[i] = 0x1f;
            
    // for each currently pressed key map it into the ZX Keyboard IN register
    for(i=0;i<(sizeof(NormalKeys)/sizeof(NormalKeys[0]));i++)
    {
        if ( NormalKeys[ i] == 0x48)    // intercept a Break key to reset emulator
            pc = 0;
         
        else if ( NormalKeys[ i] != 0)
        {
            // convert the HID value in a scan_code
            code.val = ConvertHID2KBD( NormalKeys[i]);
            
            // separate implicity shift for extended keyboard functions
            if ( code.shift)
                ModKeys[ MOD_LEFT_SHIFT] = 1;
            if ( code.sym)
                ModKeys[ MOD_LEFT_CONTROL] = 1;
                    
            // decode basic key position
            INK[ code.row] &= ~code.key;
        }
    }
    
    // add the modifiers (shift, control, alt keys )
   if((ModKeys[MOD_LEFT_SHIFT] == 1)||
        (ModKeys[MOD_RIGHT_SHIFT] == 1))
    {
        INK[ 0] &= 0x1e;    // shift
    }    

   if((ModKeys[MOD_LEFT_CONTROL] == 1)||
        (ModKeys[MOD_RIGHT_CONTROL] == 1))
    {
        INK[ 7] &= 0x1d;    // sym
    }    
} // App_ProcessInputReport        
        


/****************************************************************************
  Function:
    void App_Detect_Device(void)

  Description:
    This function monitors the status of device connected/disconnected

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    None
***************************************************************************/
void App_Detect_Device(void)
{
  if(!USBHostHID_ApiDeviceDetect())
  {
     App_State_Keyboard = DEVICE_NOT_CONNECTED;
  }
}



/****************************************************************************
  Function:
    BOOL USB_HID_DataCollectionHandler(void)
  Description:
    This function is invoked by HID client , purpose is to collect the 
    details extracted from the report descriptor. HID client will store
    information extracted from the report descriptor in data structures.
    Application needs to create object for each report type it needs to 
    extract.
    For ex: HID_DATA_DETAILS Appl_ModifierKeysDetails;
    HID_DATA_DETAILS is defined in file usb_host_hid_appl_interface.h
    Each member of the structure must be initialized inside this function.
    Application interface layer provides functions :
    USBHostHID_ApiFindBit()
    USBHostHID_ApiFindValue()
    These functions can be used to fill in the details as shown in the demo
    code.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    TRUE    - If the report details are collected successfully.
    FALSE   - If the application does not find the the supported format.

  Remarks:
    This Function name should be entered in the USB configuration tool
    in the field "Parsed Data Collection handler".
    If the application does not define this function , then HID cient 
    assumes that Application is aware of report format of the attached
    device.
***************************************************************************/
BOOL USB_HID_DataCollectionHandler(void)
{
  BYTE NumOfReportItem = 0;
  BYTE i;
  USB_HID_ITEM_LIST* pitemListPtrs;
  USB_HID_DEVICE_RPT_INFO* pDeviceRptinfo;
  HID_REPORTITEM *reportItem;
  HID_USAGEITEM *hidUsageItem;
  BYTE usageIndex;
  BYTE reportIndex;
  BOOL foundLEDIndicator = FALSE;
  BOOL foundModifierKey = FALSE;
  BOOL foundNormalKey = FALSE;

  pDeviceRptinfo = USBHostHID_GetCurrentReportInfo(); // Get current Report Info pointer
  pitemListPtrs = USBHostHID_GetItemListPointers();   // Get pointer to list of item pointers

  BOOL status = FALSE;
   /* Find Report Item Index for Modifier Keys */
   /* Once report Item is located , extract information from data structures provided by the parser */
   NumOfReportItem = pDeviceRptinfo->reportItems;
   for(i=0;i<NumOfReportItem;i++)
    {
       reportItem = &pitemListPtrs->reportItemList[i];
       if((reportItem->reportType==hidReportInput) && (reportItem->dataModes == HIDData_Variable)&&
           (reportItem->globals.usagePage==USAGE_PAGE_KEY_CODES))
        {
           /* We now know report item points to modifier keys */
           /* Now make sure usage Min & Max are as per application */
            usageIndex = reportItem->firstUsageItem;
            hidUsageItem = &pitemListPtrs->usageItemList[usageIndex];
            if((hidUsageItem->usageMinimum == USAGE_MIN_MOD_KEY)
                &&(hidUsageItem->usageMaximum == USAGE_MAX_MOD_KEY)) //else application cannot suuport
            {
               reportIndex = reportItem->globals.reportIndex;
               Appl_ModifierKeysDetails.reportLength = (pitemListPtrs->reportList[reportIndex].inputBits + 7)/8;
               Appl_ModifierKeysDetails.reportID = (BYTE)reportItem->globals.reportID;
               Appl_ModifierKeysDetails.bitOffset = (BYTE)reportItem->startBit;
               Appl_ModifierKeysDetails.bitLength = (BYTE)reportItem->globals.reportsize;
               Appl_ModifierKeysDetails.count=(BYTE)reportItem->globals.reportCount;
               Appl_ModifierKeysDetails.interfaceNum= USBHostHID_ApiGetCurrentInterfaceNum();
               foundModifierKey = TRUE;
            }

        }
        else if((reportItem->reportType==hidReportInput) && (reportItem->dataModes == HIDData_Array)&&
           (reportItem->globals.usagePage==USAGE_PAGE_KEY_CODES))
        {
           /* We now know report item points to modifier keys */
           /* Now make sure usage Min & Max are as per application */
            usageIndex = reportItem->firstUsageItem;
            hidUsageItem = &pitemListPtrs->usageItemList[usageIndex];
            if((hidUsageItem->usageMinimum == USAGE_MIN_NORMAL_KEY)
                &&(hidUsageItem->usageMaximum <= USAGE_MAX_NORMAL_KEY)) //else application cannot suuport
            {
               reportIndex = reportItem->globals.reportIndex;
               Appl_NormalKeysDetails.reportLength = (pitemListPtrs->reportList[reportIndex].inputBits + 7)/8;
               Appl_NormalKeysDetails.reportID = (BYTE)reportItem->globals.reportID;
               Appl_NormalKeysDetails.bitOffset = (BYTE)reportItem->startBit;
               Appl_NormalKeysDetails.bitLength = (BYTE)reportItem->globals.reportsize;
               Appl_NormalKeysDetails.count=(BYTE)reportItem->globals.reportCount;
               Appl_NormalKeysDetails.interfaceNum= USBHostHID_ApiGetCurrentInterfaceNum();
               foundNormalKey = TRUE;
            }

        }
        else if((reportItem->reportType==hidReportOutput) &&
                (reportItem->globals.usagePage==USAGE_PAGE_LEDS))
        {
            usageIndex = reportItem->firstUsageItem;
            hidUsageItem = &pitemListPtrs->usageItemList[usageIndex];

            reportIndex = reportItem->globals.reportIndex;
            Appl_LED_Indicator.reportLength = (pitemListPtrs->reportList[reportIndex].outputBits + 7)/8;
            Appl_LED_Indicator.reportID = (BYTE)reportItem->globals.reportID;
            Appl_LED_Indicator.bitOffset = (BYTE)reportItem->startBit;
            Appl_LED_Indicator.bitLength = (BYTE)reportItem->globals.reportsize;
            Appl_LED_Indicator.count=(BYTE)reportItem->globals.reportCount;
            Appl_LED_Indicator.interfaceNum= USBHostHID_ApiGetCurrentInterfaceNum();
            foundLEDIndicator = TRUE;
        }

    }


   if(pDeviceRptinfo->reports == 1)
    {
        report.Report_ID = 0;
        report.ReportSize = (pitemListPtrs->reportList[reportIndex].inputBits + 7)/8;
        report.ReportData = (BYTE*)malloc(report.ReportSize);
        report.ReportPollRate = pDeviceRptinfo->reportPollingRate;
        if((foundNormalKey == TRUE)&&(foundModifierKey == TRUE))
        status = TRUE;
    }

    return(status);
}

/****************************************************************************
  Function:
    void LCD_Display_Routine(BYTE data , BYTE HIDData)
  Description:
    This function displays the key strokes on the LCD  mounted on MikroE-MMB
    demo board. 

  Precondition:
    None

  Parameters:
    BYTE data       -   ASCII code for the key pressed
    BYTE HIDData    -   HID code for the key pressed, this is needed to take
                        action for keys like Esc, Enter, Tab etc.

  Return Values:
    None

  Remarks:
***************************************************************************/

void LCD_Display_Routine(BYTE data, BYTE HIDData)
{
    msg.cmd = MSG_LCD_PutChar;

   if((HIDData>=0x1E && HIDData<=0x27) || (HIDData>=0x04 && HIDData<=0x1D) ||
            (HIDData>=0x2D && HIDData<=0x38) || ((HIDData>=0x59 && HIDData<=0x62)&&(NUM_Lock_Pressed == 1)))
    {
        msg.cmd = MSG_LCD_PutChar;
        msg.bVal[0] = data;
        xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
    }
    else if(HIDData == 0x29)    // escape key pressed
    {
        msg.cmd = MSG_LCD_Clear;
        xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
    }
    else if (HIDData == 0x2C) // space pressed
    {
        msg.cmd = MSG_LCD_PutChar;
        msg.bVal[0] = ' ';
        xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
    }
    else if (HIDData == Symbol_Backspace) // back space pressed
    {
        msg.cmd = MSG_LCD_PutChar;
        msg.bVal[0] = '\b';
        xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
    }
    else if((HIDData>=0x4F && HIDData<=0x52) ||
            (( HIDData==0x5C || HIDData==0x5E || HIDData==0x5A || HIDData==0x60  )
               && (NUM_Lock_Pressed == 0)))
    {
        switch(HIDData)
        {
            case 0x4F :   // Right Arrow
            case 0x5E :
                msg.cmd = MSG_LCD_RIGHT;
                xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
                break;
            case 0x50 :   // Left Arrow
            case 0x5C :
        msg.cmd = MSG_LCD_LEFT;
        xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
                break;
            case 0x52 :   // Up Arrow
            case 0x60 :
                msg.cmd = MSG_LCD_UP;
                xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
                break;
            case 0x51 :   // Down Arrow
            case 0x5A :
                msg.cmd = MSG_LCD_DOWN;
                xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
                break;
            default :
                break;
        }

    }
      
}

/****************************************************************************
  Function:
    void LCDDisplayString(BYTE* data, BYTE lineNum)
  Description:
    This function displays the string on the LCD 

  Precondition:
    None

  Parameters:
    BYTE* data      -   Array of characters to be displayed on the LCD
    BYTE lineNum    -   LCD_LINE_ONE : To display on Line one to the LCD
                        LCD_LINE_TWO : To display on Line two to the LCD

  Return Values:
    None

  Remarks:
***************************************************************************/
void LCDDisplayString(BYTE* data, BYTE lineNum)
{
    msg.cmd = MSG_LCD_Clear;
    xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
    
    while((*data != '\0')) // || (index < 16))
    {
        msg.cmd = MSG_LCD_PutChar;
        msg.bVal[0] = *data;
        xQueueSend(hLCDQueue, &msg, portMAX_DELAY);
         data++;
    }
} // LCDDisplayString


//
//
void InitializeTimer( void )
{ 
    OpenTimer4( T4_ON |  T4_SOURCE_INT | T4_PS_1_64, 0x4000);
    ConfigIntTimer4( T4_INT_PRIOR_1 | T4_INT_SUB_PRIOR_1);
    INTClearFlag( INT_T4);
    INTEnable( INT_T4, INT_ENABLED);

    // init interrupt count
    iCount = 0; 
}


void __ISR( _TIMER_4_VECTOR, ipl4) _T4Interrupt( void )
{
    portBASE_TYPE Task;
    static int alt=0;

    iCount++;
        
    INTClearFlag( INT_T4);
    
    // cause the host to start a new request for input from kbd
    if( READY_TO_TX_RX_REPORT == App_State_Keyboard)
    {
        App_State_Keyboard = GET_INPUT_REPORT; // If no report is pending schedule new request
    }
    
    // mark a Z80 interrupt is pending (50Hz)
    if ( iCount >= IPR)
    { 
        iff = 1;    // set Z80 interrupt flag
        iCount = 0; // counts

        // mark a screen refresh is requested (25fps)
        if (alt^=1)
        {
            LD3 = 0;    // turn on LD3 to signal keyboard interrupt and refresh 20ms?
            msg.cmd = MSG_ZX_UPDATE;
            xQueueSendFromISR(hLCDQueue, &msg, &Task);
        }    
        LD3 = 1;    // turn off LD3    
    }    
}
