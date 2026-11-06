// AudioQueue owns conversion to the device format and its realtime thread.
// Three reusable buffers; callers sleep on a condition rather than polling.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#define N 3
struct yf_audio {
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[N];
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    unsigned busy[N], pending, size, rate_bytes;
    unsigned char *ring;
    size_t head, length, capacity;
    int input, started, error;
};
static int wait_ready(struct yf_audio *s) {
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += 3;
    return pthread_cond_timedwait(&s->ready, &s->mutex, &end);
}
static void output(void *ctx, AudioQueueRef q, AudioQueueBufferRef b) {
    (void)q;
    struct yf_audio *s = ctx;
    pthread_mutex_lock(&s->mutex);
    for (int i=0;i<N;i++) if (s->buffers[i]==b && s->busy[i]) {
        s->busy[i]=0; s->pending--; break;
    }
    pthread_cond_broadcast(&s->ready);
    pthread_mutex_unlock(&s->mutex);
}
static void input(void *ctx, AudioQueueRef q, AudioQueueBufferRef b,
                  const AudioTimeStamp *t, UInt32 n, const AudioStreamPacketDescription *p) {
    (void)t; (void)n; (void)p;
    struct yf_audio *s=ctx;
    pthread_mutex_lock(&s->mutex);
    size_t count=b->mAudioDataByteSize;
    if (count>s->capacity) count=s->capacity;
    if (s->length+count>s->capacity) {
        size_t drop=s->length+count-s->capacity;
        s->head=(s->head+drop)%s->capacity; s->length-=drop;
    }
    size_t tail=(s->head+s->length)%s->capacity;
    size_t first=count<s->capacity-tail?count:s->capacity-tail;
    memcpy(s->ring+tail,b->mAudioData,first);
    memcpy(s->ring,(char*)b->mAudioData+first,count-first);
    s->length+=count;
    OSStatus err=AudioQueueEnqueueBuffer(q,b,0,NULL);
    if (err) s->error=err;
    pthread_cond_broadcast(&s->ready);
    pthread_mutex_unlock(&s->mutex);
}
void yf_audio_close(struct yf_audio *s) {
    if (!s) return;
    if (s->queue) AudioQueueDispose(s->queue,true);
    pthread_cond_destroy(&s->ready); pthread_mutex_destroy(&s->mutex);
    free(s->ring); free(s);
}
struct yf_audio *yf_audio_open(int recording, const char *device, unsigned rate,
                              unsigned channels, int floating, unsigned frames, int *error) {
    struct yf_audio *s=calloc(1,sizeof(*s));
    if (!s) { *error=ENOMEM; return NULL; }
    pthread_mutex_init(&s->mutex,NULL); pthread_cond_init(&s->ready,NULL);
    s->input=recording;
    unsigned sample=floating?4:2;
    s->size=frames*channels*sample; s->rate_bytes=rate*channels*sample;
    AudioStreamBasicDescription format={0};
    format.mSampleRate=rate; format.mFormatID=kAudioFormatLinearPCM;
    format.mFormatFlags=kAudioFormatFlagIsPacked | (floating?kAudioFormatFlagIsFloat:kAudioFormatFlagIsSignedInteger);
    format.mBytesPerPacket=format.mBytesPerFrame=channels*sample;
    format.mFramesPerPacket=1; format.mChannelsPerFrame=channels; format.mBitsPerChannel=sample*8;
    OSStatus err=recording?AudioQueueNewInput(&format,input,s,NULL,NULL,0,&s->queue):
                          AudioQueueNewOutput(&format,output,s,NULL,NULL,0,&s->queue);
    if (err) goto fail;
    if (device && *device) {
        CFStringRef uid=CFStringCreateWithCString(NULL,device,kCFStringEncodingUTF8);
        if (!uid) { err=EINVAL; goto fail; }
        AudioDeviceID id=kAudioObjectUnknown;
        AudioValueTranslation translation={&uid,sizeof(uid),&id,sizeof(id)};
        AudioObjectPropertyAddress address={kAudioHardwarePropertyTranslateUIDToDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        UInt32 size=sizeof(translation);
        err=AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,&translation);
        if (!err && id==kAudioObjectUnknown) err=kAudioHardwareBadDeviceError;
        if (!err) err=AudioQueueSetProperty(s->queue,kAudioQueueProperty_CurrentDevice,&uid,sizeof(uid));
        CFRelease(uid); if (err) goto fail;
    }
    if (recording) {
        s->capacity=s->rate_bytes; s->ring=malloc(s->capacity);
        if (!s->ring) { err=ENOMEM; goto fail; }
    }
    for (int i=0;i<N;i++) {
        err=AudioQueueAllocateBuffer(s->queue,s->size,&s->buffers[i]);
        if (err) goto fail;
        if (recording) { err=AudioQueueEnqueueBuffer(s->queue,s->buffers[i],0,NULL); if(err) goto fail; }
    }
    if (recording) { err=AudioQueueStart(s->queue,NULL); if(err) goto fail; s->started=1; }
    *error=0; return s;
fail:
    *error=err; yf_audio_close(s); return NULL;
}
int yf_audio_write(struct yf_audio *s,const unsigned char *data,size_t len) {
    while (len) {
        pthread_mutex_lock(&s->mutex);
        int slot=-1,err=0;
        while(slot<0 && !err) {
            for(int i=0;i<N;i++) if(!s->busy[i]) { slot=i; break; }
            if(slot<0) err=wait_ready(s);
        }
        if(err) { pthread_mutex_unlock(&s->mutex); return err; }
        AudioQueueBufferRef b=s->buffers[slot];
        size_t count=len<s->size?len:s->size;
        memcpy(b->mAudioData,data,count); b->mAudioDataByteSize=(UInt32)count;
        s->busy[slot]=1; s->pending++;
        pthread_mutex_unlock(&s->mutex);
        err=AudioQueueEnqueueBuffer(s->queue,b,0,NULL);
        if(err) return err;
        if(!s->started) { err=AudioQueueStart(s->queue,NULL); if(err) return err; s->started=1; }
        data+=count; len-=count;
    }
    return 0;
}
int yf_audio_read(struct yf_audio *s,unsigned char *data,size_t len) {
    pthread_mutex_lock(&s->mutex);
    while(len) {
        while(!s->length && !s->error) { int e=wait_ready(s); if(e) { pthread_mutex_unlock(&s->mutex); return e; } }
        if(s->error) { int e=s->error; pthread_mutex_unlock(&s->mutex); return e; }
        size_t count=len<s->length?len:s->length;
        if(count>s->capacity-s->head) count=s->capacity-s->head;
        memcpy(data,s->ring+s->head,count);
        s->head=(s->head+count)%s->capacity; s->length-=count;
        data+=count; len-=count;
    }
    pthread_mutex_unlock(&s->mutex); return 0;
}
int yf_audio_drain(struct yf_audio *s) {
    pthread_mutex_lock(&s->mutex);
    while(s->pending) { int e=wait_ready(s); if(e) { pthread_mutex_unlock(&s->mutex); return e; } }
    pthread_mutex_unlock(&s->mutex); return 0;
}
void yf_audio_flush(struct yf_audio *s) {
    AudioQueueStop(s->queue,true);
    pthread_mutex_lock(&s->mutex);
    memset(s->busy,0,sizeof(s->busy)); s->pending=0; s->started=0;
    pthread_cond_broadcast(&s->ready); pthread_mutex_unlock(&s->mutex);
}
unsigned yf_audio_latency(struct yf_audio *s) {
    pthread_mutex_lock(&s->mutex);
    unsigned bytes=s->pending*s->size;
    pthread_mutex_unlock(&s->mutex);
    return (unsigned)((uint64_t)bytes*1000000/s->rate_bytes);
}
