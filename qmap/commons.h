#ifndef COMMONS_H
#define COMMONS_H

#define NFFT 32768

extern "C" {

extern struct {                     //This is "common/datcom/..." in Fortran
  float d4[2*5760000];              //Raw I/Q data from Linrad
  float ss[400*NFFT];               //Half-symbol spectra at 0,45,90,135 deg pol
  float savg[NFFT];                 //Avg spectra at 0,45,90,135 deg pol
  double fcenter;                   //Center freq from Linrad (MHz)
  int nutc;                         //UTC as integer, HHMM
  float fselected;                  //Selected frequency for nagain decodes
  int mousedf;                      //User-selected DF
  int mousefqso;                    //User-selected QSO freq (kHz)
  int nagain;                       //1 ==> decode only at fQSO +/- Tol
  int ndepth;                       //How much hinted decoding to do?
  int ndiskdat;                     //1 ==> data read from *.iq file
  int ntx60;                        //Number of seconds transmitted in Q65-60x
  int newdat;                       //1 ==> new data, must do long FFT
  int nfa;                          //Low decode limit (kHz)
  int nfb;                          //High decode limit (kHz)
  int nfcal;                        //Frequency correction, for calibration (Hz)
  int nfshift;                      //Shift of displayed center freq (kHz)
  int ntx30a;                       //Number of seconds transmitted in first half minute , Q65-30x
  int ntx30b;                       //Number of seconds transmitted in second half minute, Q65-30x
  int ntol;                         //+/- decoding range around fQSO (Hz)
  int n60;                          //nsecs%60
  int junk4;                        //
  int nfsample;                     //Input sample rate
  int ndop58;                       //EME Self Doppler at t=58
  int nBaseSubmode;                 //Base submode for Q65-60x (aka m_modeQ65)
  int ndop00;                       //EME Self Doppler at t=0
  int nsave;                        //0=None, 1=SaveDecoded, 2=SaveAll
  int max_drift;                    //Maximum Q65 drift: units symbol_rate/TxT
  int offset;                       //Offset in Hz
  int nhsym;                        //Number of available JT65 half-symbols
  char mycall[12];
  char mygrid[6];
  char hiscall[12];
  char hisgrid[6];
  char datetime[20];
  int junk1;                        //Used to test extent of copy to shared memory
  int junk2;
  bool bAlso30;                     //Process for 30-second submode as well as 60-second
} datcom_;

extern struct {                     //This is "common/datcom/..." in Fortran
  float d4[2*5760000];              //Raw I/Q data from Linrad
  float ss[400*NFFT];               //Half-symbol spectra at 0,45,90,135 deg pol
  float savg[NFFT];                 //Avg spectra at 0,45,90,135 deg pol
  double fcenter;                   //Center freq from Linrad (MHz)
  int nutc;                         //UTC as integer, HHMM
  float fselected;                  //Selected frequency for nagain decodes
  int mousedf;                      //User-selected DF
  int mousefqso;                    //User-selected QSO freq (kHz)
  int nagain;                       //1 ==> decode only at fQSO +/- Tol
  int ndepth;                       //How much hinted decoding to do?
  int ndiskdat;                     //1 ==> data read from *.iq file
  int ntx60;                        //Number of seconds transmitted in Q65-60x
  int newdat;                       //1 ==> new data, must do long FFT
  int nfa;                          //Low decode limit (kHz)
  int nfb;                          //High decode limit (kHz)
  int nfcal;                        //Frequency correction, for calibration (Hz)
  int nfshift;                      //Shift of displayed center freq (kHz)
  int ntx30a;                       //Number of seconds transmitted in first half minute , Q65-30x
  int ntx30b;                       //Number of seconds transmitted in second half minute, Q65-30x
  int ntol;                         //+/- decoding range around fQSO (Hz)
  int n60;                          //nsecs%60
  int junk4;                        //
  int nfsample;                     //Input sample rate
  int ndop58;                       //EME Self Doppler at t=58
  int nBaseSubmode;                 //Base submode for Q65-60x (aka m_modeQ65)
  int ndop00;                       //EME Self Doppler at t=0
  int nsave;                        //0=None, 1=SaveDecoded, 2=SaveAll
  int max_drift;                    //Maximum Q65 drift: units symbol_rate/TxT
  int offset;                       //Offset in Hz
  int nhsym;                        //Number of available JT65 half-symbols
  char mycall[12];
  char mygrid[6];
  char hiscall[12];
  char hisgrid[6];
  char datetime[20];
  int junk1;                        //Used to test extent of copy to shared memory
  int junk2;
  bool bAlso30;                     //Process for 30-second submode as well as 60-second
} datcom2_;

extern struct {
  int ndecodes;          //These are flags for inter-process communication
  int ncand;             //between QMAP and WSJT-X
  int nQDecoderDone;     //1 for real-time decodes, 2 for data from disk
  int nWDecoderBusy;     //Set to 1 when WSJT-X decoder is busy
  int nWTransmitting;    //Set to TRperiod when WSJT-X is transmitting
  int kHzRequested;      //Integer kHz dial frequency request to WSJT-X
  char result[50][64];   //Staging area for QMAP decodes
} decodes_;

extern struct {
  char revision[22];
  char saveFileName[120];
} savecom_;

}

#endif // COMMONS_H
