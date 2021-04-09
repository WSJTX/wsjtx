
/*   Linux / Windows program to control the frequency of a si570 synthesizer
     ON5GN  6 jan 2012
     Under Linux:
       -use the linux version of function void si570_sleep(int us)
       -compile with
        gcc -Wall -o set_si570_freq set_si570_freq.c -lusb -lm
       -run with sudo ./set_si570_freq
     Under Windows:
       -use the windows version of function void si570_sleep(int us)
       -compile with mingw
        C:\mingw\bin\mingw32-gcc -Wall -o set_si570_freq set_si570_freq.c -lusb -lm
       -run with  set_si570_freq.exe
*/

#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <sys/time.h>

#ifdef WIN32
#include <windows.h>
#endif

#include <usb.h>
//#include "/users/joe/linrad/3.37/usb.h"
#include <QDebug>

#define USB_SUCCESS	            0
#define USB_ERROR_NOTFOUND      1
#define USB_ERROR_ACCESS        2
#define USB_ERROR_IO            3
#define VENDOR_NAME	            "www.obdev.at"
#define PRODUCT_NAME            "DG8SAQ-I2C"
#define USBDEV_SHARED_VENDOR    0x16C0  // VOTI  VID
#define USBDEV_SHARED_PRODUCT   0x05DC  // OBDEV PID
                                     // Use obdev's generic shared VID/PID pair
                                     // following the rules outlined in
                                     // firmware/usbdrv/USBID-License.txt.
#define REQUEST_SET_FREQ_BY_VALUE 0x32
#define MAX_USB_ERR_CNT         6

double freq_parm;
double delay_average;
int  from_freq;
int  to_freq;
int  increment_freq;
int  retval = -1;
int  display_freq = -1;
int  delay;
usb_dev_handle  *global_si570usb_handle = NULL;

// ********sleep functions***************
//use this function  under LINUX
/*
void si570_sleep(int us)
{
usleep(us);
}
*/

//use this function under WINDOWS
void si570_sleep(int us)
{
  Sleep(us/1000);
}

double round(double x)
{
  int i=x+0.5;
  return (double)i;
}

double current_time(void) //for delay measurements
{
  struct timeval t;
  gettimeofday(&t,NULL);
  return 0.000001*t.tv_usec+t.tv_sec;
}

int  usbGetStringAscii(usb_dev_handle *dev, int my_index,
               int langid, char *buf, int buflen);
unsigned char Si570usbOpenDevice(usb_dev_handle **device, char *usbSerialID);
void setLongWord( int value, char * bytes);
int setFreqByValue(usb_dev_handle * handle, double frequency);
void sweepa_freq(void);
void sweepm_freq(void);

int set570(double freq_MHz)
{
//###
//  qDebug() << "A" << freq_MHz;
//  if(freq_MHz != 999.0) return 0;
//###

  char * my_usbSerialID = NULL;

// MAIN MENU DIALOG
  retval=Si570usbOpenDevice(&global_si570usb_handle, my_usbSerialID);
  if (retval != 0) return -1;

//SET FREQUENCY
  if((freq_MHz < 3.45)|(freq_MHz > 866.0)) return -2;
  retval=setFreqByValue(global_si570usb_handle,freq_MHz);
  return 0;
}

int  usbGetStringAscii(usb_dev_handle *dev, int my_index,
                       int langid, char *buf, int buflen)
{
  char    buffer[256];
  int     rval, i;
  if((rval = usb_control_msg(dev, USB_ENDPOINT_IN, USB_REQ_GET_DESCRIPTOR,
     (USB_DT_STRING << 8) + my_index, langid, buffer,
     sizeof(buffer), 1000)) < 0) return rval;
  if(buffer[1] != USB_DT_STRING)  return 0;
  if((unsigned char)buffer[0] < rval) rval = (unsigned char)buffer[0];
  rval /= 2;
// lossy conversion to ISO Latin1
  for(i=1;i<rval;i++) {
    if(i > buflen) break;                       // destination buffer overflow
    buf[i-1] = buffer[2 * i];
    if(buffer[2 * i + 1] != 0)  buf[i-1] = '?'; // outside of ISO Latin1 range
  }
  buf[i-1] = 0;
  return i-1;
}

