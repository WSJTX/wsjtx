#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "DecoderEngine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#ifdef __linux__
#include <link.h>
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(EXIT_FAILURE); } } while (0)
extern void decoder_engine_test_fst4_signal(int, int, float, int16_t *);
extern void decoder_engine_test_fst4_weak_signal(int, float, int16_t *);
extern void decoder_engine_test_fst4_ap_signal(int16_t *);
extern void decoder_engine_test_fst4_rr73_signal(int16_t *);
extern void decoder_engine_test_fst4_workspace(void);
extern void decoder_engine_test_fst4_hash_signals(int, int16_t *);
typedef struct {
  decoder_engine_handle engine;
  decoder_attempt_request request;
  decoder_audio_view audio;
  int16_t *samples, *before;
  int count, effective_bits, ap_type, expected_bits;
  int hash_count, hash_seen;
  const char *expected_message;
} fixture;
static int long_periods, smoke;

static void observe(const decoder_observation *o, void *opaque)
{
  fixture *f=opaque;
  CHECK(o->input_id==f->request.input_id && o->analysis_id==f->request.analysis_id);
  CHECK(o->attempt_no==f->request.attempt_no && o->mode==f->request.mode);
  CHECK(o->fst4.period_seconds==f->request.fst4.period_seconds);
  CHECK(o->fst4.effective_bits==(f->expected_bits ? f->expected_bits : o->mode==DECODER_MODE_FST4 ? 91 : 66));
  f->effective_bits=o->fst4.effective_bits;
  f->ap_type=o->ap_type;
  CHECK(o->fst4.blanker_percent==0);
  CHECK(o->fst4.has_doppler==f->request.fst4.measure_doppler);
  if(o->fst4.has_doppler) CHECK(isfinite(o->fst4.fmid_hz) && isfinite(o->fst4.width_hz) && o->fst4.width_hz>=0);
  else CHECK(o->fst4.fmid_hz==0 && o->fst4.width_hz==0);
  CHECK(isfinite(o->sync) && o->sync>0 && isfinite(o->quality));
  CHECK(fabsf(o->dt_seconds)<0.3f);
  if(f->hash_count) {
    CHECK(o->fst4.has_hash22==1);
    int index=o->fst4.hash22==1234567 ? 0 : o->fst4.hash22==2345678 ? 1 : -1;
    CHECK(index>=0 && index<f->hash_count);
    CHECK(!(f->hash_seen&(1<<index)));
    f->hash_seen|=1<<index;
    CHECK(!strcmp(o->message,"<...> FN42AB"));
    CHECK(fabsf(o->frequency_hz-(f->hash_count==1 ? 1500 : index==0 ? 1450 : 1550))<5);
    ++f->count;
  } else {
    CHECK(o->fst4.has_hash22==0 && o->fst4.hash22==0);
    CHECK(fabsf(o->frequency_hz-1500)<5);
    CHECK(!strcmp(o->message,f->expected_message ? f->expected_message :
      o->mode==DECODER_MODE_FST4 ? "K1ABC W9XYZ FN42" : "K1ABC FN42 37"));
    CHECK(!f->count++);
  }
  CHECK(decoder_engine_release_input(f->engine,o->input_id)==DECODER_BUSY);
  CHECK(decoder_engine_reset_session(f->engine)==DECODER_BUSY);
  CHECK(decoder_engine_set_fst4w_calls(f->engine,NULL,0)==DECODER_BUSY);
}
static void configure(fixture *f,int mode,int period)
{
  static int64_t id=800;
  memset(&f->request,0,sizeof f->request);
  f->request.input_id=++id;
  f->request.analysis_id=id;
  f->request.attempt_no=1;
  f->request.mode=mode;
  f->request.phase=DECODER_PHASE_NORMAL;
  f->request.source=DECODER_SOURCE_FILE;
  f->request.fst4.period_seconds=period;
  f->request.fst4.receive_frequency_hz=1500;
  f->request.fst4.search_low_hz=1400;
  f->request.fst4.search_high_hz=1600;
  f->request.fst4.tolerance_hz=50;
  f->request.fst4.depth=1;
  f->request.fst4.single_decode=1;
  memset(f->request.fst4.mycall,' ',12);
  memset(f->request.fst4.hiscall,' ',12);
  f->audio.samples=f->samples;
  f->audio.sample_count=period*12000;
  f->audio.sample_rate_hz=12000;
}
static void decode(fixture *f,int expected)
{
  decoder_attempt_request saved=f->request;
  decoder_attempt_outcome outcome;
  size_t bytes=(size_t)f->audio.sample_count*sizeof *f->samples;
  memcpy(f->before,f->samples,bytes);
  f->count=0;
  f->hash_seen=0;
  CHECK(decoder_engine_decode(f->engine,&f->request,&f->audio,observe,f,&outcome)==DECODER_OK);
  CHECK(outcome.status==DECODER_OK && outcome.observation_count==f->count);
  if(expected>=0 && f->count!=expected) fprintf(stderr,
    "FST4 mode %d progress %d attempt %d expected %d observations, got %d (AP %d, message %s)\n",
    f->request.mode,f->request.fst4.qso_progress,f->request.attempt_no,expected,f->count,
    f->ap_type,f->expected_message ? f->expected_message : "ordinary fixture");
  CHECK((expected<0 || f->count==expected) && !outcome.evidence_dropped);
  if(f->hash_count) CHECK(f->hash_seen==(1<<f->hash_count)-1);
  CHECK(!memcmp(&saved,&f->request,sizeof saved));
  CHECK(!memcmp(f->before,f->samples,bytes));
}
static void release(fixture *f)
{ CHECK(decoder_engine_release_input(f->engine,f->request.input_id)==DECODER_OK); }
static void repeat(fixture *f)
{
  f->request.phase=DECODER_PHASE_REPEAT;
  ++f->request.analysis_id;
  ++f->request.attempt_no;
}
static void check_rr73_ap(fixture *f)
{
  decoder_engine_options options={DECODER_ENGINE_ABI};
  decoder_engine_test_fst4_rr73_signal(f->samples);
  f->hash_count=0;
  f->expected_message="K1ABC W9XYZ RR73";
  for(int lifecycle=0;lifecycle<3;++lifecycle) {
    if(lifecycle==1) CHECK(decoder_engine_reset_session(f->engine)==DECODER_OK);
    if(lifecycle==2) {
      CHECK(decoder_engine_destroy(f->engine)==DECODER_OK);
      CHECK(decoder_engine_create(&options,&f->engine)==DECODER_OK);
    }
    configure(f,DECODER_MODE_FST4,15);
    f->request.fst4.depth=3;
    f->request.fst4.qso_progress=1;
    memcpy(f->request.fst4.mycall,"K1ABC       ",12);
    memcpy(f->request.fst4.hiscall,"W9XYZ       ",12);
    decode(f,0);
    repeat(f);
    f->request.fst4.qso_progress=lifecycle==1 ? 4 : 3;
    decode(f,1);
    CHECK(f->ap_type==6);
    repeat(f);
    decode(f,1);
    CHECK(f->ap_type==6);
    release(f);
  }
  f->expected_message=NULL;
}
static void run_tests(void)
{
  decoder_engine_options options={DECODER_ENGINE_ABI};
  decoder_engine_capabilities caps;
  fixture f={0};
  const int capacity=120*12000;
  int16_t *short_pcm=malloc(15*12000*sizeof *short_pcm);
  f.samples=malloc((size_t)capacity*sizeof *f.samples);
  f.before=malloc((size_t)capacity*sizeof *f.before);
  CHECK(short_pcm && f.samples && f.before);
  CHECK(decoder_engine_create(&options,&f.engine)==DECODER_OK);
  CHECK(decoder_engine_get_capabilities(f.engine,&caps)==DECODER_OK);
  CHECK((caps.supported_modes&(DECODER_SUPPORT_FST4|DECODER_SUPPORT_FST4W))==(DECODER_SUPPORT_FST4|DECODER_SUPPORT_FST4W));
  decoder_engine_test_fst4_signal(DECODER_MODE_FST4,15,1500,short_pcm);
  decoder_engine_test_fst4_signal(DECODER_MODE_FST4W,120,1500,f.samples);
  configure(&f,DECODER_MODE_FST4W,120);
  decode(&f,1);
  repeat(&f);
  f.request.fst4.tolerance_hz=0;
  decode(&f,1);
  f.request.fst4.tolerance_hz=50;
  decoder_attempt_outcome invalid_outcome;
  int saved_mode=f.request.mode;
  f.request.mode=DECODER_MODE_FST4;
  CHECK(decoder_engine_decode(f.engine,&f.request,&f.audio,observe,&f,&invalid_outcome)==DECODER_INVALID);
  f.request.mode=saved_mode;
  f.request.fst4.period_seconds=60;
  CHECK(decoder_engine_decode(f.engine,&f.request,&f.audio,observe,&f,&invalid_outcome)==DECODER_INVALID);
  f.request.fst4.period_seconds=120;
  --f.audio.sample_count;
  CHECK(decoder_engine_decode(f.engine,&f.request,&f.audio,observe,&f,&invalid_outcome)==DECODER_INVALID);
  ++f.audio.sample_count;
  repeat(&f);
  decode(&f,1);
  release(&f);
  decoder_fst4w_call history={{0}},copied;
  int32_t count;
  memset(history.call_grid,' ',sizeof history.call_grid);
  memcpy(history.call_grid,"K1ABC FN42",10);
  CHECK(decoder_engine_set_fst4w_calls(f.engine,&history,1)==DECODER_OK);
  memset(&history,0,sizeof history);
  CHECK(decoder_engine_get_fst4w_calls(f.engine,NULL,0,&count)==DECODER_OK && count==1);
  CHECK(decoder_engine_get_fst4w_calls(f.engine,&copied,1,&count)==DECODER_OK);
  CHECK(!memcmp(copied.call_grid,"K1ABC FN42",10));
  CHECK(decoder_engine_set_fst4w_calls(f.engine,NULL,1)==DECODER_INVALID);
  CHECK(decoder_engine_set_fst4w_calls(f.engine,NULL,DECODER_FST4W_CALL_CAPACITY+1)==DECODER_INVALID);
  decoder_engine_test_fst4_weak_signal(120,2.0f,f.samples);
  configure(&f,DECODER_MODE_FST4W,120);
  f.request.fst4.depth=3;
  f.expected_bits=50;
  CHECK(decoder_engine_set_fst4w_calls(f.engine,NULL,0)==DECODER_OK);
  decode(&f,0);
  repeat(&f);
  CHECK(decoder_engine_set_fst4w_calls(f.engine,&copied,1)==DECODER_OK);
  decode(&f,1);
  release(&f);
  f.expected_bits=0;
  memcpy(f.samples,short_pcm,15*12000*sizeof *short_pcm);
  configure(&f,DECODER_MODE_FST4,15);
  decode(&f,1);
  CHECK(decoder_engine_get_fst4w_calls(f.engine,NULL,0,&count)==DECODER_OK && count==1);
  repeat(&f);
  f.request.fst4.search_low_hz=1499;
  f.request.fst4.search_high_hz=1501;
  f.request.fst4.tolerance_hz=1;
  decode(&f,1);
  f.request.fst4.search_low_hz=1400;
  f.request.fst4.search_high_hz=1600;
  f.request.fst4.tolerance_hz=50;
  repeat(&f);
  f.request.fst4.measure_doppler=1;
  f.request.fst4.mycall[0]='K';
  f.request.fst4.hiscall[0]='W';
  decode(&f,1);
  release(&f);
  CHECK(decoder_engine_reset_session(f.engine)==DECODER_OK);
  CHECK(decoder_engine_get_fst4w_calls(f.engine,NULL,0,&count)==DECODER_OK && count==0);
  decoder_engine_test_fst4_ap_signal(f.samples);
  configure(&f,DECODER_MODE_FST4,15);
  f.request.fst4.depth=3;
  f.request.fst4.qso_progress=1;
  memcpy(f.request.fst4.mycall,"K1ABC       ",12);
  memcpy(f.request.fst4.hiscall,"W9XYZ       ",12);
  decode(&f,1);
  fprintf(stderr,"corrupted-call fixture AP type %d\n",f.ap_type);
  CHECK(f.ap_type==2 || f.ap_type==3);
  repeat(&f);
  memcpy(f.request.fst4.mycall,"N0CALL      ",12);
  memcpy(f.request.fst4.hiscall,"K9XYZ       ",12);
  decode(&f,0);
  release(&f);
  configure(&f,DECODER_MODE_FST4,15);
  decoder_attempt_outcome outcome;
  f.request.fst4.period_seconds=INT32_MAX;
  CHECK(decoder_engine_decode(f.engine,&f.request,&f.audio,observe,&f,&outcome)==DECODER_INVALID);
  f.request.fst4.period_seconds=15;
  f.audio.sample_count=15*12000+1;
  CHECK(decoder_engine_decode(f.engine,&f.request,&f.audio,observe,&f,&outcome)==DECODER_INVALID);
  f.audio.sample_count=1;
  f.samples[0]=0;
  decode(&f,0);
  release(&f);
  CHECK(decoder_engine_destroy(f.engine)==DECODER_OK);
  CHECK(decoder_engine_create(&options,&f.engine)==DECODER_OK);
  memcpy(f.samples,short_pcm,15*12000*sizeof *short_pcm);
  configure(&f,DECODER_MODE_FST4,15);
  decode(&f,1);
  release(&f);
  configure(&f,DECODER_MODE_FST4,15);
  f.request.fst4.eme_delay_seconds=15;
  decode(&f,0);
  release(&f);
  for(int count=1;count<=2;++count) {
    decoder_engine_test_fst4_hash_signals(count,f.samples);
    configure(&f,DECODER_MODE_FST4W,15);
    f.hash_count=count;
    f.request.fst4.single_decode=0;
    f.request.fst4.tolerance_hz=100;
    f.request.fst4.blanker_mode=5;
    decode(&f,count);
    repeat(&f);
    decode(&f,count);
    release(&f);
  }
  check_rr73_ap(&f);
  CHECK(decoder_engine_destroy(f.engine)==DECODER_OK);
  free(short_pcm); free(f.samples); free(f.before);
}
static void run_smoke(void)
{
  decoder_engine_options options={DECODER_ENGINE_ABI};
  decoder_engine_capabilities caps;
  fixture f={0};
  const int capacity=120*12000;
  f.samples=malloc((size_t)capacity*sizeof *f.samples);
  f.before=malloc((size_t)capacity*sizeof *f.before);
  CHECK(f.samples && f.before);
  CHECK(decoder_engine_create(&options,&f.engine)==DECODER_OK);
  CHECK(decoder_engine_get_capabilities(f.engine,&caps)==DECODER_OK);
  CHECK((caps.supported_modes&(DECODER_SUPPORT_FST4|DECODER_SUPPORT_FST4W))==(DECODER_SUPPORT_FST4|DECODER_SUPPORT_FST4W));
  decoder_engine_test_fst4_signal(DECODER_MODE_FST4W,120,1500,f.samples);
  configure(&f,DECODER_MODE_FST4W,120);
  decode(&f,1);
  repeat(&f);
  decode(&f,1);
  release(&f);
  decoder_engine_test_fst4_signal(DECODER_MODE_FST4,15,1500,f.samples);
  configure(&f,DECODER_MODE_FST4,15);
  decode(&f,1);
  release(&f);
  decoder_engine_test_fst4_hash_signals(2,f.samples);
  configure(&f,DECODER_MODE_FST4W,15);
  f.hash_count=2;
  f.request.fst4.single_decode=0;
  f.request.fst4.tolerance_hz=100;
  f.request.fst4.blanker_mode=5;
  decode(&f,2);
  repeat(&f);
  decode(&f,2);
  release(&f);
  CHECK(decoder_engine_reset_session(f.engine)==DECODER_OK);
  CHECK(decoder_engine_destroy(f.engine)==DECODER_OK);
  free(f.samples); free(f.before);
}

