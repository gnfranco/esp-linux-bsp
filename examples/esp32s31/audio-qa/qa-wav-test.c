#include <stdint.h>
#include <stdio.h>
#include "wav_util.h"
int main(void){
    int16_t mono[] = {1,-2,3,-4};
    int16_t stereo[] = {1,-2,3,-4,5,-6,7,-8};
    if(wav_writer_open("qa-mono.wav",1,44100) || wav_writer_write_int16(4,mono) || wav_writer_close()) return 1;
    if(wav_writer_open("qa-stereo.wav",2,44100) || wav_writer_write_int16(8,stereo) || wav_writer_close()) return 2;
    return 0;
}
