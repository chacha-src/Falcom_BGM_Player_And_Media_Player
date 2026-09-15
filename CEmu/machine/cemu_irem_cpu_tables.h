#pragma once
#include <stdint.h>

/* Irem 暗号化 V35（Software Guard）のセット別オペコード置換表。
   非暗号セットなら NULL。 */
const uint8_t* CEmuIremCpuDecryptionTable(const char* archive);
