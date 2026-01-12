#include <stdio.h>
#include <conio.h>
#include "portaudio.h"
#include <string.h>
#include <time.h>

// Prototypes for Fortran subroutine update
void update_(double* total_time, int* ic1, int* ic2);

int iaa;
int icc;
int n2send=0;
int ndebug=0;
double total_time=0.0;

//  Definition of structure pointing to the audio data
typedef struct
{
  int    *iwrite;
  int    *itx;
  int    *TxOK;
  int    *Transmitting;
  int    *nwave;
  int    *nright;
  int     nring;
  int     nfs;
  short  *y1;
  short  *y2;
  short  *iwave;
} paTestData;

//  Input callback routine:
static int
SoundIn( void *inputBuffer, void *outputBuffer,
		       unsigned long framesPerBuffer,
		       const PaStreamCallbackTimeInfo* timeInfo, 
		       PaStreamCallbackFlags statusFlags,
		       void *userData )
{
  paTestData *data = (paTestData*)userData;
  short *in = (short*)inputBuffer;
  unsigned int i;
  static int ia=0;

// Don't save audio input samples when we're transmitting
  if(*data->Transmitting) return 0;

  if(outputBuffer == timeInfo) i=0; //Suppress 'unused' warnings
  
  if(statusFlags!=0) printf("Status flags %d\n",(int)statusFlags);

  if((statusFlags&1) == 0) {
    //increment buffer pointers only if data available
    ia=*data->iwrite;
    if(*data->nright==0) {                 //Use left channel for input
      for(i=0; i<framesPerBuffer; i++) {
	data->y1[ia] = (*in++);
	data->y2[ia] = (*in++);
	ia++;
	if(ia >= data->nring) ia=0;          //Wrap buffer pointer if necessary
      }
    } else {                               //Use right channel
      for(i=0; i<framesPerBuffer; i++) {
	data->y2[ia] = (*in++);
	data->y1[ia] = (*in++);
	ia++;
	if(ia >= data->nring) ia=0;          //Wrap buffer pointer if necessary
      }
    }
  }

  *data->iwrite = ia;                  //Save buffer pointer
  iaa=ia;
  total_time += (double)framesPerBuffer/12000.0;
  return 0;
}

//  Output callback routine:
static int
SoundOut( void *inputBuffer, void *outputBuffer,
		       unsigned long framesPerBuffer,
		       const PaStreamCallbackTimeInfo* timeInfo, 
		       PaStreamCallbackFlags statusFlags,
		       void *userData )
{
  paTestData *data = (paTestData*)userData;
  short *wptr = (short*)outputBuffer;
  unsigned int i;
  static short int n2;
  static int ic=0;
  static int TxOKz=0;
  static int nsent=0;

  if(inputBuffer == timeInfo) i=0; //Suppress 'unused' warnings
  if(statusFlags!=0) printf("Status flags %d\n",(int)statusFlags);

  //  if(ndebug>0) printf("SoundOut: %d  %d  %d\n",TxOKz,*data->TxOK,(int)framesPerBuffer);

  if(*data->TxOK && (!TxOKz)) ic=0;   //Reset buffer pointer to start Tx
  *data->Transmitting=*data->TxOK;    //Set the "transmitting" flag

  if(*data->TxOK)  {
    if(!TxOKz) {
      // Start of a transmission
      nsent=0;
      if(ndebug>0) printf("Start Tx %d  %d\n",TxOKz,*data->TxOK);
    }
    TxOKz=*data->TxOK;
    for(i=0 ; i < framesPerBuffer; i++ )  {
      n2=data->iwave[ic];
      //      addnoise_(&n2);
      *wptr++ = n2;                   //left
      *wptr++ = n2;                   //right
      ic++;

      if(ic > n2send) {
	*data->TxOK = 0;
	*data->Transmitting = 0;
	*data->iwrite = 0;            //Reset Rx buffer pointer to 0
	TxOKz=0;  //### ??? ###
	ic=0;
	if(ndebug>0) {
	  printf("TxT = %7.3f  nSent = %d  Frames = %7.3f\n",nsent/12000.0,
		 nsent,nsent/(53.0*384.0));
	}
	break;
      }
    }
    nsent += framesPerBuffer;
  } else {
    memset((void*)outputBuffer, 0, 2*sizeof(short)*framesPerBuffer);
  }
  *data->itx = icc;                    //Save buffer pointer
  icc=ic;
  return 0;
}

