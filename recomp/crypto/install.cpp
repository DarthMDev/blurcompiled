// Bounded verification of the actual runtime implementation against the
// existing host. Install before its first guest crypto import; retain earlier
// observers. No game instruction, validation result, hash data or comparison is
// rewritten.
#include "crypto.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
ImportCallObserver previous;
bool installed = false;
bool install_before_import(const char *name, const uint32_t *args,
                           uint32_t argc, uint32_t *result) {
  if (!installed) {
    // imports_dispatch captured this call's fn before invoking its observer.
    // Require a non-crypto first import, so the obsolete shim cannot run.
    if (strstr(name, "!Crypt")) {
      fputs("crypto.slice: first import is already crypto\n", stderr);
      abort();
    }
    imports_register(g_crypto_shims, g_crypto_shim_count);
    installed = true;
    fprintf(stderr, "crypto.slice: installed real SHA-1 before %s\n", name);
  }
  return previous ? previous(name, args, argc, result) : true;
}
__attribute__((constructor)) void start() {
  previous = imports_set_call_observer_get();
  imports_set_call_observer(install_before_import);
}
} // namespace