static void run_long_periods(void)
{
  decoder_engine_options options={DECODER_ENGINE_ABI};
  fixture f={0};
  const int periods[]={300,900,1800,30,60};
  f.samples=malloc((size_t)1800*12000*sizeof *f.samples);
  f.before=malloc((size_t)1800*12000*sizeof *f.before);
  CHECK(f.samples && f.before);
  CHECK(decoder_engine_create(&options,&f.engine)==DECODER_OK);
  for(size_t i=0;i<sizeof periods/sizeof periods[0];++i) {
    decoder_engine_test_fst4_signal(DECODER_MODE_FST4,periods[i],1500,f.samples);
    configure(&f,DECODER_MODE_FST4,periods[i]);
    if(periods[i]==1800 || periods[i]==30) f.request.fst4.measure_doppler=1;
    decode(&f,1);
    if(periods[i]==1800) {
      repeat(&f);
      decode(&f,1);
    }
    release(&f);
  }
  CHECK(decoder_engine_destroy(f.engine)==DECODER_OK);
  free(f.samples); free(f.before);
  decoder_engine_test_fst4_workspace();
}
static void write_le(FILE *file,uint32_t value,unsigned bytes)
{ for(unsigned i=0;i<bytes;++i) CHECK(fputc((value>>(8*i))&255,file)!=EOF); }
static void write_wav(const char *path,int mode,int period)
{
  int count=period*12000;
  int16_t *pcm=malloc((size_t)count*sizeof *pcm);
  CHECK(pcm);
  decoder_engine_test_fst4_signal(mode,period,1500,pcm);
  FILE *file=fopen(path,"wb"); CHECK(file);
  CHECK(fwrite("RIFF",1,4,file)==4); write_le(file,36+count*2,4);
  CHECK(fwrite("WAVEfmt ",1,8,file)==8); write_le(file,16,4);
  write_le(file,1,2); write_le(file,1,2); write_le(file,12000,4); write_le(file,24000,4);
  write_le(file,2,2); write_le(file,16,2); CHECK(fwrite("data",1,4,file)==4);
  write_le(file,count*2,4);
  for(int i=0;i<count;++i) write_le(file,(uint16_t)pcm[i],2);
  CHECK(fclose(file)==0); free(pcm);
}
#ifdef _WIN32
static DWORD WINAPI run_on_small_stack(LPVOID unused)
#else
static void *run_on_small_stack(void *unused)
#endif
{
  (void)unused;
  if(long_periods) run_long_periods();
  else if(smoke) run_smoke();
  else run_tests();
  return 0;
}

