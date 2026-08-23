#include "syscalls.hpp"
#include <cstring>
#include <algorithm>

namespace
{
    enum
    {
        QIP=0,  QSI=1,  SIT=2,  GCT=3,  NOP=4,  NTP=5,
        CTE=6,  AVM=7,  FVM=8,  PVM=9,  RVM=10, WVM=11,
        CLO=12, DE=13,  NOK=14, SVK=15, RT=16,  NEK=17,
        QVK=18, NOF=19, QVIF=20, QO=21,
        TOTAL=22
    };

    struct Entry { const char* name; uint16_t ssn; };
    Entry g_ssn[TOTAL] =
    {
        { "NtQueryInformationProcess",    0 },
        { "NtQuerySystemInformation",     0 },
        { "NtSetInformationThread",       0 },
        { "NtGetContextThread",           0 },
        { "NtOpenProcess",                0 },
        { "NtTerminateProcess",           0 },
        { "NtCreateThreadEx",             0 },
        { "NtAllocateVirtualMemory",      0 },
        { "NtFreeVirtualMemory",          0 },
        { "NtProtectVirtualMemory",       0 },
        { "NtReadVirtualMemory",          0 },
        { "NtWriteVirtualMemory",         0 },
        { "NtClose",                      0 },
        { "NtDelayExecution",             0 },
        { "NtOpenKey",                    0 },
        { "NtSetValueKey",                0 },
        { "NtResumeThread",               0 },
        { "NtEnumerateKey",               0 },
        { "NtQueryValueKey",              0 },
        { "NtOpenFile",                   0 },
        { "NtQueryVolumeInformationFile", 0 },
        { "NtQueryObject",                0 },
    };

    using Stub = uint8_t[16];
    Stub*  g_stubs = nullptr;
    bool   g_ready = false;

    uint16_t read_ssn( const uint8_t* fn )
    {
        if ( fn[0]==0x4C && fn[1]==0x8B && fn[2]==0xD1 && fn[3]==0xB8 )
            return *reinterpret_cast<const uint16_t*>( fn + 4 );
        return 0xFFFF;
    }

