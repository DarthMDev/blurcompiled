/* New synthetic static-DLL chain. Compile as ROLE=0 (EXE), 1 (A), 2 (B).
 * No original game code, CRT imports, host pointers or FILE layout. */
typedef unsigned int u32;
typedef void *handle;
__declspec(dllimport) handle __stdcall GetStdHandle(u32);
__declspec(dllimport) int __stdcall WriteFile(handle,const void *,u32,u32 *,void *);
__declspec(dllimport) handle __stdcall GetModuleHandleA(const char *);
__declspec(dllimport) handle __stdcall LoadLibraryA(const char *);
__declspec(dllimport) int __stdcall FreeLibrary(handle);
__declspec(dllimport) void __stdcall ExitProcess(u32);
static void text(const char *s) {
    u32 len=0,written=0;while(s[len])++len;
    if(!WriteFile(GetStdHandle((u32)-11),s,len,&written,0)||written!=len)ExitProcess(90);
}
static void require(int ok) { if(!ok){text("slice.failure\n");ExitProcess(91);} }
#if ROLE == 2
__declspec(dllexport) u32 b_value;
__declspec(dllexport) u32 __cdecl b_code(void) { return b_value ^ 0x01010101; }
int __stdcall dll_start(handle self,u32 reason,void *reserved) {
    require(self!=0);
    if(reason==1) { require(reserved!=0);b_value=0x12345678;text("slice.b.attach.static\n"); }
    if(reason==0) { require(reserved!=0);text("slice.b.detach.process\n"); }
    return 1;
}
#elif ROLE == 1
__declspec(dllimport) u32 b_value;
__declspec(dllimport) u32 __cdecl b_code(void);
__declspec(dllexport) u32 __cdecl a_code(void) { return b_code() ^ b_value; }
int __stdcall dll_start(handle self,u32 reason,void *reserved) {
    require(self!=0);
    if(reason==1) {
        require(reserved!=0 && b_value==0x12345678 && a_code()==0x01010101);
        text("slice.a.attach.static\n");
    }
    if(reason==0) { require(reserved!=0);text("slice.a.detach.process\n"); }
    return 1;
}
#else
__declspec(dllimport) u32 __cdecl a_code(void);
void __cdecl start(void) {
    text("slice.exe.start\n");require(a_code()==0x01010101);
    handle a=GetModuleHandleA("slice_a.dll"),b=GetModuleHandleA("slice_b.dll");
    require(a!=0 && b!=0);
    require(LoadLibraryA("slice_a.dll")==a && FreeLibrary(a));
    require(GetModuleHandleA("slice_a.dll")==a && a_code()==0x01010101);
    text("slice.exe.complete\n");ExitProcess(0);
}
#endif
