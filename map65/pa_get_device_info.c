#include <portaudio.h>
#include <stdio.h>

#define MAX_LATENCY 20

PaStream *in_stream;
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
