#pragma once
#include <stdint.h>
#include <stddef.h>
struct yf_aac;
struct yf_aac *yf_aac_open(int rate,int channels,const uint8_t *config,size_t length);
void yf_aac_free(struct yf_aac *decoder);
void yf_aac_reset(struct yf_aac *decoder);
int yf_aac_decode(struct yf_aac *decoder,const uint8_t *data,size_t length,const float **samples);
