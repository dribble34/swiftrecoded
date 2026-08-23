#include "prot/antidebug_junk.h"
#include <windows.h>
#include <intrin.h>
#include <cstdint>
namespace antidebug_junk {
volatile uint64_t seed=0x9E3779B97F4A7C15ULL;
inline void f(int x){ volatile uint64_t a=__rdtsc() ^ seed ^ (uint64_t)x; a ^= (a<<13)^(a>>7); seed+=a; __nop(); }
void init(){ for(int i=0;i<80;i++){ f(i); f(i*3); } }
}