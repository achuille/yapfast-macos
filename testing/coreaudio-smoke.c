// Local integration test: enqueue silence, drain, flush, reopen, reject a bad UID.
#include "../crates/yapfast-core/native/coreaudio.c"
#include <assert.h>
#include <stdio.h>
int main(void) {
    for(int floating=0; floating<2; floating++) {
        int error=0;
        struct yf_audio *s=yf_audio_open(0,NULL,48000,2,floating,960,&error);
        assert(s && !error);
        unsigned char silence[48000]={0};
        for(int i=0;i<4;i++) assert(!yf_audio_write(s,silence,sizeof(silence)));
        assert(!yf_audio_drain(s));
        assert(!yf_audio_write(s,silence,sizeof(silence)));
        yf_audio_flush(s);
        assert(!yf_audio_write(s,silence,sizeof(silence)));
        assert(!yf_audio_drain(s));
        yf_audio_close(s);
    }
    int error=0;
    struct yf_audio *s=yf_audio_open(0,"yapfast-nonexistent-device",48000,2,0,960,&error);
    assert(!s && error);
    puts("CoreAudio output: PCM16, float32, drain, flush, restart and invalid device passed");
}
