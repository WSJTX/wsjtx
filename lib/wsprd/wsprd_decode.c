/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 This file is part of program wsprd, a detector/demodulator/decoder
 for the Weak Signal Propagation Reporter (WSPR) mode.

 File name: wsprd_decode.c

 wsprd's run over a file, moved from wsprd.c, and the WSPR calls wsjt-codec
 makes, over the decoder in wspr_decoder.c.

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
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fftw3.h>
#include <errno.h>
#include <sys/stat.h>

#include "wsprd_utils.h"
#include "wspr_decoder.h"
#include "wsprd_decode.h"

struct wsprd_context {
    wspr_decoder *decoder;
};

_Static_assert((int)WSPRD_DECODE_MAX_RESULTS == (int)WSPR_DECODER_MAX_RESULTS,
               "wspr_decoder_run's result count");

void wsprd_decode_options_init(struct wsprd_decode_options *o)
{
    o->wspr_type=2;
    o->npasses=3;
    o->subtraction=1;
    o->quickmode=0;
    o->more_candidates=0;
    o->stackdecoder=0;
    o->verbose=0;
    o->maxcycles=10000;
    o->ndepth=-1;
    o->fmin=-110;
    o->fmax=110;
    o->bias=0.45;
    o->dialfreq_error=0.0;
}

