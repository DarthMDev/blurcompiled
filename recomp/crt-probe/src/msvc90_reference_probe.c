/* New synthetic i386 reference/parity probe for the pinned Microsoft VC90 DLL.
 * Exports are checked relative to the loaded module so an OS loader may rebase.
 * Allocations must be nonnull; the native host separately enforces arena bounds.
 * FILE is opaque. Run only in an isolated C:\CRTProbe scratch environment.
 * This does not establish full ABI, locale, exception or game compatibility.
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

typedef void *(__cdecl *fopen_fn)(const char *, const char *);
typedef u32(__cdecl *io_fn)(void *, u32, u32, void *);
typedef int(__cdecl *seek_fn)(void *, int, int);
typedef int(__cdecl *close_fn)(void *);
typedef int(__cdecl *remove_fn)(const char *);
typedef char *(__cdecl *locale_fn)(int, const char *);
typedef int *(__cdecl *errno_fn)(void);
static handle output;
static void extended(handle crt);

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
    require((u32)address >= (u32)crt && (u32)address < (u32)crt + 0xa3000,
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
    require(crt != 0, "LoadLibrary failed");
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
    require(p != 0, "allocation failed");
    for (u32 i = 0; i < 32; ++i) p[i] = (unsigned char)(i ^ 0xa5);
    word("crt.msize32", size(p));
    require(size(p) >= 32, "allocation size too small");
    unsigned char *grown = resize(p, 96);
    require(grown != 0,
            "reallocation failed");
    for (u32 i = 0; i < 32; ++i) require(grown[i] == (unsigned char)(i ^ 0xa5), "realloc lost data");
    word("crt.msize96", size(grown));
    require(size(grown) >= 96, "reallocated size too small");
    release(grown);
    extended(crt);
    require(FreeLibrary(crt), "FreeLibrary failed");
    text("crt.smoke.complete 1\n");
    ExitProcess(0);
}

/* New synthetic behavior checks; no recovered game logic or FILE layout. */
static void extended(handle crt) {
    fopen_fn open = (fopen_fn)resolve(crt, "fopen");
    io_fn write = (io_fn)resolve(crt, "fwrite"), read = (io_fn)resolve(crt, "fread");
    seek_fn seek = (seek_fn)resolve(crt, "fseek");
    close_fn close = (close_fn)resolve(crt, "fclose");
    remove_fn remove_file = (remove_fn)resolve(crt, "remove");
    locale_fn locale = (locale_fn)resolve(crt, "setlocale");
    errno_fn error = (errno_fn)resolve(crt, "_errno");
    char *name = locale(0, 0);
    require(name != 0, "locale query failed");
    text("crt.locale.initial "); text(name); text("\n");
    name = locale(0, "C");
    require(name && name[0] == 'C' && !name[1], "C locale rejected");
    text("crt.locale.C "); text(name); text("\n");
    require(locale(0, "BlurSyntheticInvalidLocale_123") == 0, "invalid locale accepted");
    name = locale(0, 0);
    require(name && name[0] == 'C' && !name[1], "invalid locale changed state");
    void *f = open("C:\\CRTProbe\\synthetic-r2.bin", "w+b");
    require(f != 0, "fopen failed");
    unsigned char data[5] = {0x41,0x0a,0x00,0xff,0x5a}, got[5];
    require(write(data, 1, 5, f) == 5, "fwrite failed");
    require(seek(f, 0, 0) == 0, "fseek failed");
    require(read(got, 1, 5, f) == 5, "fread failed");
    for(u32 i=0;i<5;++i) require(got[i] == data[i], "FILE binary bytes changed");
    require(close(f) == 0, "fclose failed");
    require(remove_file("C:\\CRTProbe\\synthetic-r2.bin") == 0, "remove failed");
    *error() = 0;
    require(open("C:\\CRTProbe\\missing-synthetic-r2.bin", "rb") == 0, "missing file opened");
    word("crt.file.missing.errno", (u32)*error());
    require(*error() == 2, "missing file errno differs");
    text("crt.file.binary.complete 1\n");
}
