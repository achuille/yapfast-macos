/* AAC in MP4 through AudioToolbox. One compressed packet and one reusable
 * interleaved float buffer; no playback device is opened by this decoder. */
#include "audiodecode.h"
#include <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>
#include <string.h>
struct yf_aac {
    AudioConverterRef converter;
    const uint8_t *input;
    UInt32 length,channels;
    AudioStreamPacketDescription packet;
    float *samples;
};
static OSStatus input(AudioConverterRef converter,UInt32 *count,AudioBufferList *data,
                      AudioStreamPacketDescription **packets,void *context) {
    (void)converter;
    struct yf_aac *d=context;
    if(!d->length) { *count=0; return 1; }
    *count=1;
    data->mNumberBuffers=1;
    data->mBuffers[0]=(AudioBuffer){d->channels,d->length,(void *)d->input};
    d->packet=(AudioStreamPacketDescription){0,0,d->length};
    if(packets) *packets=&d->packet;
    d->length=0;
    return noErr;
}
void yf_aac_free(struct yf_aac *d) {
    if(!d) return;
    if(d->converter) AudioConverterDispose(d->converter);
    free(d->samples); free(d);
}
void yf_aac_reset(struct yf_aac *d) { AudioConverterReset(d->converter); d->length=0; }
/* MPEG-4 descriptor lengths use four seven-bit groups. */
static void descriptor(uint8_t *out,uint8_t tag,size_t length) {
    out[0]=tag;
    for(int i=0;i<4;i++) out[i+1]=(uint8_t)((length>>(21-i*7))&127)|(i<3?128:0);
}
struct yf_aac *yf_aac_open(int rate,int channels,const uint8_t *config,size_t length) {
    if(rate<8000 || rate>192000 || channels<1 || channels>8 || length<2 || length>1024) return NULL;
    struct yf_aac *d=calloc(1,sizeof(*d)); if(!d) return NULL;
    d->channels=(UInt32)channels;
    d->samples=malloc(8192*(size_t)channels*sizeof(float));
    if(!d->samples) { yf_aac_free(d); return NULL; }
    uint8_t cookie[1055]={0};
    descriptor(cookie,3,length+26);
    descriptor(cookie+8,4,length+18);
    cookie[13]=0x40; cookie[14]=0x15;
    descriptor(cookie+26,5,length);
    memcpy(cookie+31,config,length);
    AudioStreamBasicDescription src={.mSampleRate=rate,.mFormatID=kAudioFormatMPEG4AAC,
        .mFramesPerPacket=1024,.mChannelsPerFrame=(UInt32)channels};
    UInt32 format_size=sizeof(src);
    if(AudioFormatGetProperty(kAudioFormatProperty_FormatInfo,(UInt32)(length+31),cookie,&format_size,&src)) {
        yf_aac_free(d); return NULL;
    }
    /* Keep the declared rate/layout stable; unsupported changes fail instead of
     * writing beyond the caller's channel count. */
    AudioStreamBasicDescription dst={.mSampleRate=rate,.mFormatID=kAudioFormatLinearPCM,
        .mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,
        .mBytesPerPacket=(UInt32)channels*4,.mFramesPerPacket=1,
        .mBytesPerFrame=(UInt32)channels*4,.mChannelsPerFrame=(UInt32)channels,.mBitsPerChannel=32};
    if(AudioConverterNew(&src,&dst,&d->converter) ||
       AudioConverterSetProperty(d->converter,kAudioConverterDecompressionMagicCookie,(UInt32)(length+31),cookie)) {
        yf_aac_free(d); return NULL;
    }
    return d;
}
int yf_aac_decode(struct yf_aac *d,const uint8_t *data,size_t length,const float **samples) {
    if(!data || !length || length>1024*1024) return -1;
    d->input=data; d->length=(UInt32)length;
    AudioBufferList output={.mNumberBuffers=1,.mBuffers={{d->channels,8192*d->channels*4,d->samples}}};
    UInt32 frames=8192;
    OSStatus status=AudioConverterFillComplexBuffer(d->converter,input,d,&frames,&output,NULL);
    d->input=NULL; d->length=0;
    if(status && status!=1) return -1;
    *samples=d->samples;
    return (int)frames;
}
