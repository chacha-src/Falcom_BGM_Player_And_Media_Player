#pragma once
#include <stddef.h>
#include <stdint.h>
/* x64 の RIP 相対アドレスを絶対番地に寄せる。7z の BCJ2 の前にかける。可逆。 */
void OxzRipConvert(uint8_t* data, size_t size, int encoding);