    uint16_t resolve( HMODULE ntdll, const char* name )
    {
        auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>( ntdll );
        auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS*>( (uint8_t*)ntdll + dos->e_lfanew );
        auto& exp  = nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ];
        if ( !exp.VirtualAddress ) return 0xFFFF;
        auto* dir  = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>( (uint8_t*)ntdll + exp.VirtualAddress );
        auto* nms  = reinterpret_cast<DWORD*>( (uint8_t*)ntdll + dir->AddressOfNames );
        auto* ords = reinterpret_cast<WORD*> ( (uint8_t*)ntdll + dir->AddressOfNameOrdinals );
        auto* fns  = reinterpret_cast<DWORD*>( (uint8_t*)ntdll + dir->AddressOfFunctions );
        int idx = -1;
        for ( DWORD i = 0; i < dir->NumberOfNames; ++i )
            if ( !strcmp( (char*)ntdll + nms[i], name ) ) { idx = (int)i; break; }
        if ( idx < 0 ) return 0xFFFF;
        auto* fn = (const uint8_t*)ntdll + fns[ ords[idx] ];
        uint16_t ssn = read_ssn( fn );
        if ( ssn != 0xFFFF ) return ssn;
        for ( int d = 1; d <= 8; ++d )
        {
            int dirs[2] = { -1, +1 };
            for ( int di = 0; di < 2; ++di )
            {
                int ni = idx + d * dirs[di];
                if ( ni < 0 || ni >= (int)dir->NumberOfNames ) continue;
                uint16_t ns = read_ssn( (const uint8_t*)ntdll + fns[ ords[ni] ] );
                if ( ns == 0xFFFF ) continue;
                int inf = (int)ns - d * dirs[di];
                if ( inf >= 0 && inf <= 0x1FFF ) return (uint16_t)inf;
            }
        }
        return 0xFFFF;
    }

    void build_stub( Stub& s, uint16_t ssn )
    {
        s[0]=0x4C; s[1]=0x8B; s[2]=0xD1;
        s[3]=0xB8; s[4]=ssn&0xFF; s[5]=ssn>>8; s[6]=0; s[7]=0;
        s[8]=0x0F; s[9]=0x05; s[10]=0xC3;
        memset( s+11, 0x90, 5 );
    }

    template<typename... A>
    NTSTATUS call( int i, A... a )
    {
        if ( !g_ready ) return -1L;
        using F = NTSTATUS(__fastcall*)( A... );
        return reinterpret_cast<F>( static_cast<void*>( &g_stubs[i][0] ) )( a... );
    }

    uintptr_t find_ntdll()
    {
        auto* peb = reinterpret_cast<uint8_t*>( __readgsqword(0x60) );
        auto* ldr = *reinterpret_cast<uint8_t**>( peb + 0x18 );
        auto* head = reinterpret_cast<uint8_t*>( ldr + 0x20 );
        auto* cur  = *reinterpret_cast<uint8_t**>( head );
        while ( cur && cur != head )
        {
            auto  base = *reinterpret_cast<uintptr_t*>( cur + 0x20 );
            auto* name = *reinterpret_cast<wchar_t**>( cur + 0x50 );
            if ( base && name )
            {
                wchar_t low[7]{};
                for ( int i = 0; i < 6 && name[i]; ++i )
                    low[i] = (name[i]>='A'&&name[i]<='Z') ? name[i]+32 : name[i];
                if ( low[0]=='n'&&low[1]=='t'&&low[2]=='d'&&low[3]=='l'&&low[4]=='l'&&low[5]=='.' )
                    return base;
            }
            cur = *reinterpret_cast<uint8_t**>( cur );
        }
        return 0;
    }
}

