#include <stdio.h>
#define MAXDEVICES 100
#include <string.h>
#include <portaudio.h>
#include <QDebug>

//------------------------------------------------------- pa_get_device_info
int pa_get_device_info (int  n,
                        void *pa_device_name,
                        void *pa_device_hostapi,
                        double *pa_device_max_speed,
                        double *pa_device_min_speed,
                        int *pa_device_max_bytes,
                        int *pa_device_min_bytes,
                        int *pa_device_max_channels,
                        int *pa_device_min_channels )
{

  (void) n ;
  (void) pa_device_name;
  (void) pa_device_hostapi;
  (void) pa_device_max_speed;
  (void) pa_device_min_speed;
  (void) pa_device_max_bytes;
  (void) pa_device_min_bytes;
  (void) pa_device_max_channels;
  (void) pa_device_min_channels;
  const PaDeviceInfo *deviceInfo;
  PaError pa_err;
  PaStreamParameters inputParameters;
  int i,j, speed_warning;
  int minBytes, maxBytes;
  double maxStandardSampleRate;
  double minStandardSampleRate;
  int minInputChannels;
  int maxInputChannels;

// negative terminated  list
  static double standardSampleRates[] = {8000.0, 9600.0,
        11025.0, 12000.0, 16000.0, 22050.0, 24000.0, 32000.0,
        44100.0, 48000.0, 88200.0, 96000.0, 192000.0, -1};
// *******************************************************


  *pa_device_max_speed=0;
  *pa_device_min_speed=0;
  *pa_device_max_bytes=0;
  *pa_device_min_bytes=0;
  *pa_device_max_channels=0;
  *pa_device_min_channels=0;
  minInputChannels=0;
  if(n >= Pa_GetDeviceCount() ) return -1;
  deviceInfo = Pa_GetDeviceInfo(n);
  if (deviceInfo->maxInputChannels==0) return -1;
  sprintf((char*)(pa_device_name),"%s",deviceInfo->name);
  sprintf((char*)(pa_device_hostapi),"%s",
          Pa_GetHostApiInfo( deviceInfo->hostApi )->name);
  speed_warning=0;

// bypass bug in Juli@ ASIO driver:
// this driver hangs after a Pa_IsFormatSupported call
  i = strncmp(deviceInfo->name, "ASIO 2.0 - ESI Juli@", 19);
  if (i == 0) {
    minStandardSampleRate=44100;
    maxStandardSampleRate=192000;
    minBytes=1;
    maxBytes=4;
    maxInputChannels= deviceInfo->maxInputChannels;
    minInputChannels= 1;
    goto end_pa_get_device_info;
  }

// Investigate device capabilities.
// Check min and max samplerates  with 16 bit data.
  maxStandardSampleRate=0;
  minStandardSampleRate=0;
  inputParameters.device = n;
  inputParameters.channelCount = deviceInfo->maxInputChannels;
  inputParameters.sampleFormat = paInt16;
  inputParameters.suggestedLatency = 0;
  inputParameters.hostApiSpecificStreamInfo = NULL;

// ************************************************************************
//filter for portaudio Windows hostapi's with non experts.
//only allow ASIO or WASAPI or WDM-KS
  i = strncmp(Pa_GetHostApiInfo(deviceInfo->hostApi)->name, "ASIO", 4);
  if (i==0 ) goto end_filter_hostapi;
  i = strncmp(Pa_GetHostApiInfo(deviceInfo->hostApi)->name,
              "Windows WASAPI", 14);
  if (i==0 ) goto end_filter_hostapi;
  i = strncmp(Pa_GetHostApiInfo(deviceInfo->hostApi)->name,
              "Windows WDM-KS", 14);
  if (i==0 ) goto end_filter_hostapi;
  speed_warning=1;
end_filter_hostapi:;

// ************************************************************************
  i=0;
  while(standardSampleRates[i] > 0 && minStandardSampleRate==0) {
    pa_err=Pa_IsFormatSupported(&inputParameters, NULL,
                                standardSampleRates[i] );
    if(pa_err == paDeviceUnavailable) return -1;
    if(pa_err == paInvalidDevice) return -1;
    if(pa_err == paFormatIsSupported ) {
      minStandardSampleRate=standardSampleRates[i];
    }
    i++;
  }
  if(minStandardSampleRate == 0) return -1;
  j=i;
  while(standardSampleRates[i] > 0 ) i++;
  i--;

  while(i >= j && maxStandardSampleRate==0) {
    pa_err=Pa_IsFormatSupported(&inputParameters, NULL,
                                  standardSampleRates[i] );
    if(pa_err == paDeviceUnavailable) return -1;
    if(pa_err == paInvalidDevice) return -1;
    if( pa_err == paFormatIsSupported ) {
      maxStandardSampleRate=standardSampleRates[i];
    }
    i--;
  }

// check if min SampleRate  = max SampleRate
  if(maxStandardSampleRate==0 && (minStandardSampleRate != 0)) {
    maxStandardSampleRate= minStandardSampleRate;
  }

// check min and max bytes
  minBytes=2;
  maxBytes=2;
  inputParameters.sampleFormat = paUInt8;
  pa_err=Pa_IsFormatSupported(&inputParameters, NULL,
                                maxStandardSampleRate );
  if( pa_err == paFormatIsSupported ) {
    minBytes=1;
  }
    inputParameters.sampleFormat = paInt32;
    pa_err=Pa_IsFormatSupported(&inputParameters, NULL,
                                maxStandardSampleRate );
  if( pa_err == paFormatIsSupported ) {
    maxBytes=4;
  }

// check min channel count
  maxInputChannels= deviceInfo->maxInputChannels;
  inputParameters.channelCount = 1;
  inputParameters.sampleFormat = paInt16;
  pa_err=paFormatIsSupported+32000;
  while(pa_err != paFormatIsSupported &&
          ( inputParameters.channelCount < (maxInputChannels+1)) ) {
    pa_err=Pa_IsFormatSupported(&inputParameters, NULL,
                                maxStandardSampleRate );
    inputParameters.channelCount++;
  }
  if( pa_err == paFormatIsSupported ) {
    minInputChannels=inputParameters.channelCount-1;
  } else {
    return -1;
  }

end_pa_get_device_info:;

  *pa_device_max_speed=maxStandardSampleRate;
  *pa_device_min_speed=minStandardSampleRate;
  *pa_device_max_bytes=maxBytes;
  *pa_device_min_bytes=minBytes;
  *pa_device_max_channels= maxInputChannels;
  *pa_device_min_channels= minInputChannels;

  return speed_warning;
}


