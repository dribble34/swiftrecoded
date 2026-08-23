#include "prot/syscall_obf.h"
#include <windows.h>
namespace syscall_obf {
volatile void* tbl[16]={};
void init(){ for(int i=0;i<16;i++) tbl[i]=(void*)(uintptr_t)(0x1000+i*0x123); }
}