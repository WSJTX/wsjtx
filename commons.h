#ifndef COMMONS_H
#define COMMONS_H

#define NSMAX 6827
#define NTMAX 30*60
#define RX_SAMPLE_RATE 12000

#include "DecoderIpcProtocol.h"

#ifdef __cplusplus
#include <cstdbool>
#include <cstddef>
#include <QString>
#else
#include <stdbool.h>
#endif

  /*
   * This structure is shared with Fortran code, it MUST be kept in
   * sync with lib/jt9com.f90
   */
typedef struct decoder_params {
    int nutc;                   //UTC as integer, HHMM
    bool ndiskdat;              //true ==> data read from *.wav file
    int ntrperiod;              //TR period (seconds)
    int nQSOProgress;           //QSO state machine state
    int nfqso;                  //User-selected QSO freq (kHz)
    int nftx;                   //TX audio offset where replies might be expected
    bool newdat;                //true ==> new data, must do long FFT
    int npts8;                  //npts for c0() array
    int nfa;                    //Low decode limit (Hz)
    int nfSplit;                //JT65 | JT9 split frequency
    int nfb;                    //High decode limit (Hz)
    int ntol;                   //+/- decoding range around fQSO (Hz)
    int kin;
    int nzhsym;
    int nsubmode;
    bool nagain;
    int ndepth;
    bool lft8apon;
    bool lapcqonly;
    bool ljt65apon;
    int napwid;
    int ntxmode;
    int nmode;
    int minw;
    bool nclearave;
    int minSync;
    float emedelay;
    float dttol;
    int nlist;
    int listutc[10];
    int n2pass;
    int nranera;
    int naggressive;
    bool nrobust;
    int nexp_decode;
    int max_drift;
    char datetime[20];
    char mycall[12];
    char mygrid[6];
    char hiscall[12];
    char hisgrid[6];
    bool b_even_seq;
    bool b_superfox;
    int yymmdd;
	//below ft8mod
    char mybcall[12];
    char hisbcall[12];
    int ncandthin;         //=m_ncandthin; (mainwindow.cpp)
    int ndtcenter;         //=100 * ui->DTCenterSpinBox->value(); (mainwindow.cpp)
    int nft8cycles;        //=m_nFT8Cycles; (mainwindow.cpp)
    int ntrials10;         //=m_config.ntrials10(); (mainwindow.cpp)
    int ntrialsrxf10;      //=m_config.ntrialsrxf10(); ; (mainwindow.cpp)
    int nharmonicsdepth;   //=m_config.harmonicsdepth(); (mainwindow.cpp)
    int ntopfreq65;        //=m_config.ntopfreq65(); (mainwindow.cpp)
    int nprepass;          //=m_config.npreampass(); (mainwindow.cpp)
    int nsdecatt;          //=m_config.nsingdecatt(); (mainwindow.cpp)
    int nlasttx;           //=m_nlasttx; (mainwindow.cpp)
    int ndelay;            //=m_delay; (mainwindow.cpp)
    int nmt;               //=m_ft8threads; (mainwindow.cpp)
    int nft8rxfsens;       //=m_nFT8RXfSens; unless hound mode (mainwindow.cpp)
    int nft4depth;         //=m_nFT4depth; (mainwindow.cpp)
    int nsecbandchanged;   //=m_nsecBandChanged; (mainwindow.cpp)
    bool nagainfil;        //=0; (mainwindow.cpp)
    bool nstophint;        //=1;  (mainwindow.cpp)
    bool nhint;            //=m_hint ? 1 : 0;  (mainwindow.cpp)
    bool fmaskact;         //=m_config.fmaskact();  (mainwindow.cpp))
    bool lmultift8;        //=m_multithreadFT8;
    bool lft8lowth;        //if(m_ft8Sensitivity==0) dec_data.params.lft8lowth=false; (mainwindow.cpp)
    bool lft8subpass;      //if(m_ft8Sensitivity==2) dec_data.params.lft8subpass=true; (mainwindow.cpp)
    bool ltxing;           //=(m_enableTx &&  ... (mainwindow.cpp)
    bool lhideft8dupes;    //=ui->actionHide_FT8_dupe_messages->isChecked() ? 1 : 0; (mainwindow.cpp)
    bool lhound;           //=m_houndMode ? 1 : 0; (mainwindow.cpp)
    bool lcommonft8b;      //=m_commonFT8b; (mainwindow.cpp)
    bool lmycallstd;       //=m_bMyCallStd; (mainwindow.cpp)
    bool lhiscallstd;      //=m_bHisCallStd; (mainwindow.cpp)
    bool lapmyc;           //=m_lapmyc; (mainwindow.cpp)
    bool lmodechanged;     //=m_modeChanged ? 1 : 0; m_modeChanged=false; (mainwindow.cpp)
    bool lbandchanged;     //=m_bandChanged ? 1 : 0; m_bandChanged=false; (mainwindow.cpp)
    bool lenabledxcsearch; //if(!hisCall.isEmpty() && !m_enableTx && hisCall != m_lastloggedcall) dec_data.params.lenabledxcsearch=true; (mainwindow.cpp)
    bool lwidedxcsearch;   //=m_FT8WideDxCallSearch ? 1 : 0;  (mainwindow.cpp)
    bool lmultinst;        //=m_multInst ? 1 : 0; (mainwindow.cpp)
    bool lskiptx1;         //=m_skipTx1 ? 1 : 0; (mainwindow.cpp)
    int ndecoderstart;      //=m_FT8DecoderStart; (mainwindow.cpp)
} decoder_params_t;

typedef struct dec_data {
  float ss[184*NSMAX];
  float savg[NSMAX];
  float sred[5760];
  short int d2[NTMAX*RX_SAMPLE_RATE];
  decoder_params_t params;
} dec_data_t;

typedef struct shared_dec_data {
  decoder_ipc_control_t control;
  dec_data_t payload;
} shared_dec_data_t;

#ifdef __cplusplus
static_assert (sizeof (int) == 4, "decoder IPC requires 32-bit C integers");
static_assert (sizeof (decoder_ipc_control_t) == 12,
               "decoder IPC control header must remain 12 bytes");
static_assert (offsetof (decoder_ipc_control_t, state) == 4,
               "decoder IPC state must remain at the legacy shutdown offset");
static_assert (offsetof (shared_dec_data_t, payload) == 12,
               "decoder payload must immediately follow the control header");
static_assert (sizeof (shared_dec_data_t) ==
               sizeof (decoder_ipc_control_t) + sizeof (dec_data_t),
               "decoder shared-memory layout must not contain trailing padding");
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern struct {
  float syellow[NSMAX];
  float ref[3457];
  float filter[3457];
} spectra_;

extern struct {
  int   nclearave;
  int   nsum;
  float blue[4096];
  float red[4096];
} echocom_;

extern struct {
  float wave[(160+2)*134400*4]; /* (nsym+2)*nsps scaled up to 48kHz */
  int   nslots;
  int   nfreq;
  int   i3bit[5];
  char  cmsg[5][40];
  char  mycall[12];
  char  textMsg[26];
  bool  bMoreCQs;
  bool  bSendMsg;
} foxcom_;

#ifdef __cplusplus
}
#endif

#endif // COMMONS_H
