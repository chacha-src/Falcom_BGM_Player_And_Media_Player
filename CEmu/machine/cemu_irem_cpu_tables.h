#pragma once
#include <stdint.h>

/* Per-set opcode substitution table for Irem's encrypted V35 ("Software
   Guard") sound CPU. Returns NULL when the archive is not an encrypted set. */
const uint8_t* CEmuIremCpuDecryptionTable(const char* archive);