/*******************************************************************/
int jttyaudio_(int *ndevin, int *ndevout, int *npabuf, int *nright, 
	      short y1[], short y2[], int *nring, int *iwrite, 
	      int *itx, short iwave[], int *nwave, int *nfsample, 
	       int *TxOK, int *Transmitting, int *ngo, int *ndebug0)

{
  paTestData data;
  PaStream *instream, *outstream;
  PaStreamParameters inputParameters, outputParameters;
  //  PaStreamInfo *streamInfo;

  int nfpb = *npabuf;
  int nSampleRate = *nfsample;
  int ndevice_in = *ndevin;
  int ndevice_out = *ndevout;
  double dSampleRate = (double) *nfsample;
  ndebug = *ndebug0;
  PaError err_init, err_open_in, err_open_out, err_start_in, err_start_out;
  PaError err = 0;

  data.iwrite = iwrite;
  data.itx = itx;
  data.TxOK = TxOK;
  data.Transmitting = Transmitting;
  data.y1 = y1;
  data.y2 = y2;
  data.nring = *nring;
  data.nright = nright;
  data.nwave = nwave;
  data.iwave = iwave;
  data.nfs = nSampleRate;

  err_init = Pa_Initialize();                      // Initialize PortAudio

  if(err_init) {
    printf("Error initializing PortAudio.\n");
    printf("\tErrortext: %s\n\tNumber: %d\n",Pa_GetErrorText(err_init),
	   err_init);
    Pa_Terminate();  // I don't think we need this but...
    return(-1);
  }

  //  printf("Opening device %d for input, %d for output...\n",
  //         ndevice_in,ndevice_out);

  inputParameters.device = ndevice_in;
  inputParameters.channelCount = 2;
  inputParameters.sampleFormat = paInt16;
  inputParameters.suggestedLatency = 0.1;
  inputParameters.hostApiSpecificStreamInfo = NULL;

// Test if this configuration actually works, so we do not run into an
// ugly assertion
  err_open_in = Pa_IsFormatSupported(&inputParameters, NULL, dSampleRate);

  if (err_open_in == 0) {
    err_open_in = Pa_OpenStream(
		       &instream,              //address of stream
		       &inputParameters,
		       NULL,
		       dSampleRate,            //Sample rate
		       nfpb,                   //Frames per buffer
		       paNoFlag,
		       (PaStreamCallback *)SoundIn,  //Callback routine
		       (void *)&data);  //address of data structure

    if(err_open_in) {   // We should have no error here usually
      printf("Error opening input audio stream:\n");
      printf("\tErrortext: %s\n\tNumber: %d\n",Pa_GetErrorText(err_open_in),
	     err_open_in);
      err = 1;
    } else {
      //      printf("Successfully opened audio input.\n");
    }
  } else {
    printf("Error opening input audio stream.\n");
    printf("\tErrortext: %s\n\tNumber: %d\n",Pa_GetErrorText(err_open_in),
	   err_open_in);
    err = 1;
  }

  outputParameters.device = ndevice_out;
  outputParameters.channelCount = 2;
  outputParameters.sampleFormat = paInt16;
  outputParameters.suggestedLatency = 0.2;
  outputParameters.hostApiSpecificStreamInfo = NULL;

// Test if this configuration actually works, so we do not run into an
// ugly assertion.
  err_open_out = Pa_IsFormatSupported(NULL, &outputParameters, dSampleRate);

  if (err_open_out == 0) {
    err_open_out = Pa_OpenStream(
		       &outstream,             //address of stream
		       NULL,
		       &outputParameters,
		       dSampleRate,            //Sample rate
		       nfpb,                   //Frames per buffer
		       paNoFlag,
		       (PaStreamCallback *)SoundOut,  //Callback routine
		       (void *)&data);         //address of data structure

    if(err_open_out) {     // We should have no error here usually
      printf("Error opening output audio stream!\n");
      printf("\tErrortext: %s\n\tNumber: %d\n",Pa_GetErrorText(err_open_out),
	     err_open_out);
      err += 2;
    } else {
      //      printf("Successfully opened audio output.\n");
    }
  } else {
    printf("Error opening output audio stream.\n");
    printf("\tErrortext: %s\n\tNumber: %d\n",Pa_GetErrorText(err_open_out),
	   err_open_out);
    err += 2;
  }

  // if there was no error in opening both streams start them
  if (err == 0) {
    err_start_in = Pa_StartStream(instream);             //Start input stream
    if(err_start_in) {
      printf("Error starting input audio stream!\n");
      printf("\tErrortext: %s\n\tNumber: %d\n",Pa_GetErrorText(err_start_in),
	     err_start_in);
      err += 4;
    }

    err_start_out = Pa_StartStream(outstream);          //Start output stream
    if(err_start_out) {
      printf("Error starting output audio stream!\n");
      printf("\tErrortext: %s\n\tNumber: %d\n",Pa_GetErrorText(err_start_out),
	     err_start_out);
      err += 8;
    } 
  }

  if (err != 0) printf("Error starting audio input or output.\n");

  while( Pa_IsStreamActive(instream) && (*ngo != 0) && (err == 0) )  {
    int ic1=0;
    int ic2=0;

    if(_kbhit()) ic1 = _getch();
    if(_kbhit()) ic2 = _getch();

    //    if(ic1!=0 || ic2!=0) printf("%d   %d   %d\n",iaa,ic1,ic2);
    //    if(ic1!=0 && ic2==0) putchar(ic1);
    
    update_(&total_time,&ic1,&ic2);
    Pa_Sleep(10);
  }

  Pa_AbortStream(instream);              // Abort input stream
  Pa_CloseStream(instream);              // Close input stream, we're done.
  Pa_AbortStream(outstream);             // Abort output stream
  Pa_CloseStream(outstream);             // Close output stream, we're done.

  Pa_Terminate();                        // Terminate PortAudio

  return(err);
}