void paInputDevice(int id, char* hostAPI_DeviceName, int* minChan,
                   int* maxChan, int* minSpeed, int* maxSpeed)
{
  int i;
  char pa_device_name[128];
  char pa_device_hostapi[128];
  double pa_device_max_speed;
  double pa_device_min_speed;
  int pa_device_max_bytes;
  int pa_device_min_bytes;
  int pa_device_max_channels;
  int pa_device_min_channels;
  char p2[256];
  char *p,*p1;
  static int iret, valid_dev_cnt;

  iret=pa_get_device_info (id,
                          &pa_device_name,
                          &pa_device_hostapi,
                          &pa_device_max_speed,
                          &pa_device_min_speed,
                          &pa_device_max_bytes,
                          &pa_device_min_bytes,
                          &pa_device_max_channels,
                          &pa_device_min_channels);

  if (iret >= 0 ) {
    valid_dev_cnt++;

    p1=(char*)"";
    p=strstr(pa_device_hostapi,"MME");
    if(p!=NULL) p1=(char*)"MME";
    p=strstr(pa_device_hostapi,"Direct");
    if(p!=NULL) p1=(char*)"DirectX";
    p=strstr(pa_device_hostapi,"WASAPI");
    if(p!=NULL) p1=(char*)"WASAPI";
    p=strstr(pa_device_hostapi,"ASIO");
    if(p!=NULL) p1=(char*)"ASIO";
    p=strstr(pa_device_hostapi,"WDM-KS");
    if(p!=NULL) p1=(char*)"WDM-KS";

    sprintf(p2,"%-8s %-39s",p1,pa_device_name);
    for(i=0; i<50; i++) {
      hostAPI_DeviceName[i]=p2[i];
      if(p2[i]==0) break;
    }
    *minChan=pa_device_min_channels;
    *maxChan=pa_device_max_channels;
    *minSpeed=(int)pa_device_min_speed;
    *maxSpeed=(int)pa_device_max_speed;
  } else {
    for(i=0; i<50; i++) {
      hostAPI_DeviceName[i]=0;
    }
    *minChan=0;
    *maxChan=0;
    *minSpeed=0;
    *maxSpeed=0;
  }
}

void getDev(int* numDevices0, char hostAPI_DeviceName[][50],
            int minChan[], int maxChan[],
            int minSpeed[], int maxSpeed[])
{
  int i,id,numDevices;
  int minch,maxch,minsp,maxsp;
  char apidev[256];

  numDevices=Pa_GetDeviceCount();
  *numDevices0=numDevices;

  for(id=0; id<numDevices; id++)  {
    paInputDevice(id,apidev,&minch,&maxch,&minsp,&maxsp);
    for(i=0; i<50; i++) {
      hostAPI_DeviceName[id][i]=apidev[i];
    }
    hostAPI_DeviceName[id][49]=0;
    minChan[id]=minch;
    maxChan[id]=maxch;
    minSpeed[id]=minsp;
    maxSpeed[id]=maxsp;
  }
}