#ifdef __linux__
static int add_tls_size(struct dl_phdr_info *info, size_t size, void *opaque)
{
  (void)size;
  size_t *stack_size = opaque;
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr) *header = &info->dlpi_phdr[i];
    if (header->p_type == PT_TLS) *stack_size += header->p_memsz + header->p_align;
  }
  return 0;
}
#endif

int main(int argc,char **argv)
{
  if(argc==5 && !strcmp(argv[1],"--write-wav")) {
    write_wav(argv[2],atoi(argv[3]),atoi(argv[4]));
    return EXIT_SUCCESS;
  }
  CHECK(argc==1 || (argc==2 && (!strcmp(argv[1],"--small-stack") || !strcmp(argv[1],"--small-stack-smoke") || !strcmp(argv[1],"--long-periods"))));
  long_periods=argc==2 && !strcmp(argv[1],"--long-periods");
  smoke=argc==2 && !strcmp(argv[1],"--small-stack-smoke");
#ifdef _WIN32
  HANDLE thread=CreateThread(NULL,1024*1024,run_on_small_stack,NULL,STACK_SIZE_PARAM_IS_A_RESERVATION,NULL);
  CHECK(thread!=NULL && WaitForSingleObject(thread,INFINITE)==WAIT_OBJECT_0);
  DWORD status; CHECK(GetExitCodeThread(thread,&status) && status==0); CHECK(CloseHandle(thread));
#else
  pthread_attr_t attributes;
  pthread_t thread;
  size_t stack_size=1024*1024;
#ifdef __linux__
  dl_iterate_phdr(add_tls_size,&stack_size);
#endif
  CHECK(pthread_attr_init(&attributes)==0);
  CHECK(pthread_attr_setstacksize(&attributes,stack_size)==0);
  CHECK(pthread_create(&thread,&attributes,run_on_small_stack,NULL)==0);
  CHECK(pthread_attr_destroy(&attributes)==0);
  CHECK(pthread_join(thread,NULL)==0);
#endif
  return EXIT_SUCCESS;
}