namespace syscalls
{

bool init()
{
    const uintptr_t ntdll_base = find_ntdll();
    if ( !ntdll_base ) return false;
    auto* ntdll = reinterpret_cast<HMODULE>( ntdll_base );
    for ( auto& e : g_ssn )
    {
        e.ssn = resolve( ntdll, e.name );
        if ( e.ssn == 0xFFFF ) return false;
    }
    
    {
        using AVM_t = LONG(NTAPI*)(HANDLE, void**, ULONG_PTR, SIZE_T*, ULONG, ULONG);

        const auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>( ntdll_base );
        const auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS*>( ntdll_base + dos->e_lfanew );
        const auto& edir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        const auto* exp  = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>( ntdll_base + edir.VirtualAddress );
        const auto* fns  = reinterpret_cast<uint32_t*>( ntdll_base + exp->AddressOfFunctions );
        const auto* nms  = reinterpret_cast<uint32_t*>( ntdll_base + exp->AddressOfNames );
        const auto* ords = reinterpret_cast<uint16_t*>( ntdll_base + exp->AddressOfNameOrdinals );

        auto streq = []( const char* a, const char* b ) -> bool {
            while ( *a && *b ) { if ( *a++ != *b++ ) return false; }
            return *a == *b;
        };

        AVM_t real_avm = nullptr;
        for ( ULONG i = 0; i < exp->NumberOfNames; ++i )
        {
            const char* nm = reinterpret_cast<const char*>( ntdll_base + nms[i] );
            if ( streq( nm, "NtAllocateVirtualMemory" ) )
            {
                real_avm = reinterpret_cast<AVM_t>( ntdll_base + fns[ ords[i] ] );
                break;
            }
        }

        if ( !real_avm ) return false;

        void* _b = nullptr;
        SIZE_T _s = sizeof(Stub) * TOTAL;
        real_avm( reinterpret_cast<HANDLE>(-1), &_b, 0, &_s,
                  MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
        g_stubs = reinterpret_cast<Stub*>( _b );
    }
    if ( !g_stubs ) return false;
    for ( int i = 0; i < TOTAL; ++i ) build_stub( g_stubs[i], g_ssn[i].ssn );
    g_ready = true;
    return true;
}

NTSTATUS query_information_process( HANDLE p, uint32_t c, void* i, ULONG l, ULONG* r )  { return call(QIP,p,c,i,l,r); }
NTSTATUS query_system_information( uint32_t c, void* i, ULONG l, ULONG* r )              { return call(QSI,c,i,l,r); }
NTSTATUS set_information_thread( HANDLE t, uint32_t c, void* i, ULONG l )                { return call(SIT,t,c,i,l); }
NTSTATUS get_context_thread( HANDLE t, CONTEXT* c )                                   { return call(GCT,t,c); }
NTSTATUS open_process( HANDLE* h, ULONG a, OBJECT_ATTRIBUTES_NT* o, CLIENT_ID_NT* c ) { return call(NOP,h,a,o,c); }
NTSTATUS terminate_process( HANDLE p, NTSTATUS s )                                    { return call(NTP,p,s); }
NTSTATUS create_thread_ex( HANDLE* t, ULONG a, OBJECT_ATTRIBUTES_NT* o, HANDLE p, void* s, void* g, ULONG f, SIZE_T z, SIZE_T ss, SIZE_T ms, void* al ) { return call(CTE,t,a,o,p,s,g,f,z,ss,ms,al); }
NTSTATUS allocate_virtual_memory( HANDLE p, void** b, ULONG_PTR z, SIZE_T* s, ULONG t, ULONG pr ) { return call(AVM,p,b,z,s,t,pr); }
NTSTATUS free_virtual_memory( HANDLE p, void** b, SIZE_T* s, ULONG t )               { return call(FVM,p,b,s,t); }
NTSTATUS protect_virtual_memory( HANDLE p, void** b, SIZE_T* s, ULONG pr, ULONG* o ) { return call(PVM,p,b,s,pr,o); }
NTSTATUS read_virtual_memory( HANDLE p, void* b, void* out, SIZE_T s, SIZE_T* r )    { return call(RVM,p,b,out,s,r); }
NTSTATUS write_virtual_memory( HANDLE p, void* b, const void* in, SIZE_T s, SIZE_T* w ) { return call(WVM,p,b,in,s,w); }
NTSTATUS close( HANDLE h )                                                             { return call(CLO,h); }
NTSTATUS delay_execution( BOOLEAN a, LARGE_INTEGER* i )                               { return call(DE,a,i); }
NTSTATUS open_key( HANDLE* h, ULONG a, OBJECT_ATTRIBUTES_NT* o )                     { return call(NOK,h,a,o); }
NTSTATUS set_value_key( HANDLE h, UNICODE_STRING_NT* n, ULONG ti, ULONG t, const void* d, ULONG s ) { return call(SVK,h,n,ti,t,d,s); }
NTSTATUS resume_thread( HANDLE h )               { ULONG prev = 0; return call(RT,h,&prev); }
NTSTATUS enumerate_key( HANDLE h, ULONG i, ULONG c, void* b, ULONG l, ULONG* r )    { return call(NEK,h,i,c,b,l,r); }
NTSTATUS query_value_key( HANDLE h, UNICODE_STRING_NT* n, ULONG c, void* i, ULONG l, ULONG* r ) { return call(QVK,h,n,c,i,l,r); }
NTSTATUS open_file( HANDLE* h, ULONG a, OBJECT_ATTRIBUTES_NT* o, void* isb, ULONG sa, ULONG oa ) { return call(NOF,h,a,o,isb,sa,oa); }
NTSTATUS query_volume_information_file( HANDLE h, void* isb, void* buf, ULONG len, ULONG cls ) { return call(QVIF,h,isb,buf,len,cls); }
NTSTATUS query_object( HANDLE h, uint32_t c, void* i, ULONG l, ULONG* r )                { return call(QO,h,c,i,l,r); }

}