unsigned char Si570usbOpenDevice(usb_dev_handle **device, char *usbSerialID)
{
  struct usb_bus      *bus;
  struct usb_device   *dev;
  usb_dev_handle      *handle = NULL;
  unsigned char       errorCode = USB_ERROR_NOTFOUND;
  char                string[256];
  int                 len;
  int  vendor        = USBDEV_SHARED_VENDOR;
  char *vendorName   = (char *)VENDOR_NAME;
  int  product       = USBDEV_SHARED_PRODUCT;
  char *productName  = (char *)PRODUCT_NAME;
  char serialNumberString[20];
  static int  didUsbInit = 0;

  if(!didUsbInit) {
    didUsbInit = 1;
    usb_init();
  }
  usb_find_busses();
  usb_find_devices();
  for(bus=usb_get_busses(); bus; bus=bus->next) {
    for(dev=bus->devices; dev; dev=dev->next) {
      if(dev->descriptor.idVendor == vendor &&
     dev->descriptor.idProduct == product) {
        handle = usb_open(dev); // open the device in order to query strings
        if(!handle) {
          errorCode = USB_ERROR_ACCESS;
          printf("si570.c: Warning: cannot open Si570-USB device:\n");
          printf("usb error message: %s\n",usb_strerror());
          continue;
    }
        if(vendorName == NULL && productName == NULL) {  //name does not matter
          break;
    }
        // now check whether the names match
        len = usbGetStringAscii(handle, dev->descriptor.iManufacturer, 0x0409, string, sizeof(string));
        if(len < 0) {
          errorCode = USB_ERROR_IO;
          printf("si570.c: Warning: cannot query manufacturer for Si570-USB device:\n");
          printf("usb error message: %s\n",usb_strerror());
    } else {
          errorCode = USB_ERROR_NOTFOUND;
           //fprintf(stderr, "seen device from vendor ->%s<-\n", string);
          if(strcmp(string, vendorName) == 0){
            len = usbGetStringAscii(handle, dev->descriptor.iProduct,
                    0x0409, string, sizeof(string));
            if(len < 0) {
              errorCode = USB_ERROR_IO;
              printf("si570.c: Warning: cannot query product for Si570-USB device: \n");
              printf("usb error message: %s\n",usb_strerror());
        } else {
              errorCode = USB_ERROR_NOTFOUND;
              // fprintf(stderr, "seen product ->%s<-\n", string);
              if(strcmp(string, productName) == 0) {
        len = usbGetStringAscii(handle, dev->descriptor.iSerialNumber,
             0x0409, serialNumberString, sizeof(serialNumberString));
        if (len < 0) {
          errorCode = USB_ERROR_IO;
          printf("si570.c: Warning: cannot query serial number for Si570-USB device: \n");
                  printf("usb error message: %s\n",usb_strerror());
        } else {
          errorCode = USB_ERROR_NOTFOUND;
          if ((usbSerialID == NULL) ||
              (strcmp(serialNumberString, usbSerialID) == 0)) {
//                    printf("\nOpen Si570 USB device: OK\n");
//                    printf("usbSerialID          : %s\n",serialNumberString);
            break;
          }
        }
          }
        }
      }
    }
        usb_close(handle);
        handle = NULL;
      }
    }
    if(handle) break;
  }
  if(handle != NULL) {
    errorCode = USB_SUCCESS;
    *device = handle;
  }
  return errorCode;
}

void setLongWord( int value, char * bytes)
{
  bytes[0] = value & 0xff;
  bytes[1] = ((value & 0xff00) >> 8) & 0xff;
  bytes[2] = ((value & 0xff0000) >> 16) & 0xff;
  bytes[3] = ((value & 0xff000000) >> 24) & 0xff;
}

int setFreqByValue(usb_dev_handle * handle, double frequency)
{
// Windows Doc from PE0FKO:
//
// Command 0x32:
// -------------
// Set the oscillator frequency by value. The frequency is formatted in MHz
// as 11.21 bits value.
// The "automatic band pass filter selection", "smooth tune",
// "one side calibration" and the "frequency subtract multiply" are all
// done in this function. (if enabled in the firmware)
//
// Default:    None
//
// Parameters:
//     requesttype:    USB_ENDPOINT_OUT
//     request:         0x32
//     value:           0
//     index:           0
//     bytes:           pointer 32 bits integer
//     size:            4
//
// Code sample:
//     uint32_t iFreq;
//     double   dFreq;
//
//     dFreq = 30.123456; // MHz
//     iFreq = (uint32_t)( dFreq * (1UL << 21) )
//     r = usbCtrlMsgOUT(0x32, 0, 0, (char *)&iFreq, sizeof(iFreq));
//     if (r < 0) Error
//

  char   buffer[4];
  int  i2cAddress = 0x55;
  int    request = REQUEST_SET_FREQ_BY_VALUE;
  int    value = 0x700 + i2cAddress;
  int    my_index = 0;
  int    retval;
  int    err_cnt;

  err_cnt =0;
 set_again:;
  setLongWord(round(frequency * 2097152.0), buffer);  //   2097152=2^21
  retval=usb_control_msg(
         handle, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
         request,
         value,
         my_index,
         buffer,
         sizeof(buffer),
         5000);
  if (retval != 4) {
    err_cnt ++;
    if(err_cnt < MAX_USB_ERR_CNT) {
      si570_sleep(1000);          // delay 1000 microsec
      goto set_again;
    } else {
      printf("Error when setting frequency, returncode=%i\n",retval);
      printf("usb error message: %s\n", usb_strerror());
    }
  }
  return retval;
}
