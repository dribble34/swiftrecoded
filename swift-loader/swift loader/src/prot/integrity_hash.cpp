#include "prot/integrity_hash.h"
#include <windows.h>
#include <cstdint>
namespace integrity_hash {
volatile uint32_t h=0x811C9DC5;
inline void mix(uint32_t x){ h ^= x; h *= 0x1000193; }
void init(){ for(int i=0;i<90;i++) mix((uint32_t)i*0x9E3779B9); }
uint32_t get(){ return h; }
}