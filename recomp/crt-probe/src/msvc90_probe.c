/* Synthetic i386 CRT ownership probe. No original game code or host pointers.
 * Passing these assertions alone does not validate DLL initialization or VC90
 * compatibility: the experiment host must also reject every runtime fallback.
 */
typedef unsigned int u32;
typedef void *handle;
typedef char pointer_is_32_bits[sizeof(void *) == 4 ? 1 : -1];

__declspec(dllimport) handle __stdcall GetStdHandle(u32);
__declspec(dllimport) int __stdcall WriteFile(handle, const void *, u32, u32 *, void *);
__declspec(dllimport) handle __stdcall LoadLibraryA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(handle, const char *);
__declspec(dllimport) int __stdcall FreeLibrary(handle);
__declspec(dllimport) void __stdcall ExitProcess(u32);

typedef void *(__cdecl *malloc_fn)(u32);
typedef void *(__cdecl *realloc_fn)(void *, u32);
typedef void(__cdecl *free_fn)(void *);
typedef u32(__cdecl *msize_fn)(void *);

static handle output;

static void text(const char *s) {
    u32 size = 0, written = 0;
    while (s[size]) ++size;
    if (!WriteFile(output, s, size, &written, 0) || written != size) ExitProcess(90);
}

static void word(const char *key, u32 value) {
    char hex[11];
    const char *digits = "0123456789abcdef";
    hex[0] = '0'; hex[1] = 'x'; hex[10] = 0;
    for (u32 i = 0; i < 8; ++i) hex[2 + i] = digits[(value >> (28 - 4 * i)) & 15];
    text(key); text(" "); text(hex); text("\n");
}

static void require(int condition, const char *reason) {
    if (!condition) {
        text("crt.smoke.failure "); text(reason); text("\n");
        ExitProcess(91);
    }
}

static void *resolve(handle crt, const char *name) {
    void *address = GetProcAddress(crt, name);
    word(name, (u32)address);
    require((u32)address >= 0x78520000 && (u32)address < 0x785c3000,
            "export outside pinned DLL image");
    return address;
}

/* Read bytes to avoid asserting an unverified host type for exported data.
 * These observed values require comparison with independent original execution.
 */
static u32 bytes_word(const unsigned char *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void __cdecl start(void) {
    output = GetStdHandle((u32)-11);
    text("crt.smoke.begin 1\n");
    handle crt = LoadLibraryA("msvcr90.dll");
    word("crt.module", (u32)crt);
    require((u32)crt == 0x78520000, "LoadLibrary failed or returned pseudo handle");
    malloc_fn allocate = (malloc_fn)resolve(crt, "malloc");
    realloc_fn resize = (realloc_fn)resolve(crt, "realloc");
    free_fn release = (free_fn)resolve(crt, "free");
    msize_fn size = (msize_fn)resolve(crt, "_msize");
    unsigned char *cmdline = resolve(crt, "_acmdln");
    unsigned char *huge = resolve(crt, "_HUGE");
    unsigned char *adjust = resolve(crt, "_adjust_fdiv");
    word("crt.acmdln.value", bytes_word(cmdline));
    word("crt.HUGE.low", bytes_word(huge));
    word("crt.HUGE.high", bytes_word(huge + 4));
    word("crt.adjust_fdiv.value", bytes_word(adjust));

    unsigned char *p = allocate(32);
    require((u32)p >= 0x02000000 && (u32)p < 0x0e000000 - 96, "allocation outside guest heap");
    for (u32 i = 0; i < 32; ++i) p[i] = (unsigned char)(i ^ 0xa5);
    require(size(p) >= 32, "allocation size too small");
    unsigned char *grown = resize(p, 96);
    require((u32)grown >= 0x02000000 && (u32)grown < 0x0e000000 - 96,
            "reallocation outside guest heap");
    for (u32 i = 0; i < 32; ++i) require(grown[i] == (unsigned char)(i ^ 0xa5), "realloc lost data");
    require(size(grown) >= 96, "reallocated size too small");
    release(grown);
    require(FreeLibrary(crt), "FreeLibrary failed");
    text("crt.smoke.complete 1\n");
    ExitProcess(0);
}
