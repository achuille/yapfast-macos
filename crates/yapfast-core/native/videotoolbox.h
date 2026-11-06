#pragma once
#include <stdint.h>
#include <stddef.h>
struct yf_vt;
struct yf_vt *yf_vt_open(const uint8_t *avcc, size_t length);
void yf_vt_free(struct yf_vt *decoder);
int yf_vt_send(struct yf_vt *decoder, const uint8_t *data, size_t length, int64_t pts);
int yf_vt_receive(struct yf_vt *decoder, const uint8_t **planes, int *strides, int *width, int *height, int64_t *pts);

struct yf_vt_stream;
struct yf_vt_stream *yf_vt_stream_new(void);
void yf_vt_stream_free(struct yf_vt_stream *stream);
int yf_vt_stream_decode(struct yf_vt_stream *stream,const uint8_t *data,size_t length,
                       const uint8_t **planes,int *strides,int *width,int *height);
