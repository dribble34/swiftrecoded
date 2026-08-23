#include "prot/control_flow.h"
#include <intrin.h>
namespace control_flow {
volatile int sink=0;
void flatten(int x){ int s=x%4; switch(s){ case 0: sink^=0x11; break; case 1: sink^=0x22; break; case 2: sink^=0x33; break; default: sink^=0x44; break; } __nop(); }
void init(){ for(int i=0;i<70;i++) flatten(i); }
}