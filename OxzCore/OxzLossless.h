#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

int OxzPcmMaybe(const uint8_t* p, size_t n);
int OxzFlacCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
int OxzFlacDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen);
int OxzAlacCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
int OxzAlacDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen);
int OxzTtaCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
int OxzTtaDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen);
