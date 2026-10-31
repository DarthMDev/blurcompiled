# Amax and Discord compatibility notebook

## Observed locally

The supplied `amax/config/amax-redirect.cfg` maps the PC Demonware auth and lobby
hosts to `master.amax-emu.com`, and four regional STUN hosts to
`stun.amax-emu.com`. Its address values refer to the x86 reference program; they
are not native implementation addresses. Endpoint reachability was not tested.

`amax/dlls/amax_auth.asi` SHA-256:
`4437bd98cbb4d2b80ced3246b5cfc065251642c3865543465de104b89fb4b1a1`.
It contains Rust crate/module strings, including a platform stream socket module
and HTTP/WebSocket-related library strings. This suggests additional transport
behavior; strings alone do not establish actual on-wire use or protocol details.

`amax/dlls/blur_rpc.dll` SHA-256:
`840133ec90eaa7f176df7847a785f0068b78e81b5d6c301f3f14c5670ca0e3f7`.
Static imports from `discord-rpc.dll` are `Discord_Initialize`,
`Discord_Shutdown`, `Discord_RunCallbacks`, and `Discord_UpdatePresence`.

| Evidence in presence DLL | File offset | RVA | Interpretation |
| --- | --- | --- | --- |
| `933390770453499974` | `0x7f7c` | `0x997c` | Application ID passed to Discord_Initialize; statically confirmed |
| `Main menu` | `0x7864` | `0x9264` | State label |
| `In online lobby` | `0x79a0` | `0x93a0` | State label |
| `Racing` | `0x79f0` | `0x93f0` | State label |

Other observed labels cover multiplayer menus, private lobbies and playlists.
Asset-key strings and party identifiers are present. No complete mapping has
been reconstructed; no presence update has been sent to Discord.

At RVA `0x2352`, the DLL pushes the ID string's preferred VA `0x1000997c`;
at RVA `0x2357`, it calls through IAT VA `0x10009204`, identified from the PE
import table as `Discord_Initialize`. The three preceding arguments are zero.
This establishes the static call equivalent to
`Discord_Initialize("933390770453499974", nullptr, 0, nullptr)`.
It does not establish current Discord application ownership or availability.

## Native integration approach

Recover the auth module's framing, endpoints, session lifecycle and hook behavior.
Use protocol fixtures from controlled reference runs, with credentials and
account-specific data omitted from tracked files. Build explicit serializers and
parsers, then validate the full auth/lobby/race lifecycle against the PC client.
Host redirection alone will not provide native Amax compatibility.

For presence, trace application ID, image keys, details, timestamps, party sizes
and join semantics. Keep the same visible behavior where confirmed and implement
a native transport or SDK adapter. The Windows DLL cannot load into ARM64 macOS.
An unavailable Discord client should not stop the game. Confirm available SDK
and mobile support at implementation time.

## Public leads inspected

- [Amax organization](https://github.com/Amax-Emu): project and repository index.
- [Client-file repository](https://github.com/Amax-Emu/amax_emu_files): distribution
  material; not a complete server protocol specification.
- [FPS unlocker](https://github.com/Amax-Emu/blur-fps-unlocker): a possible future
  reference for timing-related investigation; source not incorporated.
- [Discord's legacy RPC repository](https://github.com/discord/discord-rpc):
  reference for the imported API; evaluate current integration choices separately.

No complete Amax protocol specification was found in the public pages inspected.
No maintainer has been contacted and no server traffic has been generated.
