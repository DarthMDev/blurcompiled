# Legacy SHA-1 adapter slice

This is a new portable implementation of the reached unkeyed SHA-1 CryptoAPI
path, not recovered Microsoft code or general CryptoAPI acceptance.

The original experimental shim ignored CryptHashData and returned sixteen bytes
of `42` from CryptGetHashParam. In the pinned Blur executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`,
VA `00cb8210` / RVA `008b8210` requests algorithm `8004` (SHA-1), and the caller
at VA `00cb726d` / RVA `008b726d` passes a 20-byte hash to its validator.
The failing path at VA `00cb7292` / RVA `008b7292` reports `cache\gamedata.pak`
through the registered callback, eventually reaching VA `008528b0` / RVA
`004528b0`, which writes application state 2. The frame loop at VA `00849d54`
/ RVA `00449d54` sends WM_CLOSE when that state is nonzero. Confidence is high
for these instruction/field observations and the live quit path, from corrected
listings, original PE bytes, and the read-only observer logs.

The real hash adapter removes that observed failure without changing files,
comparison results, error callbacks, or game branches. It does not implement
keys, signatures, persistent key containers, other hash algorithms or Amax
messages/authentication. Random-number support remains a separate older shim.

The implementation uses opaque 32-bit provider/hash handles, isolated streaming
states, HP_ALGID/HP_HASHSIZE/HP_HASHVAL queries, buffer-size checks, finalization
on successful hash-value output, and stale/foreign handle rejection. Only
keyless Base-provider contexts and zero hash flags are implemented by this
slice; other provider types/names and key operations are refused. Active hashes
must be destroyed before their provider can be released. Full Windows error
precedence, persistent-provider behavior and size-only HASHVAL finalization
parity have not been measured against a Windows oracle.

Contract references checked October 18, 2026:

- [CryptCreateHash](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptcreatehash)
- [CryptHashData](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-crypthashdata)
- [CryptGetHashParam](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptgethashparam)
- [CryptDestroyHash](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptdestroyhash)

Validation uses synthetic inputs: empty and `abc`, the million-`a` vector,
independently generated hashlib digests around SHA-1 padding/block boundaries,
multiple chunk sizes, concurrent logical hash handles, insufficient buffers,
output guards, finalization, invalid flags/ranges and unsupported algorithms.
`recomp/crypto/crypto_tests.cpp` exercises the actual adapter and portable core.

For bounded startup verification, `recomp/crypto/install.cpp` registers the same
runtime shim implementation before the first guest crypto import, preserving
existing observers. It refuses installation if the first observed import is
already crypto, because that import's old function pointer would have been
captured by the dispatcher. The final normal host integration registers these
shims in imports_init; a rebuilt host would not need this temporary module.
Native builds go through kit tools/build.py. The original host remains intact.
