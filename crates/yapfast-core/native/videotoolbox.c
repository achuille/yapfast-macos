// H.264 file decoding through macOS. libwebrtc's macOS FFmpeg has no H.264 decoder.
#include "videotoolbox.h"
#include <VideoToolbox/VideoToolbox.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define FRAMES 18
struct yf_vt {
    VTDecompressionSessionRef session;
    CMVideoFormatDescriptionRef format;
    CVPixelBufferRef frames[FRAMES], shown;
    int64_t times[FRAMES];
    unsigned count, reorder;
    int drained;
    int error;
    pthread_mutex_t mutex;
    uint8_t *chroma;
    size_t capacity;
};
static void decoded(void *context, void *source, OSStatus status, VTDecodeInfoFlags flags,
                    CVImageBufferRef image, CMTime pts, CMTime duration) {
    (void)source; (void)flags; (void)duration;
    struct yf_vt *d=context;
    pthread_mutex_lock(&d->mutex);
    if(status) d->error=status;
    if(!status && image) {
        if(d->count==FRAMES) d->error=-1;
        else {
            unsigned index=d->count;
            while(index>0 && d->times[index-1]>pts.value) {
                d->frames[index]=d->frames[index-1]; d->times[index]=d->times[index-1]; index--;
            }
            d->frames[index]=CVPixelBufferRetain(image);
            d->times[index]=pts.value;
            d->count++;
        }
    }
    pthread_mutex_unlock(&d->mutex);
}
static void release_shown(struct yf_vt *d) {
    if(d->shown) {
        CVPixelBufferUnlockBaseAddress(d->shown,kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(d->shown); d->shown=NULL;
    }
}
void yf_vt_free(struct yf_vt *d) {
    if(!d) return;
    if(d->session) {
        VTDecompressionSessionWaitForAsynchronousFrames(d->session);
        VTDecompressionSessionInvalidate(d->session); CFRelease(d->session);
    }
    release_shown(d);
    for(unsigned i=0;i<d->count;i++) CVPixelBufferRelease(d->frames[i]);
    if(d->format) CFRelease(d->format);
    pthread_mutex_destroy(&d->mutex); free(d->chroma); free(d);
}

/* The SPS bounds how many decoded reference pictures may precede display.
 * VideoToolbox invokes callbacks in decode order, including B pictures. */
struct bits { const uint8_t *data; size_t size,at; int error; };
static unsigned bits(struct bits *b,unsigned count) {
    if(count>32 || b->at+count>b->size*8) { b->error=1; return 0; }
    unsigned value=0;
    while(count--) { value=(value<<1)|((b->data[b->at/8]>>(7-b->at%8))&1); b->at++; }
    return value;
}
static unsigned ue(struct bits *b) {
    unsigned zeros=0;
    while(!b->error && bits(b,1)==0) if(++zeros>=31) { b->error=1; return 0; }
    return ((1u<<zeros)-1)+bits(b,zeros);
}
static int se(struct bits *b) { unsigned n=ue(b); return n&1?(int)((n+1)/2):-(int)(n/2); }
static unsigned reference_frames(const uint8_t *sps,size_t size) {
    if(size<4 || size>65535) return 16;
    uint8_t *rbsp=malloc(size); if(!rbsp) return 16;
    size_t length=0; unsigned zeros=0;
    for(size_t i=1;i<size;i++) {
        if(zeros>=2 && sps[i]==3) { zeros=0; continue; }
        rbsp[length++]=sps[i]; zeros=sps[i]==0?zeros+1:0;
    }
    struct bits b={rbsp,length,0,0};
    unsigned profile=bits(&b,8); bits(&b,16); ue(&b);
    if(profile==100 || profile==110 || profile==122 || profile==244 || profile==44 ||
       profile==83 || profile==86 || profile==118 || profile==128 || profile==138 || profile==139 || profile==134 || profile==135) {
        unsigned chroma=ue(&b); if(chroma==3) bits(&b,1);
        ue(&b); ue(&b); bits(&b,1);
        if(bits(&b,1)) for(unsigned i=0;i<(chroma==3?12:8);i++) if(bits(&b,1)) {
            int last=8,next=8;
            for(unsigned j=0;j<(i<6?16:64);j++) {
                if(next) next=(last+se(&b)+256)%256;
                last=next?next:last;
            }
        }
    }
    ue(&b); unsigned order=ue(&b);
    if(order==0) ue(&b);
    else if(order==1) {
        bits(&b,1); se(&b); se(&b); unsigned cycle=ue(&b);
        if(cycle>255) b.error=1;
        else for(unsigned i=0;i<cycle;i++) se(&b);
    }
    unsigned refs=ue(&b);
    if(b.error || refs>16) refs=16;
    free(rbsp); return refs;
}

struct yf_vt *yf_vt_open(const uint8_t *avcc,size_t length) {
    if(length<7 || avcc[0]!=1) return NULL;
    int nal=(avcc[4]&3)+1;
    if(nal==3) return NULL;
    const uint8_t *sets[64]; size_t sizes[64], count=0, at=6;
    unsigned group=avcc[5]&31;
    for(int pass=0;pass<2;pass++) {
        for(unsigned i=0;i<group;i++) {
            if(at+2>length || count==64) return NULL;
            size_t size=((size_t)avcc[at]<<8)|avcc[at+1]; at+=2;
            if(!size || size>length-at) return NULL;
            sets[count]=avcc+at; sizes[count++]=size; at+=size;
        }
        if(pass==0) { if(at>=length) return NULL; group=avcc[at++]; }
    }
    if(count<2) return NULL;
    struct yf_vt *d=calloc(1,sizeof(*d)); if(!d) return NULL;
    pthread_mutex_init(&d->mutex,NULL);
    d->reorder=reference_frames(sets[0],sizes[0]);
    OSStatus err=CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL,count,sets,sizes,nal,&d->format);
    if(err) { yf_vt_free(d); return NULL; }
    CMVideoDimensions dims=CMVideoFormatDescriptionGetDimensions(d->format);
    if(dims.width<=0 || dims.height<=0 || dims.width>8192 || dims.height>8192) { yf_vt_free(d); return NULL; }
    int pixel=kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    CFNumberRef number=CFNumberCreate(NULL,kCFNumberIntType,&pixel);
    const void *keys[]={kCVPixelBufferPixelFormatTypeKey};
    const void *values[]={number};
    CFDictionaryRef attributes=CFDictionaryCreate(NULL,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    VTDecompressionOutputCallbackRecord callback={decoded,d};
    err=VTDecompressionSessionCreate(NULL,d->format,NULL,attributes,&callback,&d->session);
    CFRelease(attributes); CFRelease(number);
    if(err) { yf_vt_free(d); return NULL; }
    return d;
}
static int send_frame(struct yf_vt *d,const uint8_t *data,size_t length,int64_t pts,VTDecodeFrameFlags flags) {
    if(!data) {
        d->drained=1;
        OSStatus err=VTDecompressionSessionFinishDelayedFrames(d->session);
        if(!err) err=VTDecompressionSessionWaitForAsynchronousFrames(d->session);
        return err? -1:0;
    }
    if(!flags) d->reorder=0;
    CMBlockBufferRef block=NULL; CMSampleBufferRef sample=NULL;
    OSStatus err=CMBlockBufferCreateWithMemoryBlock(NULL,NULL,length,NULL,NULL,0,length,0,&block);
    if(!err) err=CMBlockBufferReplaceDataBytes(data,block,0,length);
    CMSampleTimingInfo timing={kCMTimeInvalid,CMTimeMake(pts,1),kCMTimeInvalid};
    if(!err) err=CMSampleBufferCreateReady(NULL,block,d->format,1,1,&timing,1,&length,&sample);
    if(!err) err=VTDecompressionSessionDecodeFrame(d->session,sample,flags,NULL,NULL);
    if(!err) err=VTDecompressionSessionWaitForAsynchronousFrames(d->session);
    if(sample) CFRelease(sample);
    if(block) CFRelease(block);
    return err? -1:0;
}
int yf_vt_receive(struct yf_vt *d,const uint8_t **planes,int *strides,int *width,int *height,int64_t *pts) {
    release_shown(d);
    pthread_mutex_lock(&d->mutex);
    if(d->error) { pthread_mutex_unlock(&d->mutex); return -1; }
    if(!d->count || (!d->drained && d->count<=d->reorder)) { pthread_mutex_unlock(&d->mutex); return 0; }
    d->shown=d->frames[0]; *pts=d->times[0]; d->count--;
    memmove(d->frames,d->frames+1,d->count*sizeof(d->frames[0]));
    memmove(d->times,d->times+1,d->count*sizeof(d->times[0]));
    pthread_mutex_unlock(&d->mutex);
    if(CVPixelBufferLockBaseAddress(d->shown,kCVPixelBufferLock_ReadOnly)) { CVPixelBufferRelease(d->shown); d->shown=NULL; return -1; }
    *width=(int)CVPixelBufferGetWidth(d->shown); *height=(int)CVPixelBufferGetHeight(d->shown);
    if(*width<=0 || *height<=0 || *width>8192 || *height>8192 || CVPixelBufferGetPlaneCount(d->shown)!=2) return -1;
    int cw=(*width+1)/2,ch=(*height+1)/2;
    size_t needed=(size_t)cw*ch*2;
    if(needed>d->capacity) { void *grown=realloc(d->chroma,needed); if(!grown) return -1; d->chroma=grown; d->capacity=needed; }
    const uint8_t *uv=CVPixelBufferGetBaseAddressOfPlane(d->shown,1);
    size_t uvstride=CVPixelBufferGetBytesPerRowOfPlane(d->shown,1);
    for(int y=0;y<ch;y++) for(int x=0;x<cw;x++) {
        d->chroma[y*cw+x]=uv[y*uvstride+2*x];
        d->chroma[cw*ch+y*cw+x]=uv[y*uvstride+2*x+1];
    }
    planes[0]=CVPixelBufferGetBaseAddressOfPlane(d->shown,0);
    planes[1]=d->chroma; planes[2]=d->chroma+cw*ch;
    strides[0]=(int)CVPixelBufferGetBytesPerRowOfPlane(d->shown,0); strides[1]=strides[2]=cw;
    return 1;
}

int yf_vt_send(struct yf_vt *d,const uint8_t *data,size_t length,int64_t pts) {
    return send_frame(d,data,length,pts,kVTDecodeFrame_EnableTemporalProcessing);
}

/* Live streams arrive as Annex B access units. Keep their parameter sets across
 * packets, and rebuild the session when the sender changes resolution. */
struct yf_vt_stream {
    struct yf_vt *decoder;
    uint8_t *sps,*pps,*packet;
    size_t sps_size,pps_size,capacity;
    int64_t frame;
};
struct yf_vt_stream *yf_vt_stream_new(void) { return calloc(1,sizeof(struct yf_vt_stream)); }
void yf_vt_stream_free(struct yf_vt_stream *s) {
    if(!s) return;
    yf_vt_free(s->decoder); free(s->sps); free(s->pps); free(s->packet); free(s);
}
static size_t start_code(const uint8_t *data,size_t size,size_t at,size_t *prefix) {
    for(;at+3<=size;at++) {
        if(data[at] || data[at+1]) continue;
        if(data[at+2]==1) { *prefix=3; return at; }
        if(at+4<=size && data[at+2]==0 && data[at+3]==1) { *prefix=4; return at; }
    }
    *prefix=0; return size;
}
static int parameter(uint8_t **dest,size_t *length,const uint8_t *data,size_t size) {
    if(!size || size>65535) return -1;
    if(*length==size && !memcmp(*dest,data,size)) return 0;
    uint8_t *copy=malloc(size); if(!copy) return -1;
    memcpy(copy,data,size); free(*dest); *dest=copy; *length=size; return 1;
}
int yf_vt_stream_decode(struct yf_vt_stream *s,const uint8_t *data,size_t size,
                       const uint8_t **planes,int *strides,int *width,int *height) {
    /* Bound compressed input as well as the decoded image and frame queue. */
    if(!size || size>32*1024*1024) return -1;
    size_t prefix,at=start_code(data,size,0,&prefix),used=0;
    if(at!=0) return -1;
    size_t needed=size+size/3+4;
    if(needed>s->capacity) {
        void *grown=realloc(s->packet,needed); if(!grown) return -1;
        s->packet=grown; s->capacity=needed;
    }
    int changed=0,has_picture=0;
    while(at<size) {
        size_t begin=at+prefix,next_prefix,next=start_code(data,size,begin,&next_prefix),end=next;
        while(end>begin && data[end-1]==0) end--;
        size_t length=end-begin;
        if(!length) return -1;
        unsigned type=data[begin]&31;
        int update=0;
        if(type==7) update=parameter(&s->sps,&s->sps_size,data+begin,length);
        if(type==8) update=parameter(&s->pps,&s->pps_size,data+begin,length);
        if(update<0) return -1;
        changed|=update;
        if(type==1 || type==5) has_picture=1;
        s->packet[used++]=(uint8_t)(length>>24); s->packet[used++]=(uint8_t)(length>>16);
        s->packet[used++]=(uint8_t)(length>>8); s->packet[used++]=(uint8_t)length;
        memcpy(s->packet+used,data+begin,length); used+=length;
        at=next; prefix=next_prefix;
    }
    if(changed) { yf_vt_free(s->decoder); s->decoder=NULL; }
    if(!has_picture) return 0;
    if(!s->decoder) {
        if(s->sps_size<4 || !s->pps_size) return 0;
        size_t length=11+s->sps_size+s->pps_size;
        uint8_t *avcc=malloc(length); if(!avcc) return -1;
        avcc[0]=1; memcpy(avcc+1,s->sps+1,3); avcc[4]=255; avcc[5]=225;
        avcc[6]=(uint8_t)(s->sps_size>>8); avcc[7]=(uint8_t)s->sps_size;
        memcpy(avcc+8,s->sps,s->sps_size);
        size_t p=8+s->sps_size; avcc[p++]=1;
        avcc[p++]=(uint8_t)(s->pps_size>>8); avcc[p++]=(uint8_t)s->pps_size;
        memcpy(avcc+p,s->pps,s->pps_size);
        s->decoder=yf_vt_open(avcc,length); free(avcc);
        if(!s->decoder) return -1;
    }
    if(send_frame(s->decoder,s->packet,used,s->frame++,0)<0) return -1;
    int64_t pts;
    return yf_vt_receive(s->decoder,planes,strides,width,height,&pts);
}
