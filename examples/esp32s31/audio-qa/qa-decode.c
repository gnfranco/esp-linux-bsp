#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "classic/btstack_sbc_bluedroid.h"
static btstack_sbc_decoder_bluedroid_t state;
static FILE *out;
static unsigned frames, bad_format;
static void pcm(int16_t *data,int n,int channels,int rate,void *unused){
    (void)unused;
    if(channels!=2 || rate!=44100 || n<=0){bad_format++; return;}
    if(fwrite(data,sizeof(*data),n*channels,out)!=(size_t)(n*channels)) exit(3);
    frames += n;
}
int main(int argc,char **argv){
    if(argc!=3) return 2;
    FILE *in=fopen(argv[1],"rb");out=fopen(argv[2],"wb");
    if(!in || !out) return 3;
    const btstack_sbc_decoder_t *d=btstack_sbc_decoder_bluedroid_init_instance(&state);
    d->configure(&state,SBC_MODE_STANDARD,pcm,NULL);
    unsigned char b[1024]; size_t n; unsigned bytes=0;
    while((n=fread(b,1,sizeof b,in))!=0){d->decode_signed_16(&state,0,b,(int)n);bytes+=n;}
    if(ferror(in) || fclose(out)) return 3;
    fclose(in);
    printf("QA_OFFLINE bytes=%u good=%d bad=%d zero=%d pcm_frames=%u format_errors=%u\n",bytes,state.good_frames_nr,state.bad_frames_nr,state.zero_frames_nr,frames,bad_format);
    return !frames || bad_format || state.bad_frames_nr || state.zero_frames_nr;
}
