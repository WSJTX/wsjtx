
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

#include <libusb.h>
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
int  display_freq = -1;
int  delay;
static libusb_device_handle * global_si570usb_handle;

void si570_sleep(int us)
{
#if defined (Q_OS_WIN)
  ::Sleep (us / 1000);
#else
  ::usleep (us);
#endif
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

unsigned char Si570usbOpenDevice(libusb_device_handle **device, char *usbSerialID);
void setLongWord( int value, char * bytes);
int setFreqByValue(libusb_device_handle * handle, double frequency);
void sweepa_freq(void);
void sweepm_freq(void);

int set570(double freq_MHz)
{
//###
//  qDebug() << "A" << freq_MHz;
//  if(freq_MHz != 999.0) return 0;
//###

  char * my_usbSerialID = nullptr;

// MAIN MENU DIALOG
  if (Si570usbOpenDevice(&global_si570usb_handle, my_usbSerialID) != USB_SUCCESS)
    {
      return -1;
    }

//SET FREQUENCY
  if((freq_MHz < 3.45)|(freq_MHz > 866.0)) return -2;
  setFreqByValue(global_si570usb_handle,freq_MHz);
  return 0;
}

unsigned char Si570usbOpenDevice (libusb_device_handle * * udh, char * usbSerialID)
{
  if (*udh) return USB_SUCCESS; // only scan USB devices 1st time

  int  vendor        = USBDEV_SHARED_VENDOR;
  char *vendorName   = (char *)VENDOR_NAME;
  int  product       = USBDEV_SHARED_PRODUCT;
  char *productName  = (char *)PRODUCT_NAME;

  libusb_device_handle * handle = nullptr;
  unsigned char errorCode = USB_ERROR_NOTFOUND;
  char buffer[256];
  int rc;
  if ((rc = libusb_init (nullptr)) < 0) // init default context (safe to repeat)
    {
      printf ("usb initialization error message %s\n", libusb_error_name (rc));
      return errorCode = USB_ERROR_ACCESS;
    }

  libusb_device * * device_list;
  int device_count = libusb_get_device_list (nullptr, &device_list);
  if (device_count < 0)
    {
      puts ("no usb devices");
      errorCode = USB_ERROR_NOTFOUND;
    }
  else
    {
      for (int i = 0; i < device_count; ++i)
        {
          libusb_device * device = device_list[i];
          libusb_device_descriptor descriptor;
          if ((rc = libusb_get_device_descriptor (device, &descriptor)) < 0)
            {
              printf ("usb get device descriptor error message %s\n", libusb_error_name (rc));
              errorCode = USB_ERROR_ACCESS;
              continue;
            }
          if (vendor == descriptor.idVendor && product == descriptor.idProduct)
            {
              // now we must open the device to query strings
              if ((rc = libusb_open (device, &handle)) < 0)
                {
                  printf ("usb open device error message %s\n", libusb_error_name (rc));
                  errorCode = USB_ERROR_ACCESS;
                  continue;
                }
              if (!vendorName && !productName)
                {
                  break;            // good to go
                }
              if (libusb_get_string_descriptor_ascii (handle, descriptor.iManufacturer
                                                      , reinterpret_cast<unsigned char *> (buffer), sizeof buffer) < 0)
                {
                  printf ("usb get vendor name error message %s\n", libusb_error_name (rc));
                  errorCode = USB_ERROR_IO;
                }
              else
                {
                  if (!vendorName || !strcmp (buffer, vendorName))
                    {
                      if (libusb_get_string_descriptor_ascii (handle, descriptor.iProduct
                                                              , reinterpret_cast<unsigned char *> (buffer), sizeof buffer) < 0)
                        {
                          printf ("usb get product name error message %s\n", libusb_error_name (rc));
                          errorCode = USB_ERROR_IO;
                        }
                      else
                        {
                          if (!productName || !strcmp (buffer, productName))
                            {
                              if (libusb_get_string_descriptor_ascii (handle, descriptor.iSerialNumber
                                                                      , reinterpret_cast<unsigned char *> (buffer), sizeof buffer) < 0)
                                {
                                  printf ("usb get serial number error message %s\n", libusb_error_name (rc));
                                  errorCode = USB_ERROR_IO;
                                }
                              else
                                {
                                  if (!usbSerialID || !strcmp (buffer, usbSerialID))
                                    {
                                      break; // good to go
                                    }
                                }
                            }
                        }
                    }
                }
              libusb_close (handle);
              handle = nullptr;
            }
        }
      libusb_free_device_list (device_list, 1);
    }
  if (handle)
    {
      errorCode = USB_SUCCESS;
      *udh = handle;
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

int setFreqByValue(libusb_device_handle * handle, double frequency)
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
//     requesttype:    LIBUSB_ENDPOINT_OUT
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
  retval = libusb_control_transfer (
                                    handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
                                    request,
                                    value,
                                    my_index,
                                    reinterpret_cast<unsigned char *> (buffer),
                                    sizeof(buffer),
                                    5000);
  if (retval != 4) {
    err_cnt ++;
    if(err_cnt < MAX_USB_ERR_CNT) {
      si570_sleep(1000);          // delay 1000 microsec
      goto set_again;
    } else {
      printf("Error when setting frequency, returncode=%i\n",retval);
      printf("usb error message: %s\n", libusb_error_name (retval));
    }
  }
  return retval;
}
