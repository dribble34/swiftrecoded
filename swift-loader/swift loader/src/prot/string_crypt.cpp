#include "prot/string_crypt.h"
#include <cstdint>
#include <cstring>
namespace string_crypt {
void xor_crypt(char* s, size_t n, uint8_t k){ for(size_t i=0;i<n;i++) s[i] ^= (char)(k + i*0x11); }
}