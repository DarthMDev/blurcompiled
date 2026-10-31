/* New compiler-generated MSVC x86 EH probe. This checks this Clang object's
 * interaction with the pinned VC90 DLL, not all VC90 compiler output. */
typedef unsigned int u32;
extern "C" {
__declspec(dllimport) void *__stdcall GetStdHandle(u32);
__declspec(dllimport) int __stdcall WriteFile(void *,const void *,u32,u32 *,void *);
__declspec(dllimport) void __stdcall ExitProcess(u32);
}
static void text(const char *s) {
    u32 len=0,written=0;while(s[len])++len;
    if(!WriteFile(GetStdHandle((u32)-11),s,len,&written,0)||written!=len)ExitProcess(90);
}
static volatile int destroyed;
struct Scope { ~Scope() { ++destroyed; text("eh.destructor\n"); } };
__declspec(noinline) static void raise_value() {
    Scope scope;
    throw 37;
}
extern "C" void __cdecl start(void) {
    text("eh.begin\n");
    int caught=0;
    try { raise_value(); }
    catch(int value) { if(value==37)caught=1;text("eh.catch.int\n"); }
    catch(...) { text("eh.catch.unexpected\n"); }
    if(caught!=1 || destroyed!=1){text("eh.failure\n");ExitProcess(91);}
    text("eh.complete\n");ExitProcess(0);
}
