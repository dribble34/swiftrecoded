#include "prot/vm_junk.h"
#include <intrin.h>
#include <cstdint>
namespace vm_junk {
volatile uint64_t v=0;
void junk(){ v ^= __rdtsc(); v = (v<<17)|(v>>47); __nop(); }
void init(){ for(int i=0;i<80;i++) junk(); }
}