/* New load-time VC90 import fixture. Reuses the synthetic dynamic checks after
 * proving that code and data imports work before any explicit LoadLibrary. */
#define start dynamic_checks
#include "../crt-probe/src/msvc90_reference_probe.c"
#undef start
__declspec(dllimport) void *__cdecl malloc(u32);
__declspec(dllimport) void __cdecl free(void *);
__declspec(dllimport) unsigned char _HUGE[8];
__declspec(dllimport) handle __stdcall GetModuleHandleA(const char *);
void __cdecl start(void) {
    output = GetStdHandle((u32)-11);
    text("crt.static.begin 1\n");
    handle crt = GetModuleHandleA("msvcr90.dll");
    require(crt != 0, "CRT absent before explicit load");
    require((void *)malloc == GetProcAddress(crt,"malloc"), "static code import differs");
    require((void *)_HUGE == GetProcAddress(crt,"_HUGE"), "static data import differs");
    require(bytes_word(_HUGE) == 0 && bytes_word(_HUGE+4) == 0x7ff00000, "static HUGE differs");
    unsigned char *p = malloc(16);
    require(p != 0, "static malloc failed");
    p[0]=0xa5;p[15]=0x5a;require(p[0]==0xa5 && p[15]==0x5a,"static allocation lost data");
    free(p);
    text("crt.static.complete 1\n");
    dynamic_checks();
}
