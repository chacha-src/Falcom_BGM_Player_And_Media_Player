#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

extern "C" void OxzPulseEncode(unsigned long long done, unsigned long long span);
extern "C" void OxzPulseDecode(unsigned long long produced);
extern "C" int OxzEncodeLevel(void);
extern "C" int OxzEncodeScout(void);

int OxzPictMaybe(const uint8_t* p, size_t n);
int OxzVidMaybe(const uint8_t* p, size_t n);
int OxzPictCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
int OxzPictDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen);
int OxzVidCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
int OxzVidDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen);
int OxzLzssCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
int OxzLzssDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen);