struct wsprd_context *wsprd_context_new(wsprd_plan_guard plan_guard)
{
    struct wsprd_context *ctx=calloc(1,sizeof *ctx);
    if (!ctx) return NULL;
    if (wspr_decoder_create_guarded(&ctx->decoder,plan_guard) != DECODER_OK) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void wsprd_context_free(struct wsprd_context *ctx)
{
    if (!ctx) return;
    wspr_decoder_destroy(ctx->decoder);
    free(ctx);
}

void wsprd_hash_clear(struct wsprd_context *ctx)
{
    wspr_decoder_reset(ctx->decoder);
}

int wsprd_hash_load_line(struct wsprd_context *ctx, char const *line)
{
    return wspr_decoder_import_hash_line(ctx->decoder,line) == DECODER_OK ? 0 : -1;
}

int wsprd_hash_load(struct wsprd_context *ctx, char const *path)
{
    char line[80];
    FILE *fp=fopen(path,"r");
    if (!fp) return -1;
    while (fgets(line,sizeof line,fp)) wsprd_hash_load_line(ctx,line);
    fclose(fp);
    return 0;
}

static void write_hash(struct wsprd_context *ctx, FILE *fp)
{
    for (int i=0;i<WSPRD_HASH_COUNT;i++) {
        const char *call,*grid;
        if (wspr_decoder_hash_entry(ctx->decoder,i,&call,&grid)) fprintf(fp,"%5d %s %s\n",i,call,grid);
    }
}

int wsprd_hash_save(struct wsprd_context *ctx, char const *path)
{
    FILE *fp=fopen(path,"w");
    if (!fp) return -1;
    write_hash(ctx,fp);
    fclose(fp);
    return 0;
}

size_t wsprd_downsample_points(int wspr_type)
{
    if (wspr_type == 2) return 114*12000;
    if (wspr_type == 15) return 114*12000*8;
    return 0;
}

/* wsprd's command-line options as the decoder takes them (wsprd.c's main
   before the split). */
static void decoder_options(struct wsprd_decode_options const *o,
                            decoder_wspr_options *options, struct wspr_tuning *tuning)
{
    float center=o->wspr_type == 15 ? 1612.5f : 1500.0f;
    float scale=o->wspr_type == 15 ? 8.0f : 1.0f;
    decoder_wspr_options d={o->wspr_type,0,center+o->fmin/scale,center+o->fmax/scale,
                            (float)o->dialfreq_error};
    *options=d;
    wspr_decoder_default_tuning(tuning);
    tuning->maxcycles=o->maxcycles;
    tuning->quickmode=o->quickmode;
    tuning->more_candidates=o->more_candidates;
    tuning->stackdecoder=o->stackdecoder;
    tuning->subtraction=o->subtraction;
    tuning->npasses=o->npasses;
    tuning->ndepth=o->ndepth;
    tuning->verbose=o->verbose;
    tuning->bias=o->bias;
    tuning->calibration_error_hz=o->dialfreq_error;
}

static void first_pass(const float *i, const float *q, void *user)
{
    struct wsprd_decode_hooks const *hooks=user;
    hooks->after_first_pass(i,q,hooks->user);
}

/* The prepared reception's decode, as wsprd prints it. */
static int run(struct wsprd_context *ctx, struct wsprd_decode_options const *o,
               struct wsprd_decode_hooks const *hooks, double dialfreq,
               char const *date, char const *uttime, struct wsprd_decode_result *results)
{
    decoder_wspr_options options;
    struct wspr_tuning tuning;
    struct wspr_diagnostic decodes[WSPRD_DECODE_MAX_RESULTS];
    struct wspr_timing timing;
    int32_t uniques;
    decoder_options(o,&options,&tuning);
    int with_first_pass=hooks && hooks->after_first_pass;
    if (wspr_decoder_run(ctx->decoder,&options,&tuning,decodes,&uniques,&timing,
                         with_first_pass ? first_pass : NULL,
                         with_first_pass ? (void *)hooks : NULL) != DECODER_OK) {
        fprintf(stderr,"Error: WSPR decoding failed\n");
        return -1;
    }
    if (hooks && hooks->timing) {
        hooks->timing->tcandidates+=timing.candidates;
        hooks->timing->tsync0+=timing.sync0;
        hooks->timing->tsync1+=timing.sync1;
        hooks->timing->tsync2+=timing.sync2;
        hooks->timing->tfano+=timing.fano;
        hooks->timing->tosd+=timing.osd;
    }
    for (int i=0;i<uniques;i++) {
        struct wspr_diagnostic const *r=&decodes[i];
        decoder_observation const *ob=&r->observation;
        struct wsprd_decode_result *out=&results[i];
        memset(out,0,sizeof *out);
        snprintf(out->date,sizeof out->date,"%s",date);
        snprintf(out->time,sizeof out->time,"%s",uttime);
        out->sync=ob->sync;
        out->snr=ob->wspr.snr_db;
        out->dt=ob->dt_seconds;
        out->freq=dialfreq+r->audio_frequency_hz/1e6;
        snprintf(out->message,sizeof out->message,"%s",r->message);
        out->drift=r->drift;
        out->cycles=r->cycles;
        out->jitter=r->jitter;
        out->blocksize=r->blocksize;
        out->metric=r->metric;
        out->nhardmin=r->nhardmin;
        out->ipass=r->ipass;
        out->decodetype=r->decodetype;
        /* audio_frequency_hz is the float 1500 + f1 (WSPR-15: 1612.5 + f1/8 in
           double): the difference is exact, and 1500 + f1 in float gives the
           audio frequency back. */
        out->f1=o->wspr_type == 15 ? (float)((r->audio_frequency_hz-1612.5)*8.0)
                                   : (float)(r->audio_frequency_hz-1500.0);
    }
    return uniques;
}

int wsprd_decode_pcm(struct wsprd_context *ctx,
                     struct wsprd_decode_options const *o,
                     struct wsprd_decode_hooks const *hooks,
                     short const *samples, size_t nsamples,
                     double dialfreq, char const *date, char const *uttime,
                     struct wsprd_decode_result *results)
{
    static const short silence=0;
    size_t points=wsprd_downsample_points(o->wspr_type);
    int32_t status;
    if (!points) {
        fprintf(stderr,"Error: WSPR type %d\n",o->wspr_type);
        return -1;
    }
    if (nsamples > points) nsamples=points;
    /* An empty period decodes as silence. */
    if (!nsamples || !samples) {
        samples=&silence;
        nsamples=1;
    }
    clock_t t0=clock();
    status=wspr_decoder_prepare_pcm(ctx->decoder,samples,(int32_t)nsamples,12000,o->wspr_type);
    if (hooks && hooks->timing) hooks->timing->tdownsample+=(float)(clock()-t0)/CLOCKS_PER_SEC;
    if (status != DECODER_OK) {
        fprintf(stderr,"Error: WSPR downsampling failed\n");
        return -1;
    }
    return run(ctx,o,hooks,dialfreq,date,uttime,results);
}

int wsprd_decode_iq(struct wsprd_context *ctx,
                    struct wsprd_decode_options const *o,
                    struct wsprd_decode_hooks const *hooks,
                    float const *idat, float const *qdat,
                    double dialfreq, char const *date, char const *uttime,
                    struct wsprd_decode_result *results)
{
    if (wspr_decoder_prepare_iq(ctx->decoder,idat,qdat,WSPRD_C2_POINTS,o->wspr_type) != DECODER_OK) {
        fprintf(stderr,"Error: WSPR type %d\n",o->wspr_type);
        return -1;
    }
    return run(ctx,o,hooks,dialfreq,date,uttime,results);
}

unsigned long wsprd_read_c2(char const *ptr_to_infile, float *idat, float *qdat,
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

unsigned long wsprd_write_c2(char const *c2filename, int trmin, double freq,
                             float const *idat, float const *qdat)
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

int wsprd_data_dir_ok(char const *dir)
{
    struct stat st;
#ifdef _WIN32
    /* Windows' stat() fails on a trailing separator, which C:\ keeps. */
    char trimmed[1024];
    size_t n = strlen(dir);
    if( n >= sizeof trimmed ) return -1;
    memcpy(trimmed, dir, n + 1);
    while( n > 1 && (trimmed[n-1] == '\\' || trimmed[n-1] == '/') && trimmed[n-2] != ':' )
        trimmed[--n] = 0;
    dir = trimmed;
#endif
    if( stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) ) return -1;
#ifdef _WIN32
    /* Windows' access() has no X_OK. */
    return access(dir, R_OK | W_OK) ? -1 : 0;
#else
    return access(dir, R_OK | W_OK | X_OK) ? -1 : 0;
#endif
}

void wsprd_run_args_init(struct wsprd_run_args *a)
{
    wsprd_decode_options_init(&a->options);
    a->data_dir=".";
    a->infile=NULL;
    a->dialfreq_cmdline=0.0;
    a->writec2=0;
    a->usehashtable=1;
    a->wisdom=1;
    a->timer=1;
    a->plan_guard=NULL;
    a->decode_finished=1;
}

/* hashtable.txt into the table, as wsprd loads it: created when missing; a
   file that cannot be opened leaves the table as it is. */
static void load_hash_file(struct wsprd_context *ctx, char const *path)
{
    char line[80];
    FILE *fhash=fopen(path,"r+");
    if (fhash) {
        while (fgets(line,sizeof line,fhash)) wsprd_hash_load_line(ctx,line);
    } else fhash=fopen(path,"w+");
    if (fhash) fclose(fhash);
}

/* wsprd -c: the first pass's residual to 000000_0001.c2. */
static void save_first_pass(const float *i, const float *q, void *user)
{
    char filename[]="000000_0001.c2";
    printf("Writing %s\n",filename);
    wsprd_write_c2(filename,2,*(double *)user,i,q);
}

/* n bytes of name from offset at, which may be negative, as strncpy copies
   them: a byte before the name reads as NUL, and NUL fills from the first. */
static void name_field(char *out, size_t n, char const *name, ptrdiff_t at)
{
    size_t i=0;
    for (; i<n && at+(ptrdiff_t)i >= 0 && name[at+(ptrdiff_t)i]; i++) out[i]=name[at+(ptrdiff_t)i];
    for (; i<=n; i++) out[i]=0;
}

/* wsprd's main after its options, from wsprd.c. */
int wsprd_run_file(struct wsprd_run_args const *a)
{
    char const *data_dir=a->data_dir ? a->data_dir : ".";
    char const *ptr_to_infile=a->infile,*suffix=NULL;
    /* -a takes up to 500 bytes in the codec (prog_args); wsprd's buffers were
       200 bytes. */
    char wisdom_fname[1024],all_fname[1024],spots_fname[1024],timer_fname[1024],hash_fname[1024];
    char date[7],uttime[5];
    int status=EXIT_FAILURE;
    double dialfreq=0;
    float treadwav=0,ttotal=0;
    struct wsprd_decode_timing timing={0};
    struct wsprd_decode_options opt=a->options;
    struct wsprd_decode_hooks hooks={0};
    struct wsprd_decode_result decodes[WSPRD_DECODE_MAX_RESULTS];
    clock_t t00=clock(),t0;
    FILE *fp_fftwf_wisdom_file=NULL,*fall_wspr=NULL,*fwsprd=NULL,*fhash=NULL,*ftimer=NULL;
    struct wsprd_context *ctx=NULL;
    int uniques=-1;

    if (wsprd_data_dir_ok(data_dir)) {
        fprintf(stderr,"Error: inaccessible data directory: '%s'\n",data_dir);return EXIT_FAILURE;
    }
    if (!ptr_to_infile) {fprintf(stderr,"Error: no WSPR input file\n");return EXIT_FAILURE;}
    if (build_data_path(wisdom_fname,sizeof wisdom_fname,data_dir,"wspr_wisdom.dat") ||
        build_data_path(all_fname,sizeof all_fname,data_dir,"ALL_WSPR.TXT") ||
        build_data_path(spots_fname,sizeof spots_fname,data_dir,"wspr_spots.txt") ||
        build_data_path(timer_fname,sizeof timer_fname,data_dir,"wspr_timer.out") ||
        build_data_path(hash_fname,sizeof hash_fname,data_dir,"hashtable.txt")) return EXIT_FAILURE;
    if (a->wisdom && (fp_fftwf_wisdom_file=fopen(wisdom_fname,"r"))) {
        fftwf_import_wisdom_from_file(fp_fftwf_wisdom_file);fclose(fp_fftwf_wisdom_file);
    }
    if (!(ctx=wsprd_context_new(a->plan_guard))) {
        fprintf(stderr,"Error: unable to allocate wsprd decoder resources\n");return EXIT_FAILURE;
    }
    fall_wspr=fopen(all_fname,"a");
    fwsprd=fopen(spots_fname,"w");
    if (a->timer) {
        if ((ftimer=fopen(timer_fname,"r"))) {
            int nr=fscanf(ftimer,"%f %f %f %f %f %f %f %f",&treadwav,&timing.tcandidates,&timing.tsync0,
                          &timing.tsync1,&timing.tsync2,&timing.tfano,&timing.tosd,&ttotal);
            fclose(ftimer);
            if (nr==0) fprintf(stderr,"Empty timer file: '%s'\n",timer_fname);
        }
        ftimer=fopen(timer_fname,"w");
    }
    /* As wsprd did before the decoder was split from it: a side file that
       cannot be opened goes unwritten, and the decodes still print. */
    if ((suffix=strstr(ptr_to_infile,".wav"))) {
        size_t count=wsprd_downsample_points(opt.wspr_type),nr;
        FILE *fp;
        short *pcm;
        t0=clock();
        if (!count) goto cleanup;
        if (!(fp=fopen(ptr_to_infile,"rb"))) {fprintf(stderr,"Cannot open data file '%s'\n",ptr_to_infile);goto cleanup;}
        if (!(pcm=calloc(count,sizeof *pcm))) {fclose(fp);goto cleanup;}
        nr=fread(pcm,2,22,fp);
        nr=fread(pcm,2,count,fp);
        fclose(fp);
        if (!nr) {free(pcm);fprintf(stderr,"No data in file '%s'\n",ptr_to_infile);goto cleanup;}
        treadwav+=(float)(clock()-t0)/CLOCKS_PER_SEC;
        dialfreq=a->dialfreq_cmdline-opt.dialfreq_error*1.0e-06;
        name_field(date,6,ptr_to_infile,(suffix-ptr_to_infile)-11);
        name_field(uttime,4,ptr_to_infile,(suffix-ptr_to_infile)-4);
        if (a->usehashtable) load_hash_file(ctx,hash_fname);
        hooks.after_first_pass=a->writec2 ? save_first_pass : NULL;
        hooks.user=&dialfreq;
        hooks.timing=&timing;
        uniques=wsprd_decode_pcm(ctx,&opt,&hooks,pcm,count,dialfreq,date,uttime,decodes);
        treadwav+=timing.tdownsample;  /* readwavfile downsampled */
        free(pcm);
    } else if ((suffix=strstr(ptr_to_infile,".c2"))) {
        float *i=calloc(WSPRD_C2_POINTS,sizeof(float)),*q=calloc(WSPRD_C2_POINTS,sizeof(float));
        if (!i || !q) {free(i);free(q);goto cleanup;}
        if (wsprd_read_c2(ptr_to_infile,i,q,&dialfreq,&opt.wspr_type) == 1) {free(i);free(q);goto cleanup;}
        if (opt.wspr_type != 15) opt.wspr_type=2;  /* as wsprd read any other type */
        dialfreq-=opt.dialfreq_error*1.0e-06;
        name_field(date,6,ptr_to_infile,(suffix-ptr_to_infile)-11);
        name_field(uttime,4,ptr_to_infile,(suffix-ptr_to_infile)-4);
        if (a->usehashtable) load_hash_file(ctx,hash_fname);
        hooks.after_first_pass=a->writec2 ? save_first_pass : NULL;
        hooks.user=&dialfreq;
        hooks.timing=&timing;
        uniques=wsprd_decode_iq(ctx,&opt,&hooks,i,q,dialfreq,date,uttime,decodes);
        free(i);free(q);
    } else {
        printf("Error: Failed to open %s\n",ptr_to_infile);
        printf("WSPR file must have suffix .wav or .c2\n");goto cleanup;
    }
    if (uniques < 0) goto cleanup;
    for (int i=0;i<uniques;i++) {
        struct wsprd_decode_result const *r=&decodes[i];
        printf("%4s %3.0f %4.1f %10.6f %2d  %-s \n",uttime,r->snr,r->dt,r->freq,(int)r->drift,r->message);
        if (fall_wspr) fprintf(fall_wspr,
                "%6s %4s %3.0f %5.2f %11.7f  %-22s %2d %5.2f %2d %2d %4d %2d %3d %5u %5d\n",
                date,uttime,r->snr,r->dt,r->freq,r->message,(int)r->drift,r->sync,
                r->ipass+1,r->blocksize,r->jitter,r->decodetype,r->nhardmin,r->cycles/81,r->metric);
        if (fwsprd) fprintf(fwsprd,
                "%6s %4s %3d %3.0f %4.1f %10.6f  %-22s %2d %5u %4d\n",
                date,uttime,(int)(10*r->sync),r->snr,r->dt,r->freq,r->message,(int)r->drift,r->cycles/81,r->jitter);
    }
    if (a->decode_finished) printf("<DecodeFinished>\n");
    if (a->wisdom && (fp_fftwf_wisdom_file=fopen(wisdom_fname,"w"))) {
        fftwf_export_wisdom_to_file(fp_fftwf_wisdom_file);fclose(fp_fftwf_wisdom_file);
    }
    if (ftimer) {
        ttotal+=(float)(clock()-t00)/CLOCKS_PER_SEC;
        fprintf(ftimer,"%7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n\n",
                treadwav,timing.tcandidates,timing.tsync0,timing.tsync1,timing.tsync2,timing.tfano,timing.tosd,ttotal);

        fprintf(ftimer,"Code segment        Seconds   Frac\n");
        fprintf(ftimer,"-----------------------------------\n");
        fprintf(ftimer,"readwavfile        %7.2f %7.2f\n",treadwav,treadwav/ttotal);
        fprintf(ftimer,"Coarse DT f0 f1    %7.2f %7.2f\n",timing.tcandidates,
                timing.tcandidates/ttotal);
        fprintf(ftimer,"sync_and_demod(0)  %7.2f %7.2f\n",timing.tsync0,timing.tsync0/ttotal);
        fprintf(ftimer,"sync_and_demod(1)  %7.2f %7.2f\n",timing.tsync1,timing.tsync1/ttotal);
        fprintf(ftimer,"sync_and_demod(2)  %7.2f %7.2f\n",timing.tsync2,timing.tsync2/ttotal);
        fprintf(ftimer,"Stack/Fano decoder %7.2f %7.2f\n",timing.tfano,timing.tfano/ttotal);
        fprintf(ftimer,"OSD        decoder %7.2f %7.2f\n",timing.tosd,timing.tosd/ttotal);
        fprintf(ftimer,"-----------------------------------\n");
        fprintf(ftimer,"Total              %7.2f %7.2f\n",ttotal,1.0);
    }
    if (a->usehashtable && (fhash=fopen(hash_fname,"w"))) {
        write_hash(ctx,fhash);
        fclose(fhash);fhash=NULL;
    }
    status=EXIT_SUCCESS;
cleanup:
    if (fall_wspr) fclose(fall_wspr);
    if (fwsprd) fclose(fwsprd);
    if (ftimer) fclose(ftimer);
    if (fhash) fclose(fhash);
    wsprd_context_free(ctx);
    return status;
}
