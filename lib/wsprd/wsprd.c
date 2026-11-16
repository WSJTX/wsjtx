/*
 This file is part of program wsprd, a detector/demodulator/decoder
 for the Weak Signal Propagation Reporter (WSPR) mode.
 
 File name: wsprd.c
 
 Copyright 2001-2018, Joe Taylor, K1JT
 
 Much of the present code is based on work by Steven Franke, K9AN,
 which in turn was based on earlier work by K1JT.
 
 Copyright 2014-2018, Steven Franke, K9AN
 
 License: GNU GPL v3
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fftw3.h>
#include <errno.h>

#include "fano.h"
#include "jelinek.h"
#include "nhash.h"
#include "wsprd_utils.h"
#include "wsprsim_utils.h"
#include "wsprd_stream.h"
#include "wspr_decoder.h"

unsigned long readc2file(char *ptr_to_infile, float *idat, float *qdat,
                         double *freq, int *wspr_type)
{
    float *buffer;
    double dfreq;
    int i,ntrmin;
    char c2file[15];
    size_t nr;
    FILE* fp;
    
    fp = fopen(ptr_to_infile,"rb");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open data file '%s': %s\n", ptr_to_infile, strerror(errno));
        return 1;
    }
    if (fread(c2file,sizeof(char),14,fp) != 14 ||
        fread(&ntrmin,sizeof(int),1,fp) != 1 ||
        fread(&dfreq,sizeof(double),1,fp) != 1) { fclose(fp); return 1; }
    *freq=dfreq;
    
    buffer=calloc(2*65536,sizeof(float));
    if (!buffer) { fclose(fp); return 1; }
    nr=fread(buffer,sizeof(float),2*45000,fp);
    fclose(fp);
    
    *wspr_type=ntrmin;
    
    for(i=0; i<45000; i++) {
        idat[i]=buffer[2*i];
        qdat[i]=-buffer[2*i+1];
    }
    free(buffer);
    
    if( nr == 2*45000 ) {
        return (unsigned long) nr/2;
    } else {
        return 1;
    }
}

static unsigned long readwavfile(char *filename, int waveform, wspr_decoder *decoder)
{
    if (waveform != 2 && waveform != 15) return 1;
    size_t count=114*12000*(waveform == 15 ? 8 : 1);
    FILE *fp=fopen(filename,"rb");
    if (!fp) {fprintf(stderr,"Cannot open data file '%s'\n",filename);return 1;}
    int16_t *pcm=calloc(count,sizeof *pcm);
    if (!pcm) {fclose(fp);return 1;}
    size_t nr=fread(pcm,2,22,fp);
    nr=fread(pcm,2,count,fp);
    fclose(fp);
    if (!nr) {free(pcm);fprintf(stderr,"No data in file '%s'\n",filename);return 1;}
    int status=wspr_decoder_prepare_pcm(decoder,pcm,(int32_t)count,12000,waveform);
    free(pcm);
    return status == DECODER_OK ? 46080 : 1;
}

unsigned long writec2file(char *c2filename, int trmin, double freq
                          , const float *idat, const float *qdat)
{
    int i;
    float *buffer;
    FILE *fp;
    
    fp = fopen(c2filename,"wb");
    if( fp == NULL ) {
        fprintf(stderr, "Could not open c2 file '%s'\n", c2filename);
        return 0;
    }
    unsigned long nwrite = fwrite(c2filename,sizeof(char),14,fp);
    nwrite = fwrite(&trmin, sizeof(int), 1, fp);
    nwrite = fwrite(&freq, sizeof(double), 1, fp);
    
    buffer=calloc(2*45000,sizeof(float));
    if (!buffer) {fclose(fp);return 0;}
    for(i=0; i<45000; i++) {
        buffer[2*i]=idat[i];
        buffer[2*i+1]=-qdat[i];
    }
    nwrite = fwrite(buffer, sizeof(float), 2*45000, fp);
    free(buffer);
    fclose(fp);
    if( nwrite == 2*45000 ) {
        return nwrite;
    } else {
        return 0;
    }
}

void usage(void)
{
    printf("Usage: wsprd [options...] infile\n");
    printf("       infile must have suffix .wav or .c2\n");
    printf("\n");
    printf("Options:\n");
    printf("       -a <path> path to writeable data files, default=\".\"\n");
    printf("       -B disable block demodulation - use single-symbol noncoherent demod\n");
    printf("       -c write .c2 file at the end of the first pass\n");
    printf("       -C maximum number of decoder cycles per bit, default 10000\n");
    printf("       -d deeper search. Slower, a few more decodes\n");
    printf("       -e x (x is transceiver dial frequency error in Hz)\n");
    printf("       -f x (x is transceiver dial frequency in MHz)\n");
    printf("       -H do not use (or update) the hash table\n");
    printf("       -J use the stack decoder instead of Fano decoder\n");
    printf("       -m decode wspr-15 .wav file\n");
    printf("       -o n (0<=n<=5), decoding depth for OSD, default is disabled\n");
    printf("       -q quick mode - doesn't dig deep for weak signals\n");
    printf("       -s single pass mode, no subtraction (same as original wsprd)\n");
    printf("       -v verbose mode (shows dupes)\n");
    printf("       -w wideband mode - decode signals within +/- 150 Hz of center\n");
    printf("       -z x (x is fano metric table bias, default is 0.45)\n");
    printf("       -0 streaming mode: read PCM audio from stdin per the\n");
    printf("          streaming framing protocol; emit decodes as NDJSON\n");
    printf("          on stdout. Replaces the positional\n");
    printf("          input file argument.\n");
}

static int build_data_path(char *destination, size_t capacity,
                           const char *data_dir, const char *filename)
{
    const char *base = data_dir ? data_dir : ".";
    int written = snprintf(destination, capacity, "%s/%s", base, filename);
    if (written < 0 || (size_t)written >= capacity) {
        fprintf(stderr, "Error: data path for '%s' exceeds %zu bytes\n",
                filename, capacity - 1);
        return -1;
    }
    return 0;
}

static void save_first_pass(const float *i, const float *q, void *user)
{
    char filename[]="000000_0001.c2";
    printf("Writing %s\n",filename);
    writec2file(filename,2,*(double *)user,i,q);
}

int main(int argc, char *argv[])
{
    char *data_dir=".",*ptr_to_infile=NULL,*suffix=NULL;
    char wisdom_fname[200],all_fname[200],spots_fname[200],timer_fname[200],hash_fname[200];
    char date[7],uttime[5];
    int c,wspr_type=2,usehashtable=1,writec2=0,stream_mode=0,status=EXIT_FAILURE;
    double dialfreq_cmdline=0,dialfreq=0,dialfreq_error=0;
    float fmin=-110,fmax=110;
    float treadwav=0,tcandidates=0,tsync0=0,tsync1=0,tsync2=0,tfano=0,tosd=0,ttotal=0;
    clock_t t00=clock(),t0;
    FILE *fp_fftwf_wisdom_file=NULL,*fall_wspr=NULL,*fwsprd=NULL,*fhash=NULL,*ftimer=NULL;
    wspr_decoder *decoder=NULL;
    struct wspr_tuning tuning;
    wspr_decoder_default_tuning(&tuning);
    struct wsprd_stream_config stream_cfg={0};
    while ((c=getopt(argc,argv,"a:BcC:de:f:HJmo:qstwvz:0")) != -1) {
        switch (c) {
        case 'a': data_dir=optarg;break;
        case 'B': tuning.npasses=2;break;
        case 'c': writec2=1;break;
        case 'C': tuning.maxcycles=(unsigned int)strtoul(optarg,NULL,10);break;
        case 'd': tuning.more_candidates=1;break;
        case 'e': dialfreq_error=strtod(optarg,NULL);break;
        case 'f': dialfreq_cmdline=strtod(optarg,NULL);break;
        case 'H': usehashtable=0;break;
        case 'J': tuning.stackdecoder=1;break;
        case 'm': wspr_type=15;break;
        case 'o': tuning.ndepth=(int)strtol(optarg,NULL,10);break;
        case 'q': tuning.quickmode=1;break;
        case 's': tuning.subtraction=0;tuning.npasses=1;break;
        case 'v': tuning.verbose=1;break;
        case 'w': fmin=-150;fmax=150;break;
        case 'z': tuning.bias=strtod(optarg,NULL);break;
        case '0': stream_mode=1;break;
        case '?': usage();return 1;
        }
    }
    if (access(data_dir,R_OK|W_OK)) {
        fprintf(stderr,"Error: inaccessible data directory: '%s'\n",data_dir);usage();return EXIT_FAILURE;
    }
    if (!stream_mode && optind+1 > argc) {usage();return 1;}
    if (!stream_mode) ptr_to_infile=argv[optind];
    if (build_data_path(wisdom_fname,sizeof wisdom_fname,data_dir,"wspr_wisdom.dat") ||
        build_data_path(all_fname,sizeof all_fname,data_dir,"ALL_WSPR.TXT") ||
        build_data_path(spots_fname,sizeof spots_fname,data_dir,"wspr_spots.txt") ||
        build_data_path(timer_fname,sizeof timer_fname,data_dir,"wspr_timer.out") ||
        build_data_path(hash_fname,sizeof hash_fname,data_dir,"hashtable.txt")) return EXIT_FAILURE;
    if ((fp_fftwf_wisdom_file=fopen(wisdom_fname,"r"))) {
        fftwf_import_wisdom_from_file(fp_fftwf_wisdom_file);fclose(fp_fftwf_wisdom_file);
    }
    if (wspr_decoder_create(&decoder) != DECODER_OK) {
        fprintf(stderr,"Error: unable to allocate wsprd decoder resources\n");return EXIT_FAILURE;
    }
    fall_wspr=fopen(all_fname,"a");
    fwsprd=fopen(spots_fname,"w");
    if ((ftimer=fopen(timer_fname,"r"))) {
        int nr=fscanf(ftimer,"%f %f %f %f %f %f %f %f",&treadwav,&tcandidates,&tsync0,&tsync1,&tsync2,&tfano,&tosd,&ttotal);
        fclose(ftimer);
        if (nr==0) fprintf(stderr,"Empty timer file: '%s'\n",timer_fname);
    }
    ftimer=fopen(timer_fname,"w");
    if (!fall_wspr || !fwsprd || !ftimer) {fprintf(stderr,"Error: unable to open wsprd output files\n");goto cleanup;}
    if (stream_mode) {
        wsprd_stream_emit_ready();
        t0=clock();
        unsigned long npoints=wsprd_stream_read(&stream_cfg,decoder);
        treadwav+=(float)(clock()-t0)/CLOCKS_PER_SEC;
        if (npoints==1) goto cleanup;
        wspr_type=stream_cfg.wspr_type;
        dialfreq=stream_cfg.dialfreq-dialfreq_error*1.0e-06;
        memcpy(date,stream_cfg.date,sizeof date);memcpy(uttime,stream_cfg.uttime,sizeof uttime);
    } else if ((suffix=strstr(ptr_to_infile,".wav"))) {
        t0=clock();
        unsigned long npoints=readwavfile(ptr_to_infile,wspr_type,decoder);
        treadwav+=(float)(clock()-t0)/CLOCKS_PER_SEC;
        if (npoints==1) goto cleanup;
        dialfreq=dialfreq_cmdline-dialfreq_error*1.0e-06;
    } else if ((suffix=strstr(ptr_to_infile,".c2"))) {
        float *i=calloc(65536,sizeof(float)),*q=calloc(65536,sizeof(float));
        if (!i || !q) {free(i);free(q);goto cleanup;}
        unsigned long npoints=readc2file(ptr_to_infile,i,q,&dialfreq,&wspr_type);
        int rc=npoints == 1 ? DECODER_INVALID : wspr_decoder_prepare_iq(decoder,i,q,npoints,wspr_type);
        free(i);free(q);
        if (rc != DECODER_OK) goto cleanup;
        dialfreq-=dialfreq_error*1.0e-06;
    } else {
        printf("Error: Failed to open %s\n",ptr_to_infile);
        printf("WSPR file must have suffix .wav or .c2\n");goto cleanup;
    }
    if (!stream_mode) {
        if (suffix-ptr_to_infile < 11) {fprintf(stderr,"Error: input filename lacks reception date/time\n");goto cleanup;}
        memcpy(date,suffix-11,6);date[6]=0;
        memcpy(uttime,suffix-4,4);uttime[4]=0;
    }
    if (usehashtable) {
        char line[80];
        if ((fhash=fopen(hash_fname,"r+"))) {
            while (fgets(line,sizeof line,fhash)) wspr_decoder_import_hash_line(decoder,line);
        } else fhash=fopen(hash_fname,"w+");
        if (!fhash) {fprintf(stderr,"Error: unable to open wsprd knowledge file\n");goto cleanup;}
        fclose(fhash);fhash=NULL;
    }
    float center=wspr_type == 15 ? 1612.5f : 1500.0f;
    float scale=wspr_type == 15 ? 8.0f : 1.0f;
    decoder_wspr_options options={wspr_type,0,center+fmin/scale,center+fmax/scale,(float)dialfreq_error};
    tuning.calibration_error_hz=dialfreq_error;
    struct wspr_diagnostic decodes[50];
    struct wspr_timing timing;
    int32_t uniques;
    if (wspr_decoder_run(decoder,&options,&tuning,decodes,&uniques,&timing,
                         writec2 ? save_first_pass : NULL,&dialfreq) != DECODER_OK) {
        fprintf(stderr,"Error: WSPR decoding failed\n");goto cleanup;
    }
    tcandidates+=timing.candidates;tsync0+=timing.sync0;tsync1+=timing.sync1;
    tsync2+=timing.sync2;tfano+=timing.fano;tosd+=timing.osd;
    for (int i=0;i<uniques;i++) {
        struct wspr_diagnostic *r=&decodes[i];
        decoder_observation *ob=&r->observation;
        double freq=dialfreq+r->audio_frequency_hz/1e6;
        float snr=ob->wspr.snr_db,dt=ob->dt_seconds,sync=ob->sync;
        if (stream_mode) wsprd_stream_emit_decode(date,uttime,snr,dt,freq,(int)r->drift,r->message);
        else printf("%4s %3.0f %4.1f %10.6f %2d  %-s \n",uttime,snr,dt,freq,(int)r->drift,r->message);
        fprintf(fall_wspr,
                "%6s %4s %3.0f %5.2f %11.7f  %-22s %2d %5.2f %2d %2d %4d %2d %3d %5u %5d\n",
                date,uttime,snr,dt,freq,r->message,(int)r->drift,sync,
                r->ipass+1,r->blocksize,r->jitter,r->decodetype,r->nhardmin,r->cycles/81,r->metric);
        fprintf(fwsprd,
                "%6s %4s %3d %3.0f %4.1f %10.6f  %-22s %2d %5u %4d\n",
                date,uttime,(int)(10*sync),snr,dt,freq,r->message,(int)r->drift,r->cycles/81,r->jitter);
    }
    if (stream_mode) wsprd_stream_emit_decode_finished(date,uttime);
    else printf("<DecodeFinished>\n");
    if ((fp_fftwf_wisdom_file=fopen(wisdom_fname,"w"))) {
        fftwf_export_wisdom_to_file(fp_fftwf_wisdom_file);fclose(fp_fftwf_wisdom_file);
    }
    ttotal+=(float)(clock()-t00)/CLOCKS_PER_SEC;
    fprintf(ftimer,"%7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n\n",
            treadwav,tcandidates,tsync0,tsync1,tsync2,tfano,tosd,ttotal);
    
    fprintf(ftimer,"Code segment        Seconds   Frac\n");
    fprintf(ftimer,"-----------------------------------\n");
    fprintf(ftimer,"readwavfile        %7.2f %7.2f\n",treadwav,treadwav/ttotal);
    fprintf(ftimer,"Coarse DT f0 f1    %7.2f %7.2f\n",tcandidates,
            tcandidates/ttotal);
    fprintf(ftimer,"sync_and_demod(0)  %7.2f %7.2f\n",tsync0,tsync0/ttotal);
    fprintf(ftimer,"sync_and_demod(1)  %7.2f %7.2f\n",tsync1,tsync1/ttotal);
    fprintf(ftimer,"sync_and_demod(2)  %7.2f %7.2f\n",tsync2,tsync2/ttotal);
    fprintf(ftimer,"Stack/Fano decoder %7.2f %7.2f\n",tfano,tfano/ttotal);
    fprintf(ftimer,"OSD        decoder %7.2f %7.2f\n",tosd,tosd/ttotal);
    fprintf(ftimer,"-----------------------------------\n");
    fprintf(ftimer,"Total              %7.2f %7.2f\n",ttotal,1.0);
    
    if (usehashtable) {
        fhash=fopen(hash_fname,"w");
        if (!fhash) {fprintf(stderr,"Error: unable to save wsprd knowledge\n");goto cleanup;}
        for (int i=0;i<WSPRD_HASH_COUNT;i++) {
            const char *call,*grid;
            if (wspr_decoder_hash_entry(decoder,i,&call,&grid)) fprintf(fhash,"%5d %s %s\n",i,call,grid);
        }
        fclose(fhash);fhash=NULL;
    }
    status=EXIT_SUCCESS;
cleanup:
    if (fall_wspr) fclose(fall_wspr);
    if (fwsprd) fclose(fwsprd);
    if (ftimer) fclose(ftimer);
    if (fhash) fclose(fhash);
    wspr_decoder_destroy(decoder);
    return status;
}
