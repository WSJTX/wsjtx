#ifndef COMMONS_H
#define COMMONS_H

#define NFFT 32768

#include <QString>

// This header defines the C++ interface to the Fortran datcom_ptrs_mod module.
// It replaces the legacy common block interface with explicit setter/getter functions.

extern "C" {
/*
extern struct {                     //This is "common/datcom/..." in Fortran
  float *dd;              //Raw I/Q data from Linrad
  float *ss;             //Half-symbol spectra at 0,45,90,135 deg pol
  float *savg;               //Avg spectra at 0,45,90,135 deg pol
  double fcenter;                   //Center freq from Linrad (MHz)
  int nutc;                         //UTC as integer, HHMM
  int idphi;                        //Phase correction for Y pol'n, degrees
  int mousedf;                      //User-selected DF
  int mousefqso;                    //User-selected QSO freq (kHz)
  int nagain;                       //1 ==> decode only at fQSO +/- Tol
  int ndepth;                       //How much hinted decoding to do?
  int ndiskdat;                     //1 ==> data read from *.tf2 or *.iq file
  int neme;                         //Hinted decoding tries only for EME calls
  int newdat;                       //1 ==> new data, must do long FFT
  int nfa;                          //Low decode limit (kHz)
  int nfb;                          //High decode limit (kHz)
  int nfcal;                        //Frequency correction, for calibration (Hz)
  int nfshift;                      //Shift of displayed center freq (kHz)
  int mcall3;                       //1 ==> CALL3.TXT has been modified
  int ntimeout;                     //Max for timeouts in Messages and BandMap
  int ntol;                         //+/- decoding range around fQSO (Hz)
  int nxant;                        //1 ==> add 45 deg to measured pol angle
  int map65RxLog;                   //Flags to control log files
  int nfsample;                     //Input sample rate
  int nxpol;                        //1 if using xpol antennas, 0 otherwise
  int nmode;                        //nmode = 10*m_modeQ65 + m_modeJT65
  int nfast;                        //No longer used
  int nsave;                        //Number of s3(64,63) spectra saved
  int max_drift;                    //Maximum Q65 drift: units symbol_rate/TxT
  int nhsym;                        //Number of available JT65 half-symbols
  char mycall[12];
  char mygrid[6];
  char hiscall[12];
  char hisgrid[6];
  char datetime[20];
  int junk1;                        //Used to test extent of copy to shared memory
  int junk2;
} datcom_;
*/
   
  extern float* dd;
  extern float* ss;
  extern float* savg;
  double get_fcenter();
  void get_mycall(char*);
  void get_hiscall(char*);
  void get_mygrid(char*);
  void get_hisgrid(char*);
  void get_datetime(char*);
  int get_nutc();
  int get_ndop00();
  int get_nkeep();
  int get_idphi();
  int get_mousedf();
  int get_mousefqso();
  int get_nagain();
  int get_ndepth();
  int get_ndiskdat();
  int get_neme();
  int get_newdat();
  int get_nfa();
  int get_nfb();
  int get_nfcal();
  int get_nfshift();
  int get_mcall3();
  int get_ntimeout();
  int get_ntol();
  int get_nxant();
  int get_map65RxLog();
  int get_nfsample();
  int get_nxpol();
  int get_nmode();
  int get_nsave();
  int get_max_drift();
  int get_nhsym();
  int get_junk1();
  int get_junk2();
  void set_d8_ptr(void* ptr, int dim1, int dim2);
  void set_dd_ptr(void* ptr, int dim1, int dim2);
  void set_ss_ptr(void* ptr, int dim1, int dim2, int dim3);
  void set_savg_ptr(void* ptr, int dim1, int dim2);
  void set_fcenter(double);
  void set_map65RxLog(int);
  void set_nutc(int);
  void set_nkeep(int);
  void set_ndop00(int);
  void set_idphi(int);
  void set_mousedf(int);
  void set_mousefqso(int);
  void set_nagain(int);
  void set_ndepth(int);
  void set_ndiskdat(int);
  void set_neme(int);
  void set_newdat(int);
  void set_nfa(int);
  void set_nfb(int);
  void set_nfcal(int);
  void set_nfshift(int);
  void set_mcall3(int);
  void set_ntimeout(int);
  void set_ntol(int);
  void set_nxant(int);
  void set_nfsample(int);
  void set_nxpol(int);
  void set_nmode(int);
  void set_nsave(int);
  void set_max_drift(int);
  void set_nhsym(int);
  void set_junk1(int);
  void set_junk2(int);
  void set_mycall(const char*);
  void set_hiscall(const char*);
  void set_mygrid(const char*);
  void set_hisgrid(const char*);
  void set_datetime(const char*);
  void set_quitid(unsigned long long);
   
  void run_m65_(int* pol, int* sample_rate); 
  void set_stop_m65(int val);
  void set_decoder_ready(int val);
  void set_wsjtx_dir_(const char* path, int path_len);
  void set_stdout_channel(void* buf_ptr,
                            void* hdr_ptr,
                            int   buf_size,
                            intptr_t event_handle);
}

extern QStringList allDecodes;
extern QStringList allDecodes2;
extern QString guiDate;

extern bool m_w3szUrl;
extern QString m_otherUrl;
extern bool m_spot_to_psk_reporter;
extern bool m_psk_reporter_tcpip;

