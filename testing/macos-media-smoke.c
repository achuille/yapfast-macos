// Native boundary checks, including malformed packet lengths, under sanitizers.
#include "../crates/yapfast-core/native/videotoolbox.h"
#include "../crates/yapfast-core/native/audiodecode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    FILE *file=fopen("testing/fixtures/stream-h264.h264","rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long length=ftell(file); assert(length>5);
    rewind(file); uint8_t *data=malloc((size_t)length); assert(data);
    assert(fread(data,1,(size_t)length,file)==(size_t)length); fclose(file);
    size_t first=1;
    for(;first+5<(size_t)length;first++)
        if(!data[first] && !data[first+1] && !data[first+2] && data[first+3]==1 && data[first+4]==9) break;
    assert(first+5<(size_t)length);
    struct yf_vt_stream *s=yf_vt_stream_new(); assert(s);
    const uint8_t *planes[3]; int strides[3],width=0,height=0;
    assert(yf_vt_stream_decode(s,data,first,planes,strides,&width,&height)==1);
    assert(width==320 && height==180 && planes[0] && planes[1] && planes[2]);
    yf_vt_stream_free(s);
    /* Truncated avcC and Annex B headers must not read beyond their input. */
    uint8_t malformed[]={1,77,0,30,255,225,255,255,0,0,0,1,7};
    for(size_t n=0;n<sizeof(malformed);n++) {
        struct yf_vt *d=yf_vt_open(malformed,n); assert(!d);
        s=yf_vt_stream_new(); assert(s);
        assert(yf_vt_stream_decode(s,malformed,n,planes,strides,&width,&height)<0);
        yf_vt_stream_free(s);
    }
    assert(!yf_aac_open(0,2,malformed,sizeof(malformed)));
    assert(!yf_aac_open(44100,99,malformed,sizeof(malformed)));
    free(data);
    puts("VideoToolbox keyframe and bounded malformed inputs passed");
}