int padevsub_(int *idevin, int *idevout)
{
  int numdev,ndefin,ndefout;
  int nchin[101], nchout[101];
  int      i, devIdx;
  int      numDevices;
  const PaDeviceInfo *pdi;
  PaError  err;

  Pa_Initialize();
  numDevices = Pa_GetDeviceCount();
  numdev = numDevices;

  if( numDevices < 0 )  {
    err = numDevices;
    Pa_Terminate();
    return err;
  }

  if ((devIdx = Pa_GetDefaultInputDevice()) > 0) {
    ndefin = devIdx;
  } else {
    ndefin = 0;
  }

  if ((devIdx = Pa_GetDefaultOutputDevice()) > 0) {
    ndefout = devIdx;
  } else {
    ndefout = 0;
  }

  if(*idevin < 0) {
    printf("\nAudio     Input    Output     Device Name\n");
    printf("Device  Channels  Channels\n");
    printf("------------------------------------------------------------------\n");

    for( i=0; i < numDevices; i++ )  {
      pdi = Pa_GetDeviceInfo(i);
//    if(i == Pa_GetDefaultInputDevice()) ndefin = i;
//    if(i == Pa_GetDefaultOutputDevice()) ndefout = i;
      nchin[i]=pdi->maxInputChannels;
      nchout[i]=pdi->maxOutputChannels;
      printf("  %2d       %2d        %2d       %s\n",i,nchin[i],nchout[i],
	     pdi->name);
    }
    printf("\nDefault devices:          Input = %2d   Output = %2d\n",
  	 ndefin,ndefout);
  }

  //  printf("\nUser requested devices:   Input = %2d   Output = %2d\n",
  //  	 *idevin,*idevout);
  if((*idevin<0) || (*idevin>=numdev)) *idevin=ndefin;
  if((*idevout<0) || (*idevout>=numdev)) *idevout=ndefout;
  if((*idevin==0) && (*idevout==0))  {
    *idevin=ndefin;
    *idevout=ndefout;
  }
  //  printf("Will open devices:        Input = %2d   Output = %2d\n",
  //  	 *idevin,*idevout);

  Pa_Terminate();

  return 0;
}

void set_tx_length_(int* nwave)
{
  n2send=*nwave;
  //  printf("n2send = %d\n",n2send);
}

void putchar_(int *n0)
{
  int n = *n0;
  if(n >= 97 && n <= 122) n=n-32;
  putchar(n);
}