// Scalar getters
inline int getNutc()            { return get_nutc(); }
inline int getNdop00()          { return get_ndop00(); }
inline int getNkeep()           { return get_nkeep(); }
inline int getIdphi()           { return get_idphi(); }
inline int getMousedf()         { return get_mousedf(); }
inline int getMousefqso()       { return get_mousefqso(); }
inline int getNagain()          { return get_nagain(); }
inline int getNdepth()          { return get_ndepth(); }
inline int getNdiskdat()        { return get_ndiskdat(); }
inline int getNeme()            { return get_neme(); }
inline int getNewdat()          { return get_newdat(); }
inline int getNfa()             { return get_nfa(); }
inline int getNfb()             { return get_nfb(); }
inline int getNfcal()           { return get_nfcal(); }
inline int getNfshift()         { return get_nfshift(); }
inline int getMcall3()          { return get_mcall3(); }
inline int getNtimeout()        { return get_ntimeout(); }
inline int getNtol()            { return get_ntol(); }
inline int getNxant()           { return get_nxant(); }
inline int getMap65RxLog()      { return get_map65RxLog(); }
inline int getNfsample()        { return get_nfsample(); }
inline int getNxpol()           { return get_nxpol(); }
inline int getNmode()           { return get_nmode(); }
inline int getNsave()           { return get_nsave(); }
inline int getMaxDrift()        { return get_max_drift(); }
inline int getNhsym()           { return get_nhsym(); }
inline int getJunk1()           { return get_junk1(); }
inline int getJunk2()           { return get_junk2(); }

inline double getFcenter()      { return get_fcenter(); }

// Character getters
inline QString getMyCall() {
    char buf[12] = {};
    get_mycall(buf);
    return QString::fromLatin1(buf, 12).trimmed();
}

inline QString getHisCall() {
    char buf[12] = {};
    get_hiscall(buf);
    return QString::fromLatin1(buf, 12).trimmed();
}

inline QString getMyGrid() {
    char buf[6] = {};
    get_mygrid(buf);
    return QString::fromLatin1(buf, 6).trimmed();
}

inline QString getHisGrid() {
    char buf[6] = {};
    get_hisgrid(buf);
    return QString::fromLatin1(buf, 6).trimmed();
}

// NOTE ABOUT DATETIME LENGTH (17 vs 20)
//
// The Fortran module npar_ptrs_mod declares `datetime` as CHARACTER(len=20)
// for historical compatibility with the old COMMON block layout. However,
// only the first 17 characters are meaningful. The decoder (m65_mod) formats
// and writes:
//
//     write(..., '( "UTC Date: ", a17 )') datetime(1:17)
//
// i.e., the timestamp is always exactly 17 characters:
//
//     YYYY-MM-DD HH:MM   = 17 chars
//
// The remaining 3 characters in the Fortran variable are unused padding.
// Therefore, the C/Fortran interface intentionally exchanges only 17 bytes
// for datetime. This matches the actual data semantics and avoids passing
// unused trailing padding through the interface.
//
// If the decoder ever changes its timestamp format, this boundary should be
// revisited, but as of now the 17-byte interface is correct and intentional.

inline QString getDatetime() {
    char buf[17] = {};
    get_datetime(buf);
    return QString::fromLatin1(buf, 17).trimmed();
}

//Scalar setters
inline void setFcenter(double val)       { set_fcenter(val); }
inline void setNutc(int val)             { set_nutc(val); }
inline void setNdop00(int val)           { set_ndop00(val); }
inline void setNkeep(int val)            { set_nkeep(val); }
inline void setIdphi(int val)            { set_idphi(val); }
inline void setMousedf(int val)          { set_mousedf(val); }
inline void setMousefqso(int val)        { set_mousefqso(val); }
inline void setNagain(int val)           { set_nagain(val); }
inline void setNdepth(int val)           { set_ndepth(val); }
inline void setNdiskdat(int val)         { set_ndiskdat(val); }
inline void setNeme(int val)             { set_neme(val); }
inline void setNewdat(int val)           { set_newdat(val); }
inline void setNfa(int val)              { set_nfa(val); }
inline void setNfb(int val)              { set_nfb(val); }
inline void setNfcal(int val)            { set_nfcal(val); }
inline void setNfshift(int val)          { set_nfshift(val); }
inline void setMcall3(int val)           { set_mcall3(val); }
inline void setNtimeout(int val)         { set_ntimeout(val); }
inline void setNtol(int val)             { set_ntol(val); }
inline void setNxant(int val)            { set_nxant(val); }
inline void setMap65RxLog(int val)       { set_map65RxLog(val); }
inline void setNfsample(int val)         { set_nfsample(val); }
inline void setNxpol(int val)            { set_nxpol(val); }
inline void setNmode(int val)            { set_nmode(val); }
inline void setNsave(int val)            { set_nsave(val); }
inline void setMaxDrift(int val)         { set_max_drift(val); }
inline void setNhsym(int val)            { set_nhsym(val); }
inline void setJunk1(int val)            { set_junk1(val); }
inline void setJunk2(int val)            { set_junk2(val); }
inline void setQuitID(unsigned long long val) { set_quitid(val); }
inline void setDecoderReady(int val) { set_decoder_ready(val); }

// Character setters
inline void setMyCall(const QString& val) {
    QByteArray arr = val.leftJustified(12, ' ', true).toLatin1();
    set_mycall(arr.constData());
}

inline void setHisCall(const QString& val) {
    QByteArray arr = val.leftJustified(12, ' ', true).toLatin1();
    set_hiscall(arr.constData());
}

inline void setMyGrid(const QString& val) {
    QByteArray arr = val.leftJustified(6, ' ', true).toLatin1();
    set_mygrid(arr.constData());
}

inline void setHisGrid(const QString& val) {
    QByteArray arr = val.leftJustified(6, ' ', true).toLatin1();
    set_hisgrid(arr.constData());
}

inline void setDatetime(const QString& val) {
    QByteArray arr = val.leftJustified(17, ' ', true).toLatin1();
    set_datetime(arr.constData());
}

#endif // COMMONS_H
