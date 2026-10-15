# Campaign co-op beyond two players

Status: Stage 1 is in progress. SDK patch `0010-system-link-sessions` (section 3, Stage 1, item 2)
builds for Windows and passes its unit tests. For Android it has only passed an NDK arm64
`-fsyntax-only` check of the touched files; it has not been built or linked there. The script
patches in guest memory (sections 2 and 4) build for Windows and pass their offline check on a
retail package; their `src/` files have passed the same NDK check and nothing more on Android.
Neither has run in the game yet, and everything else is design only.

Before this branch merges: rebase on master, check that 0002-0010 still apply in order on
`923c1a5` (`git apply --check`, one patch after the other), and run `scripts/build-android.ps1` once
on the merged tree. After the merge, the Android build applies every `sdk/patches/*.patch` and globs
`src/*.cpp`, so it is the first build to see this code.

On the `crossplay` branch, SDK patch 0011-network-crossplay (Stage 4 networking) sits on top of
this 0010, and `scripts/build-android.ps1` has built and linked 0002-0011 there. Two copies of the
crossplay build have joined each other's campaign over loopback on one PC; PC with phone has not
run yet. See [NETWORK_PLAY.md](NETWORK_PLAY.md).

Merge order: this 0010 first, then 0011. Master's 0007 (6fe0d18, 2026-10-15) shifts Android
guest ports 1-1023 by 30000 and translates socket options and FIONBIO/FIONREAD on Android. On
`crossplay` both later patches are rebased onto it: 0010 keeps that shift inside its own `Bind`
rewrite, and 0011 removes 0007's Android shift, option table and ioctl code from
`src/system/xsocket.cpp`, so `net_port_offset` is the one port mapping on Android, Windows and
Linux (a phone that bound 31000 and a PC that sent to 41000 would never meet). 0011's option table
gained SO_KEEPALIVE and SO_OOBINLINE, the two 0007 options it lacked.

This document condenses four read-only investigations from 2026-10-14 (join and spawn path,
campaign systems, patch mechanism, network). A second pass re-checked each one, and the corrections
from that pass are already applied here. The target is 4 players first, then 8-10, then PC-Android
cross-play over System Link.

## Evidence conventions

- `DG:n` / `DE:n`: line n of `out/re-mouse/decomp_GearGame.txt` / `out/re-mouse/decomp_Engine.txt`,
  the script decompiled with this repo's own UnrealScript tools (now in `tools/ue3/`: `ubc.py`,
  `upk.py`, `ue3script.py`, `ue3pkg.py`). A `~` marks an approximate line.
- `Class.Function+0xNNNN`: byte offset of a statement inside that function's bytecode.
- `upk 0x...`: file offset in the decompressed `GearGame.upk` (`out/re-mouse/pkg/`).
- `sub_XXXXXXXX`: a recompiled guest function. `recomp.N.cpp:L` is line L of
  `generated/default/gow2_recomp.N.cpp`.
- Detailed notes and scratch tools are in `out/re-coop/`: `join-spawn.md`, `campaign-systems.md`,
  `patch-mechanism.md`, `network.md`, `join-spawn/verify_patches.py`, `script_patch_poc.py`,
  `hero_links.txt`, `kismet/`.
- `out/` is gitignored. Its contents come from the player's own game files and are not in the
  repository. To reproduce them, decompress your own packages with the tools in `tools/ue3/` (see
  its README).
- **[V]** verified: read in code or bytes and confirmed by a second, independent pass.
  **[I]** inferred: follows from code structure or UE3 knowledge, but has not been confirmed in this
  binary or at run time.

## 1. What limits co-op to two today

Two independent layers stand in the way:

- The host's game script caps the session at two players and has no usable body for a third.
- Underneath, the port's System Link emulation cannot yet run a network session of any size.

### 1.1 The join cap

| Limit | Where | Effect |
|---|---|---|
| `AtCapacity` returns `NumPlayers == 2` | `GearGameSP_Base.AtCapacity+0x0000` (DG:46651), bytes `04 9A 01 FFFFF5F8 2C 02 16` at upk 0x8B112C | Three call sites invoke it as a virtual call: `AccessControl.PreLogin+0x003B` (DE:2486), `GameInfo.Login+0x014A` (DE:21033) and `GearGameSP_Base.PostLogin+0x0010` (DG:46890). `NumPlayers` goes up only later, in `GameInfo.PostLogin+0x004F` (and `HandleSeamlessTravelPlayer+0x0376`). Player 3 therefore arrives with `NumPlayers == 2` and PreLogin refuses him with `MaxedOutMessage`. `MaxPlayers` plays no part in this check. [V] |
| The LAN beacon goes silent | The host's LAN response builder `sub_828294A0` (recomp.75.cpp:~15608) | It answers a query only when `bIsLanMatch` (+156, mask 0x40000000) is set and `NumOpenPublicConnections` (+140) > 0. On every PostLogin of a LAN match, `GameInfo.UpdateGameSettingsCounts` (DE:22135-22142) sets that count to `NumPublicConnections - GetNumPlayers()`. With 2 public connections the host stops answering once two players are in, so player 3 never sees the game. [V] |
| `NumPublicConnections = 2` | `GearMenuGame.ChangeCoopInviteSetting+0x0047` (DG:25140) and the default object `Default__GearCoopGameSettings` (tagged IntProperty, upk 0x86E033) | This value feeds the beacon gate and the menu game's `MaxPlayers` (+0x0086). Settings objects are created at DG:25208, 25438, 89251 and 92950. The one at 25208 passes through `InitializeCoopSettingsFromProfile` and then `ChangeCoopInviteSetting` (+0x0206). Nobody traced whether the others always do, so both values get patched. The two passes disagreed on whether the default object matters. [V values, I paths] |
| `MaxPlayers = 2` | `GearGameSP_Base.InitGame+0x00B4` (DG:46877), `GearCampaignLobbyGame_Base.InitGame+0x009E` (DG:25642) | The SP game's `AtCapacity` ignores it. The campaign lobby, however, uses `GameInfo.AtCapacity`, which reads `MaxPlayers` (DE:20994-21002), so it caps joins made in the GearStart lobby. `Default__GearGameSP_Base.MaxPlayersAllowed = 2` only clamps the `?MaxPlayers=` URL option, and InitGame overwrites the result anyway. [V] |

Two things that look like caps are not:

- **The `PlayerWaiting` branch of `GearGameSP_Base.RestartPlayer` (0x06EE-0x077A).** It sits inside
  the block guarded at +0x0050, which requires `GetNumPlayerControllersActive() > 1 &&
  HumanPlayer != None && DefaultPawnClass == GearPawn_COGDom` (DG:46784). Every other class jumps to
  +0x077D `Super(GearGame).RestartPlayer`, which spawns a new pawn. The branch only handles player
  2 taking over the AI Dom. [V] Do not apply the proof-of-concept edit that turns it into a jump
  (`07 07 27` -> `06 07 19` at +0x06EE).
- **The native session layer.** In this SDK, `XGISessionCreate` and `XGISessionJoinRemote/Local`
  return success whatever the slot counts (`sdk/patches/0003-system-link-trace.patch`, context
  around lines 56-87). [V from the patch context]

### 1.2 The third player's body

- `GearGameSP_Base.SuggestPawnClassFor` (DG:46987) hands out classes in this order:
  `GearPawn_COGMarcus` if no player has it, then `GearPawn_COGDom`, then `GearPawn_COGGear` (+0x0084).
  [V]
- **`GearPawn_COGGear` would spawn with no body.** The class is not abstract (ClassFlags
  0x088002B6), so the spawn itself succeeds. But no template in its chain sets a `SkeletalMesh` or a
  `PhysicsAsset`:
  - `Default__GearPawn.GearPawnMesh` sets neither.
  - `GearPawn_Infantry` sets only AnimTree, AnimSets and Translation.
  - `GearPawn_COGGear` sets only LightEnvironment.

  By contrast, Marcus sets `COG_Marcus_Fenix_CamSkel`. [V data] A player spawned as `COGGear` has
  no body and no skeleton, and native animation, weapon-socket or gore code may misbehave or crash.
  [I] The roadmap line "SuggestPawnClassFor already falls back to GearPawn_COGGear" is therefore not
  enough.
- **Two more differences from Marcus and Dom.**
  - `COGGear` and the Carmine classes inherit `bKillDuringLevelTransition = True` from
    `Default__GearPawn`. Only Marcus and Dom set it to False, which matters for chapter travel in
    Stage 2.
  - Their default inventory is the Lancer alone. Marcus and Dom also get a pistol and a shotgun.

  [V]
- **Ben Carmine is the right look, but it is only loadable in some maps.**
  `GearPawn_COGBenjaminCarmine` ("B. Carmine") uses mesh `COG_Minh.Minh_Kim` and helmet
  `Item_Helmet_COGTwo`, and its super is `GearPawn_COGGear`.
  - There is no `GearGameContent` package on disk. Content classes exist only as forced exports
    inside map packages, so `DynamicLoadObject` can find one only while a loaded map holds it.
  - The chapter numbers below come from `Coalesced_int_full.txt:5976-5986`. The class is in the
    persistent maps of chapter 2 (`SP_Hospital_P`), chapters 5-6 (`SP_Landown_P`), 7-9
    (`SP_Intervention_P`) and 10-11 (`SP_Rescue_P`).
  - For chapters 3-4 it is only in streaming sub-levels. Chapter 1 and Acts 3-5 do not have it.
  - Related classes: `GearPawn_COGRedShirt` (a generic COG soldier with the same mesh and a random
    helmet) is in 18 map packages. `GearPawn_COGCarmine` ("A. Carmine", the class the cheat at
    DG:24457 loads) exists only in `SP_Landown_5_S`.
  - The startup packages contain no `GearPawn_COG*` class.

  [V, header scan of all 455 SP, GearGame_P and GearStart packages:
  `out/re-coop/join-spawn/scan_cog_pawn_classes.txt`]

### 1.3 Extra players must not be Marcus or Dom

- Kismet's `SeqVar_Marcus` and `SeqVar_Dom` are native. Each walks `WorldInfo.PawnList` and returns
  the first pawn that `IsA` `GearPawn_COGMarcus` or `GearPawn_COGDom`, or that pawn's controller or
  vehicle (`sub_828F6040` / `sub_828F60C8` -> `sub_828F5FC8`). [V]
- `PawnList` is ordered newest first. `sub_825E2780` (recomp.214.cpp:10171-10240) unlinks a pawn if
  it is already in the list, sets its `NextPawn` (+460) to `WorldInfo.PawnList` (+868), and stores
  the pawn as the new head. [V code; that this is `AWorldInfo::AddPawn` and runs for every spawned
  pawn is I]
- So a Marcus clone spawned after the host would become "Marcus" for every Kismet teleport,
  cinematic toggle and inventory gift, and the host would be left out. [I] Extra players need a class
  of their own.
- Other hero lookups are safe for extra classes. `SeqVar_Player` already means all players:
  `bAllPlayers` is True by default and none of the 1092 instances in 450 scanned maps overrides it.
  `SeqCond_IsCoop` and `SeqEvt_CoopTouch` look only at the Marcus and Dom classes. [V]

### 1.4 System Link in the port blocks even two players

From `network.md`. Line numbers refer to the patched SDK tree in `out/sdk-build/rexglue-sdk` before
patch 0010, which addresses every item in this list (section 3, Stage 1, item 2). [V unless marked]

1. `XNetGetEthernetLinkStatus` returns 0 (`xam_net.cpp:600-606`). `HasLinkConnection()` is
   therefore false and `GearMenuGame.InitializeCoopSettingsFromProfile+0x0065` aborts. The link
   between the native `HasLinkConnection` and `sub_8282A5E0` is [I].
2. `XGISessionCreate` (msg 0x000B0010) writes neither `XSESSION_INFO` nor the nonce. The host's
   beacon response copies the host address, XNKID and XNKEY out of that buffer (`sub_828294A0`,
   from SessionInfo+20/+12/+56), so clients receive nothing usable.
3. `GetResolvedConnectString` (`sub_8282CC90`, OnlineSubsystemLive vtable at .rdata 0x82177814)
   always calls `XNetXnAddrToInAddr`, because init sets `GSocketSubsystem+108` to 1 (`sub_8283C760`
   at 0x8283C7B8). The stub returns 1 without writing anything, so the client never runs
   `open <ip>` (`OnJoinCoopGameComplete+0x003A`). `XNetRegisterKey` is not on the campaign path; it
   belongs to the party-beacon resolver that only versus parties use.
4. `XSocket::SendTo` stores host-order values in network-order fields (`xsocket.cpp:237-245`). Port
   1000 goes out as 59395 and beacon port 14001 as 45366, and unicast IPs are reversed. `RecvFrom`
   (`:202-210`) does not swap the port and writes `from` even when the call fails.
5. Every instance reports XUID `0xB13EBABEBABEBABE` and the name `User` (`user_profile.cpp:28-29`).
   `GearCampaignLobbyGame_Base.OnFindCoopMatch+0x00AA` drops results owned by the local XUID, so a
   peer's session looks like the player's own.
6. `XNetGetTitleXnAddr` returns 127.0.0.1 and MAC CC:CC:CC:CC:CC:CC on every instance
   (`xam_net.cpp:496-530`). `XNetRandom` fills its buffer with 0xBB (`:286-292`), so every query
   nonce is identical.
7. `sendto` never sets the guest last-error (`xam_net.cpp:1224-1249`), yet `LowLevelSend`
   (`sub_8283C020`) reads `WSAGetLastError` after a failed send. Once the link reports active, errors
   10051, 10053, 10054, 10060 and 10065 mark the connection as failed (0x8283C130).
8. Several instances on one PC all bind `0.0.0.0:1000` and `0.0.0.0:14001` with `SO_REUSEADDR`, and
   Windows delivers each unicast datagram to only one of them. [V binds; delivery semantics I]
9. `XSessionLeave` (0x000B0013) has no handler, every session shares one handle, and
   `XSessionGetDetails` leaves its buffer empty.

Not needed:

- The game imports none of `XNetCreateKey`, `XNetConnect`, `XNetGetConnectStatus` or
  `XNetInAddrToXnAddr`.
- It skips QoS for LAN sessions (`sub_8282B640`).
- VDP packets are at most 512 bytes and pass through unchanged.

## 2. Patch mechanism: script patches in guest memory

Decision: change UnrealScript bytecode and default objects in guest memory, from a host hook in
`src/` that runs right after the engine loads its native script packages. Do not ship a rewritten
`GearGame.xxx`.

### Why

- **The file route is expensive.** `GearGame.xxx` is one fully compressed stream of 506 independent
  128 KiB LZX blocks: 25,001,984 bytes on disk, 66,293,489 decompressed. [V] Its HashSec entry holds
  the SHA-1 of the decompressed package. [V for Core, Engine, GameFramework and GearGame] Changing
  the file needs one of two things:
  - an LZX encoder, which does not exist here (the SDK's `lzx.h` and libmspack only decode);
  - a 66 MB uncompressed copy plus a TOC change and a new hash, regenerated for every game version.
- **Memory has no such costs.** No hash, no TOC and no cached copy. The same `src/` code runs on
  Windows and Android (the Android build globs `src/*.cpp`).
- **Same-size edits are safe.** Bytecode has the same length in memory as on disk.
  - `ScriptSize` equals the on-disk length for all 10,331 GearGame functions and all 4,869 Engine
    functions.
  - Object references are 4 bytes: a package index on disk, a pointer in the 32-bit guest.
  - Names are 8 bytes (an FName).
  - Jump targets are absolute big-endian WORDs, so a same-size edit never moves an offset.

  [V]
- **References resolve at run time.** A patch can point at classes the package does not import:
  GearGame.upk has no Carmine import. [V]

### Design

- **Hook.** `REX_HOOK_RAW(sub_8283F780)`, on `FStartupPackages::LoadAll`, which has a single call
  at 0x8284075C in `sub_828406F0`. It calls `__imp__sub_8283F780`, then applies the patch table.
  - LoadAll loads Core, Engine, GameFramework, IpDrv, GearGame and OnlineSubsystemLive. No gameplay
    script has run yet, and every Stage 1 target lives in GearGame.
  - The generated `sub_` symbols are weak aliases (`gow2_pch.h`). A strong definition in `src/`
    replaces both the direct call and the function-table entry; the GTA IV ReXGlue port does this
    at scale.
  - Implemented in `src/gow2_script_patch_hook.cpp`. With clang and lld-link on Windows the
    override wins: in the link map `sub_8283F780` comes from `gow2_script_patch_hook.cpp.obj` and
    `__imp__sub_8283F780` from `gow2_recomp.172.cpp.obj`, and `llvm-objdump` shows the call that
    was `bl 0x8283f780` in `sub_828406F0` going to the hook. [V link; that it runs (the log line
    `SCRIPTPATCH startup script packages loaded`) and the Android link are not confirmed yet]
- **Lookup.** [V]
  - `GObjObjects` is the TArray at 0x83136860 and `GNames` the one at 0x83136834. An FNameEntry has
    its flags at +0 (bit 0 = UTF-16) and its text at +8.
  - UObject fields: Outer +40, Name +44 (index, number), Class +52.
  - A function is found by package, class, optional state and name. State functions sit one level
    deeper (Function -> State -> Class -> Package), so the walker accepts both depths and requires
    exactly one match.
- **Script array.** `UStruct::Script` is a TArray at +84. Data is at +84 [V, from `execJump`];
  Num at +88 and Max at +92 follow the standard TArray layout [I].
- **Edit kinds.**
  1. Same-size bytes.
  2. Object operand swap: write another object's address into a 4-byte operand.
  3. Default-object field write.
  4. Grown script. Allocate a new buffer with the game's own allocator (`GMalloc` at
     [0x8310B12C], vtable +4 = Malloc(size, align), called with align 8 as the package loader does at
     0x8226F610), fill it with the kept prefix and then the tail, and write Data, Num and Max. The
     row's own byte and operand edits go into the new prefix, so the old buffer is never written,
     and never freed. Allocating outside `GMalloc` risks a mismatched free when the array is
     destroyed. [I] The buffer is allocated while the group is still being checked, so a later
     failed check leaks that one block; nothing, the new buffer included, is written before the
     whole group has passed. An edit after the kept prefix would be lost, so it is a table error:
     the group is skipped with `+0x0007 is past the kept prefix +0x0006`, and
     `tools/script_patch.py` refuses to print such a row.
- **Guest addresses.** Every read, write and copy goes through `src/gow2_guest.h`, which places a
  guest address where the runtime does. On Windows the heap at 0xE0000000 and above sits 0x1000
  higher in host memory (`PhysicalHostOffset` in the SDK's `xmemory.h`; a `static_assert` in the
  hook keeps the two equal), and copies go byte by byte, so a range may cross 0xE0000000. `GMalloc`'s
  pool pages come from `XPhysicalAlloc` and land at 0xA0000000 today, but `sub_82AAA130` clears the
  large-page flag when [0x830D9B2C] is set (`rlwinm r5,r5,0,3,1`), and a 4 KiB-page block would come
  from the 0xE0000000 heap. [V for the flag change, I for the heap] The core test puts a grown script
  at 0xE0000000 and checks that nothing lands 0x1000 below it.
- **Fail closed.** The patcher validates a group completely before it writes any byte. Each row must
  pass all of these:
  - the function is found exactly once;
  - `Script.Num == ScriptSize`, and before a grown write also `Max == Num`;
  - every expected byte matches;
  - every pointer operand equals the object resolved by name.

  Bytes that are already patched count as applied. Any failure skips the whole group, leaves the
  game retail and logs `SCRIPTPATCH <group>: skipped (<row>: <reason>); the game stays retail`.
  Success logs one line per row plus a hash of the applied set.
- **Lookup as built.** Names match without regard to case, as in UE3, and a name's text must occur
  once in `GNames`. An object matches when its own name and each outer spell the path exactly, the
  outermost outer is null, and the name of its class is the expected one (`Function`, `Class`,
  `IntProperty` and so on). The set hash is FNV-1a 64 over the rows and N, never over guest
  addresses, so it is the same on every run and platform. On Windows a fault while reading guest
  objects (a wrong layout) is caught and skips the group; Android has no such guard.
- **Setting.** `gow2_coop_players` (int). The default 2 means off. Values from 3 to 255 enable the
  Stage 1 group with N players; 255 is the ceiling because the cap is an `IntConstByte`. Two debug
  settings: `gow2_coop_marcus_clones` uses row D-1 in place of S1-2, and `gow2_coop_patch_fail_test`
  corrupts the first expected byte of the table (S1-1 +0x0000), which must skip the group.
- **Offline tool.** `tools/script_patch.py` grew out of `out/re-coop/script_patch_poc.py` and
  `join-spawn/verify_patches.py`. It owns the rows: `--cpp` prints
  `src/gow2_script_patch_table.cpp` (a test fails when the two differ), and `--check <folder>` runs
  on the player's own decompressed packages and, for each row of the group and for D-1:
  - checks the old bytes, the context bytes and every object operand (as a package reference);
  - applies the edit to a copy and re-decompiles it;
  - confirms the function still decodes to its end and, for the grown row, that no jump in the
    kept prefix lands in the replaced tail;
  - checks the default object's value, then prints the C++ table.

  Its tests (`tools/tests/test_script_patch.py`) and the C++ test target `gow2_script_patch_test`
  (`tools/tests/script_patch_core_test.cpp`, the patcher core on a synthetic guest memory) use
  synthetic bytecode only. The repo commits byte strings and offsets, never game data.
- **Build wiring.** `src/gow2_script_patches.cpp` (the portable core), `gow2_script_patch_table.cpp`
  and `gow2_script_patch_hook.cpp` (hook, settings, `GMalloc`, logging; the only part that needs
  the runtime) are in `GOW2_SOURCES` in `CMakeLists.txt`, which is an explicit list. Android picks
  them up by glob.
- **Peers.** In-memory patches do not change package GUIDs, so the engine will not notice a host and
  a client running different patch sets. Stage 1 logs the patch-set hash; Stage 4 puts it in the
  session data.

### Fallback B: only for edits that need new names or imports

Serve the decompressed, edited package uncompressed through `GOW2_VFS_OVERRIDES`, together with an
edited `Xbox360TOC.txt`:

- Keep the GearGame line and its sector, but set column 2 (uncompressed size) to 0. The 360 loader
  decides compression only from that column (`sub_827F5B58` returns -1 for 0), and `FileSize` comes
  from the TOC.
- Write the new file's SHA-1 into the `geargame.xxx` HashSec entry.

[V loader paths; never run] The file route touches LZX blocks 63, 64, 67 and 69 and costs a 66 MB
cached copy per game version.

### Related finding (fixed in Stage 1)

The game reads and hashes each edited Coalesced bundle at its TOC size. `sub_82225018` creates the
file reader, takes its `TotalSize()`, allocates and serializes that many bytes and wraps them in
FBufferReaderWithSHA (vtable 0x820954C0); `sub_827F5508` sets the reader's size (+148) from the TOC
`FileSize` while the flag at 0x830C9734 is set. [V] The served bundles carried 4 KiB of zero
padding, so HashSec received the SHA-1 of a file about 2.1 KB longer than the bytes the game
hashes. That most likely raised the DISCERR that every run with Coalesced overrides logged and that
`DiscErrorNoop` parks on a worker thread. [I, from code plus logs] `src/gow2_coalesced.cpp` now
pads each edited bundle to exactly its `FileSize` in `GearGame/Xbox360TOC.txt` (the original file
size when the TOC has no entry; every retail Coalesced TOC size equals its file size) and does not
serve a bundle whose edit is longer. The comment in `src/gow2_xam_input_guard.cpp` that blamed a
missing disc drive is corrected. Still to confirm in a run: the DISCERR line is gone.

## 3. Staged plan

### Stage 1: four players in Act 1 on one PC over loopback

Goal: a host plus three clients, as four `gow2.exe` instances on 127.0.0.1-127.0.0.4, all spawned
and playing in Act 1 chapter 2. Players 3 and 4 spawn as Ben Carmine, or as RedShirt if Ben is not
loaded.

Why chapter 2: its persistent map `SP_Hospital_P` holds `GearPawn_COGBenjaminCarmine`, so the class
stays loaded on the host and on every client for the whole chapter. Chapter 1 lacks it.

Work:

1. **Script patches.** The group in section 4, behind `gow2_coop_players`. Done; section 2 names the
   files.
2. **SDK patch `0010-system-link-sessions` (M2a), applied after 0002-0007:**
   - SendTo/RecvFrom: fill `sockaddr_in` in network order, swap the received port, and write
     `from` only when the call succeeded.
   - `XNetGetEthernetLinkStatus`: return 0x0B (active, 100 Mbps, full duplex) when System Link is
     turned on, and 0 (cable unplugged) otherwise, as before.
   - `XGISessionCreate`:
     - When flags bit 0 (HOST) is set, write into `XSESSION_INFO` a random XNKID with the
       system-link marker, the host XNADDR and a random XNKEY, and write a random nonce. [V for the
       bit: the join path `sub_8282CA88` clears it]
     - When bit 0 is clear, leave the buffer untouched, because the joiner passes the search result
       in it.
     - Give each session its own handle and slot count.
     - Return success for 0x000B0013 (leave), and fill `XSessionGetDetails`.
   - `XNetXnAddrToInAddr`: copy `xnaddr->ina` (raw, in network order) to `*in_addr` and return 0.
     Making `XNetRegisterKey` succeed is harmless but optional.
   - Last-error: `sendto`, `setsockopt` and `bind` set WSA codes. On Windows, turn off
     `SIO_UDP_CONNRESET` on UDP sockets; otherwise an ICMP port-unreachable becomes error 10054 and
     drops the connection once the link is active.
   - `XNetRandom`: use a real RNG, with a deterministic setting for tests.
   - Identity settings:
     - `user_xuid` (persisted, random per install, bits 54-55 clear) and `user_name`;
     - `net_local_ip`, which feeds `XNetGetTitleXnAddr.ina`;
     - a per-instance locally administered MAC.
   - One-PC settings:
     - `net_bind_ip` rewrites INADDR_ANY binds to the instance's 127.0.0.x address.
     - `net_broadcast_targets` sends each broadcast as one unicast copy per listed peer, skipping
       self.

     A socket bound to a specific address never receives broadcasts, so this mode requires the
     fan-out. [I] Both settings stay off on a real LAN.

   Implemented in `sdk/patches/0010-system-link-sessions.patch`. Settings:

   | Setting | Default | Effect |
   |---|---|---|
   | `net_link_active` | false | true: `XNetGetEthernetLinkStatus` returns 0x0B and System Link works. false: it returns 0 (unplugged cable), as before 0010. `gow2_coop_players` 3 or more turns it on unless `gow2.toml` or the command line set it |
   | `user_xuid` | empty | XUID the game sees. Empty with the link on: the one in `system_link_xuid.txt` in the user data folder, created on the first such run as `0xB13E` plus 48 random bits. Empty with the link off: the fixed offline XUID 0xB13EBABEBABEBABE, and no file is created |
   | `user_name` | `User` | Gamertag, up to 15 characters |
   | `net_local_ip` | empty | `XNADDR.ina`. Empty: `net_bind_ip` when set, else the address of the adapter that holds the default route, else 127.0.0.1 (with a warning that other PCs cannot join). The log line `[netlink] link active: local <ip> (<where from>)` names it; set it by hand when that is not the LAN address |
   | `net_bind_ip` | empty | Rewrites `0.0.0.0` binds and logs `[netlink] bind 0.0.0.0:1000 -> 127.0.0.2:1000 (net_bind_ip)` |
   | `net_broadcast_targets` | empty | Unicast copies of every 255.255.255.255 send, skipping this instance (`net_bind_ip`, else `net_local_ip`); the list is logged once |
   | `net_random_seed` | 0 | Non-zero: `XNetRandom`, XNKIDs, XNKEYs and nonces come from a SplitMix64 stream with this seed |

   Every one of these settings needs a restart. The network ones are read once, when the kernel
   starts, and the game threads only read that copy: the settings panel cannot move the advertised
   address under a session that already handed it out, and no thread reads a string the panel is
   rewriting.

   Decisions taken while implementing:

   - System Link is opt-in. With the defaults a solo game behaves as before 0010: the link reports
     down, so the game creates no LAN session and should bind only UDP 0.0.0.0:1000 as before [I;
     Stage 1 test run 3 checks it]; it sees the offline XUID, and nothing is written to the user
     data folder. What still differs without the link are fixes that do not depend on it: socket
     addresses in network order, a random `XNetRandom` and a per-instance MAC in
     `XNetGetTitleXnAddr` instead of CC:CC:CC:CC:CC:CC.
     `gow2_coop_players` 3 or more turns the link on in `OnPreSetup`, before the kernel reads it, and
     logs `SCRIPTPATCH coop: gow2_coop_players=4 turns System Link on (net_link_active=true)`; an
     explicit `net_link_active` in `gow2.toml` or on the command line wins. Two-player System Link
     needs `--net_link_active=true`.

   - Saves and profile files stay keyed by the old fixed identity. `UserProfile::xuid()`
     (0xB13EBABEBABEBABE) still names the content folder and `name()` ("User") the profile-settings
     folder. The game sees `network_xuid()` and `gamertag()` instead, through `XamUserGetXUID`,
     `XamUserGetSigninInfo`, `XamUserGetName`, `XamUserGetGamerTag`, profile-setting reads and
     `XamContentGetCreator`. Changing `user_xuid` or `user_name` never moves a save. Whether the game
     accepts an existing save under a new XUID is a test item; `user_xuid=B13EBABEBABEBABE` restores the
     old value. With the link off the game keeps seeing 0xB13EBABEBABEBABE.
   - The MAC is `02` plus five bytes of SplitMix64 over the XUID and the IP, so instances on one PC
     differ even when they share a XUID.
   - Session handles are 0xCAFE0004, 0xCAFE0008 and so on. Each session object is a zeroed 16-byte
     system-heap block, and `ObDereferenceObject` skips these blocks: the title dereferences the object
     after every XSession call (for example `sub_829A5810` and `sub_829A3C70`). Unknown handles still
     get the old 0xDEADF00D.
   - Joins and leaves keep a member list per session; slot limits are not enforced. Leave handles
     both forms, but the title only uses the remote one (`sub_829A3C70` passes a XUID array).
     `XSessionGetDetails` fills the 128-byte `XSESSION_LOCAL_DETAILS` and as many 16-byte members as the
     caller's size allows, right after it. The only caller, `sub_828333C8`, passes 640 bytes.
   - `XNetRegisterKey`, `XNetUnregisterKey` and `XNetUnregisterInAddr` return 0.
   - On Android the patch has only passed an NDK arm64 `-fsyntax-only` check; it has not been built
     or linked. errno maps to WSA codes for `sendto`, `setsockopt` and `bind` only, and
     `XSocket::SetOption` and `socket_ioctl` still hand the 360's SOL_SOCKET (0xFFFF) and FIONBIO
     (0x8004667E) to the POSIX calls unchanged. Android System Link needs the Stage 4 mappings.

   With the link on, a solo campaign also creates its LAN session and answers System Link queries
   on UDP 14001, as a 360 with a cable would, so anyone on the LAN can find it. [I] That is why the
   link is off unless co-op asks for it.
3. **Tools.** Done: the package and script tools are in `tools/ue3/` (one line each in its
   README), next to `tools/script_patch.py`.

Out of scope for Stage 1 (known to break; see Stage 2): chapter transitions, the death of any
player, a player leaving, cinematics with extra players present, vehicles and co-op splits.

#### Stage 1 test

Offline:

1. Run `python -I tools/script_patch.py --check <folder>` on your own decompressed Core, Engine,
   GameFramework and GearGame packages. Every old byte must match, and every patched function must
   decode to its end: AtCapacity 0x11, ChangeCoopInviteSetting 0xB2, lobby InitGame 0xA9, SP
   InitGame 0xCA, and SuggestPawnClassFor 0x117 with the new tail. The default object must hold 2.
   Passed on 2026-10-14 against the retail packages, D-1 included; the jumps in the kept prefix of
   SuggestPawnClassFor go to 0x48, 0x67 and 0x84.
2. Unit tests on synthetic bytecode: `python -m pytest tools/tests/test_script_patch.py` and the
   `gow2_script_patch_test` target. A wrong old byte, a wrong `Num`, a wrong pointer operand, or a
   function found twice must each skip the whole group, and so must an edit past the kept prefix
   of a grown row. A grown row's own edits must land in the new buffer, and a grown script placed
   at 0xE0000000 must be written where the game reads it. Both pass (25 tests, 88
   checks).

One instance, no network:

3. Run with `--gow2_coop_players=2` (the default). The log shows `SCRIPTPATCH startup script
   packages loaded` and `SCRIPTPATCH coop: off (gow2_coop_players=2)`, and play is unchanged.
4. Run with `--gow2_coop_players=4`. The log shows one `SCRIPTPATCH coop S1-n <function>: ...`
   line per row (S1-2 `script grown to 279 bytes`) and `SCRIPTPATCH coop: 6 rows applied for 4
   players, set hash <16 hex digits>`. Play the start of chapter 1 solo: nothing changes, because
   the patches only act on joins.
5. Add `--gow2_coop_patch_fail_test=true`. The log shows `SCRIPTPATCH coop: skipped (S1-1: +0x0000
   expected FB, found 04); the game stays retail`, and the game stays retail.

With SDK patch 0010, the single-instance runs also check its defaults. Run 3 (no co-op, no network
settings) must look like a run before 0010: the log shows `[netlink] link reported down
(net_link_active=false): no System Link` and `Signed-in player: XUID B13EBABEBABEBABE, gamertag
'User'`, an existing save loads, no `system_link_xuid.txt` appears in the user data folder, no
Windows Firewall prompt appears beyond any the port already raised for UDP 1000, and with
`--system_link_trace=true` the trace shows `XNetGetEthernetLinkStatus ... ret=0x0`, a single bind
(`0.0.0.0:1000`) and no `XGISessionCreate`.

Run 4 (`--gow2_coop_players=4`) also logs `SCRIPTPATCH coop: gow2_coop_players=4 turns System Link
on (net_link_active=true)`, `[netlink] link active: local <ip> (<where from>)` and a `Signed-in
player` line with the per-install XUID (`B13E` plus 12 hex digits), creates `system_link_xuid.txt`,
and with `--system_link_trace=true` the solo campaign logs `XGISessionCreate ... flags=0x00000021 ...
host=true known=true ... host_ip=<that ip>` and play is unchanged. Load an existing save in this run
too: it is the first run under a new XUID.

Network (needs SDK patch 0010):

6. Two instances: host on 127.0.0.1, client on 127.0.0.2, unique XUIDs, fan-out targets set. The
   client finds the session (the beacon names 127.0.0.1), runs `open 127.0.0.1` and takes over the
   AI Dom. This is the two-player baseline; the script patches play no part yet. Capture with
   `system_link_trace`. Settings: host `--net_bind_ip=127.0.0.1`, client `--net_bind_ip=127.0.0.2`,
   both `--net_link_active=true --net_broadcast_targets=127.0.0.1,127.0.0.2,127.0.0.3,127.0.0.4
   --system_link_trace=true`,
   and a different XUID per instance (`--user_xuid=...`, or a separate `--user_data_root`, which also
   separates the saves).

   Done 2026-10-14 [V] with the crossplay build (patches 0010 and 0011): the client found the host in
   the System Link Browser, joined the lobby, loaded chapter 2 with the host and spawned as Dom on
   Right Trigger. Harness: scripts/run-instances.ps1 and scripts/instance-input.ps1; steps and log
   lines in docs/NETWORK_PLAY.md, "Testing on one PC". Separate user data comes from a per-copy
   LOCALAPPDATA, because the app sets user_data_root from %LOCALAPPDATA% and ignores --user_data_root.
7. Host and player 2 play into Act 1 chapter 2. One way to get there is to play chapter 1 first; a
   profile with chapter 2 unlocked also works. `GearGameSP_Base.InitGame+0x0063` reads `?chapter=`,
   but nobody has tested passing it.
8. Instances 3 and 4 (127.0.0.3 and .4) search and join. Expect each of these:
   - The host keeps answering the beacon until four players are in.
   - PreLogin and Login accept both joiners.
   - Each joiner views the AI Ben (`PostLogin` finds a pawn of his class), spawns on Right Trigger
     as Ben Carmine within 1024 uu of a teammate, and joins squad Alpha. The host stays leader.
9. Play 10 minutes of combat with four players. Everyone sees the others move, shoot and take
   damage, with no crash and no desync.
10. A fifth instance must not see the game. If forced to connect, it must be refused with
    `MaxedOutMessage`.
11. Control run: repeat step 8 with `gow2_coop_players=2` on the host. Instance 3 must be refused.
12. Record per-client net stats as the bandwidth baseline (7000 bytes/s cap, see Stage 3).

Join when no cinematic is playing: a remote join more than 15 s into a level cancels the running
cutscene for everyone (Stage 2, item 5).

### Stage 2: campaign systems

Each item gives the evidence and then a candidate fix. Script changes are sized with
`tools/script_patch.py`; most of them grow their function and need edit kind 4.

1. **Chapter travel kills extra players and ends the game.**
   - `UWorld::CommitMapChange` (`sub_824A1AC8`; recomp.18.cpp:6466, 7942, 8565) destroys every
     persistent-level actor with `bKillDuringLevelTransition` set (Actor+0xB0, mask 0x40000000).
   - The extra's pawn dies. `PawnDied` sends his PC to state `Dead`, which arms `ProcessGameOver`
     for 5 s.
   - `PostCommitMapChange` (DG:46711) restarts only local PCs that are not in `Dead`.
   - `SaveCheckpoint+0x0082` refuses to save while that timer runs. The reload therefore lands
     before the transition and can loop.
   - 24 SP maps contain `SeqAct_CommitMapChange`.

   [V chain; outcome I]

   Fix: clear the flag on player pawns at spawn (in `SpawnDefaultPawnFor`, after the `Spawn` at
   +0x0061). Do not clear it on the `COGGear` default object: AI squadmate classes loaded later
   from map packages would inherit the change and survive transitions they are meant to die in.
   [I]
2. **Game-over rules.**
   - In SP, any human PC that enters `Dead` arms `ProcessGameOver` (5 s, or 2 s in a crate section;
     `GearPC.Dead.BeginState`, DG:35009), which sends `ClientGameOver` to everyone.
   - `Dead.EndState` (DG:35040) calls `ProcessGameOver` at once when a remote PC is destroyed while
     the timer is armed.
   - `EndGameCondition` (DG:64261) also ends the game when an AI-controlled pawn in the player squad
     dies, on difficulties with `bAllowAICOGDeathWhenDBNO`.
   - During co-op splits a human never counts as a reviver (`TeamHasAnyChanceOfSurvival+0x0258`).

   [V]

   Fix: extra players respawn near a teammate with a timed `RestartPlayer` instead of ending the
   game, while the heroes keep the original rule. During splits, allow a human of the same squad to
   revive.
3. **Leaving.**
   - `GearPC.Destroyed` (DG:31254) hands the pawn of any leaving `COGGear`-derived player to a new
     `GearAI_Dom` (+0x01C7) and fires every `SeqEvent_CoopPlayerLeft` (+0x031A).
   - The 14 handlers, in 5 maps, all act on the real Dom: teleport, god mode, map state.
   - A rejoining extra player spawns a fresh pawn, and the old AI-driven one stays in the world.

   [V]

   Fix: keep the AI takeover and the event for Dom only. For an extra player, `UnPossess` first and
   then `Destroy` the pawn. A gib, or a `Destroy` while still possessed, goes through `PawnDied` and
   ends the game.
4. **Teleports.** 371 `SeqAct_Teleport` target links move only Marcus and/or Dom (213 + 157 + 1);
   48 target Player. [V counts]

   Fix: after Marcus's pawn is teleported, place every extra player who is not in a vehicle near the
   destination with `FindAvailableTeleportSpot`. Skip moves shorter than about 1500 uu, so local
   placements do not drag extras along. Open: whether several pawns sent to one destination fail
   silently (`Pawn.OnTeleport`, DE:31182).
5. **Cinematics.**
   - 151 of 254 `SeqAct_ToggleCinematicMode` targets are hero-only.
   - `GearPC.SetCinematicMode` (DG:34716) hides the pawn, freezes it and sets `bInvisible`, which
     makes AI ignore it. During those scenes the extra players are the only targets AI can see.
   - Joining: a remote join more than 15 s into a level runs `CANCELCINE` for everyone
     (`PostLogin+0x040D`).
   - The joiner's cinematic flags are synced before his pawn exists (+0x0395 runs before
     +0x03CB), so the new pawn is not hidden.

   [V]

   Fix: mirror Marcus's cinematic mode to every extra PC (never mirror Dom's), including right
   after an extra spawns.
6. **Co-op splits.** `SeqAct_NotifyCoopSplit`, present in 11 maps, teleports Marcus and Dom onto
   separate paths and strands the extra players. [V]

   Fix: send the extras alternately with each hero, and copy the hero's `SquadName` when Kismet runs
   `ChangeSquad` on him.
7. **Checkpoints.**
   - `GearPC.OnCheckpoint` (DG:32633) pulls far players to the host, except at 29 checkpoints whose
     `TeleportClasses` lists only Marcus and Dom (plus `Vehicle_Jack`).
   - `ApplyCheckpointRecord+0x0577` gives any crate carrier who is not Marcus the Dom slot.
   - `CheckpointLoadComplete` (DG:47164) restarts every PC without a pawn.

   [V script] Not traced: how native code matches records to PCs (by `MyGuid` at GearPC+0xC4C, by
   order, or by class) and who sets `bCheckpointDummy`.

   Fix: treat the extra classes as matching `TeleportClasses`.
8. **Vehicles.** [V]
   - The Centaur has 2 seats, and script checks `Centaur.Seats.Length == 2` (DG:108043).
   - Each of the two Ride-Reavers has 2 seats.
   - The Brumak has a single `HumanGunner`.

   Fix: put extras into free seats via `TryToDrive`; when none is free, use a hidden rider mode
   bound to the vehicle.
9. **Loadout and look.** [V]
   - `SeqEvent_ChapterPoint` gives `MarcusInventory` only to Marcus pawns.
   - 25 `GiveInventory` links target heroes only.
   - Extra players start with a Lancer.

   Chapters without Carmine or RedShirt in the persistent map (1, 3-4 and Acts 3-5) need a class
   that is always loaded and is not Marcus or Dom. Candidates: assign a mesh to `COGGear` instances
   at spawn (the Marcus mesh is always loaded), or load the class from a sub-level and manage its
   lifetime. Open.
10. **Join wait.** A remote joiner whose class already has a pawn in the world (here the AI Ben)
    waits in `PlayerWaiting` until he presses Right Trigger (`PostLogin+0x03A4`,
    `GearPlayerInput.HandleButtonInput_RightTrigger+0x0060`). Optional fix: spawn non-Dom joiners
    immediately.

#### Stage 2 test

Four players, one scenario per item:

1. Cross the chapter 2 -> 3 transition: nobody dies, no game over, all four keep their pawns.
2. Kill an extra player: he respawns and the game continues. Kill the host: game over, as in
   retail.
3. Have an extra player leave and rejoin: Dom is untouched, no `CoopPlayerLeft` behaviour fires, and
   no orphan pawn remains.
4. Load a checkpoint: all four are restored.
5. Play through a cinematic: the extras are hidden and reappear next to Marcus.
6. Play one co-op split from the list in `campaign-systems.md` section 9 (for example
   `SP_Outpost_07a_S`).
7. Play the Ride-Reaver section (`SP_Forest_S_01`) and the Centaur section
   (`SP_MountKadar_Vehicle`).

Each scenario also runs with `gow2_coop_players=2` to confirm retail behaviour is unchanged.

### Stage 3: 8-10 players and bandwidth

- **Caps.** The same Stage 1 rows with N = 8 or 10.
- **Bandwidth.** [V unless marked]
  - A LAN match skips `UpdateNetSpeeds` (DE:21289, +0x003E), so nothing is divided by the player
    count.
  - The host's `AdjustedNetSpeed` stays at `MaxDynamicBandwidth = 7000`
    (`GearGameSP_Base.InitGame+0x00BC`: `0F 01 [GameInfo.MaxDynamicBandwidth] 1D 00 00 1B 58`), and
    `ServerSetNetSpeed` clamps to it.
  - The client connects with `open a.b.c.d`, without `?LAN`, so it uses `ConfiguredInternetSpeed`
    (7000), not `ConfiguredLanSpeed` (`sub_827D4108`).
  - Raise `MaxDynamicBandwidth` (a same-size IntConst edit), plus `ConfiguredInternetSpeed` and the
    server rate caps `MaxClientRate` / `MaxInternetClientRate` in Coalesced. Nobody traced which of
    the two server caps clamps.
  - Use the Stage 1 measurements as the baseline before choosing values.
- **Tick rate.** Coalesced sets `LanServerMaxTickRate = 35`, `NetServerMaxTickRate = 45` and
  `bClampListenServerTickRate = TRUE`. [V values; semantics I]
- **Packet size** is no issue: at most 512 bytes per packet.
- **UI limits.** [V]
  - Downed-teammate icons have 4 coordinates per split (`AssessSquadCoords`).
  - The spectator battle cam has 2 paths, gated to ControllerId 0 and 1.
  - `VoiceTaccomIconPos` has 2 slots.
  - The campaign lobby shows 2 rows.
  - Assess mode itself iterates over all pawns.
- **Spawn spots.** The search is a BFS within 1024 uu of a teammate's anchor that skips cover
  markers and occupied spots, falls back to a PlayerStart, and spawns with `bNoCollisionFail`. [V
  code] With more players, nearby spots may run out. [I]
- **Vehicles and game-over rules** scale worse as N grows and need the Stage 2 fixes.

#### Stage 3 test

Use 2-4 PCs with at most two instances each, on M2b settings: real LAN addresses, subnet-directed
broadcast and `net_bind_ip` off.

1. Join 8, then 10 players.
2. A chapter with combat and one checkpoint reload.
3. Measure per-client bytes/s and the server tick before and after the bandwidth changes.

Pass when there is no rubber-banding at 8 players and nobody else is refused below N.

### Stage 4: PC-Android cross-play over System Link

Status (2026-10-15): SDK patch 0011-network-crossplay implements the Stage 4 networking.
It now sits on the opt-in 0010 (6a8e519): System Link needs `net_link_active=true` on every
copy, PC and phone (docs/NETWORK_PLAY.md). It builds for Windows and passes its unit tests
(70 System Link cases: "All tests passed (1439 assertions in 70 test cases)",
out/logs/unit-tests-4.log). The loopback run below used the 0011 before this rebase. It has
not run on the phone yet. The Android app adds the INTERNET, ACCESS_NETWORK_STATE,
ACCESS_WIFI_STATE, CHANGE_WIFI_MULTICAST_STATE and WAKE_LOCK permissions, holds a Wi-Fi
MulticastLock and a low-latency WifiLock (WIFI_MODE_FULL_LOW_LATENCY) from onCreate to
onDestroy, and sets REX_DETECTED_LOCAL_IP to the Wi-Fi or hotspot address. For setup,
see docs/NETWORK_PLAY.md. On 2026-10-14 two PC copies on loopback played chapter 2
together with 0011 (docs/NETWORK_PLAY.md, "Testing on one PC"); the PC-phone tests are
still open. The text below the table is the original design; it is unchanged.

| Setting | Default | Effect |
|---|---|---|
| `net_port_offset` | 40000 | Guest ports below 1024 use port + offset on the host (the game's UDP 1000 becomes 41000; beacon port 14001 is unchanged). Every peer uses the same value. |
| `net_local_ip` | empty (new meaning) | a.b.c.d or a.b.c.d/prefix (prefix 24 by default; the prefix defines the local subnet). Empty means REX_DETECTED_LOCAL_IP, then `net_bind_ip`, then the default-route address, then 127.0.0.1. |
| `net_peers` | empty | Comma-separated host[:port] list (IPv4 or DNS names). Every broadcast the game sends also goes to each peer as a unicast copy. |
| `net_public_address` | empty | host[:port] (IPv4 or DNS name) advertised as XNADDR inaOnline / wPortOnline. Set `net_allow_peers` with it. |
| `net_allow_peers` | empty | IPv4 addresses, a.b.c.d/n ranges or DNS names. When set, datagrams from any other address are dropped before the game sees them; loopback and the local subnet always pass. When empty and `net_peers` or `net_public_address` is set, only the peers and the local subnet pass (fail closed, also while a name is still unresolved). When all three are empty, everyone is allowed, as on a 360 LAN. |

All five are read once at the first use after the kernel starts and need a restart, like the
0010 settings: the game threads read an immutable snapshot, never the cvar strings.

Decisions taken while implementing:

- The guest's beacon reply is a broadcast, not a reply to the sender [V]:
  sub_828294A0 (LAN response builder) sends with sub_827E3548
  (out/re-coop/sub_828294A0.txt line 98), and sub_827E3548 passes the beacon object
  itself as the destination address (generated/default/gow2_recomp.41.cpp:15762,
  `mr r7,r3`), which holds the 255.255.255.255:14001 address set at beacon init
  (sub_827E33C0).
- A broadcast from the beacon socket (guest port 14001, UE3's LanAnnouncePort) also
  goes, as a unicast copy, to the senders heard on that socket in the last second:
  at most 4 per broadcast (the player cap), newest first, and at most 2 copies per
  second to any one sender (a token bucket). That reaches a client behind NAT and a
  Windows PC on a Public network (Windows Firewall lets a unicast reply to its own
  broadcast through for about 3 seconds, but drops a broadcast reply). The caps keep
  an internet host from becoming a UDP reflector: one spoofed 16-byte query would
  otherwise send the 190-byte beacon to up to 32 addresses. Other sockets get no
  copies.
- A datagram identical to one from the same source in the last 250 ms is dropped, so
  a client that also hears the broadcast does not see the reply twice.
- On Android and Linux the runtime sends 255.255.255.255 as the subnet's directed
  broadcast, because Android routes the limited broadcast through the default
  network, which can be mobile data. Windows sends both the directed broadcast and
  255.255.255.255, because it sends the limited broadcast out of one adapter only, and
  on this PC that is not Ethernet: `Find-NetRoute -RemoteIPAddress 255.255.255.255`
  gave "Ethernet 3" (192.168.56.1, a VirtualBox host-only adapter, metric 25 like
  Ethernet) on 2026-10-15 and the Hyper-V Default Switch in an earlier check, while
  192.168.10.255 routes through Ethernet. A loopback or unknown subnet keeps
  255.255.255.255 alone.
- Datagram sockets receive into a 64 KiB host buffer. The allow list runs first, so a
  blocked sender's bytes never reach guest memory, and a datagram larger than the
  guest buffer cannot raise WSAEMSGSIZE before its source is checked; an allowed one
  is truncated and fails with WSAEMSGSIZE, as Winsock does. Stream sockets skip the
  allow list and the duplicate filter (TCP bytes are not datagrams).
- DNS: each name keeps its last good addresses when a lookup fails (logged as
  "cannot resolve <name>; keeping ..."); a name that never resolved contributes
  nothing, and the allow list stays closed rather than open. Lookups run on one
  background thread every 5 minutes, only while the link is on.
- The Android app uses REX_DETECTED_LOCAL_IP instead of REX_NET_LOCAL_IP, because
  REX_NET_LOCAL_IP would set the cvar itself and outrank the config file.
- `XNetXnAddrToInAddr` counts a peer as local when its ina is loopback, or when its
  ina is on our subnet and (it has no inaOnline, or a datagram arrived from that ina
  in the last 10 minutes). Local peers get ina, others get inaOnline (or ina when
  inaOnline is 0). This handles two homes that both use 192.168.1.x.
- An empty `net_local_ip` falls back to REX_DETECTED_LOCAL_IP, then `net_bind_ip`, then
  0010's routed address (UDP connect towards 192.0.2.1, which sends nothing), with a
  /24 prefix. The settings line names the source: "[netlink] link active: local
  192.168.10.10/24 (default-route adapter), ..., LAN broadcast 192.168.10.255,
  255.255.255.255, port offset 40000".

Still open: the version gate; the Stage 4 tests.

**Wire compatibility.** Guest code builds every packet byte in big-endian guest memory, and the
runtime passes the bytes through raw, so both platforms put the same bytes on the wire. [I,
plausible] The values the runtime supplies (XNADDR, XNKID/XNKEY, XUID and `XNetRandom` output) must
be generated the same way on both. Floating-point differences between SSE and NEON only cause
server corrections, because UE3 is server-authoritative. [I]

**Android and POSIX work.** [V gaps in code unless marked]

- **Manifest and Wi-Fi.** The manifest declares only `VIBRATE` today.
  - Add INTERNET, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE and CHANGE_WIFI_MULTICAST_STATE.
  - Hold a `WifiManager.MulticastLock` and a low-latency WifiLock while network play is possible.
  - Discover the wlan0/ap0 address and pass it as `net_local_ip`.
- **`setsockopt` and `getsockopt`.** Map the Winsock levels and options:
  - SOL_SOCKET 0xFFFF -> 1
  - SO_REUSEADDR 4 -> 2
  - SO_BROADCAST 0x20 -> 6
  - SO_SNDBUF 0x1001 -> 7
  - SO_RCVBUF 0x1002 -> 8
  - SO_LINGER 0x80 -> 13

  Convert linger as two big-endian u16 into two ints. That also fixes the u32 swap on Windows.
- **`ioctlsocket`.** Map the 360's FIONBIO (0x8004667E) to `O_NONBLOCK` and handle FIONREAD
  (0x4004667F), both endian-correct.
- **`select`.** Compute nfds from the native descriptors; UE3 passes 0.
- **errno to WSA codes.**
  - EAGAIN -> 10035
  - EMSGSIZE -> 10040
  - ENETUNREACH -> 10051
  - ECONNRESET / ECONNREFUSED -> 10054
  - EHOSTUNREACH -> 10065
  - EADDRINUSE -> 10048
  - EACCES -> 10013
- **Ports.** Android apps cannot bind below 1024 [I, platform rule], and the game binds UDP 1000.
  Remap guest ports below 1024 to port + offset (for example 1000 -> 41000) on every platform,
  symmetrically in bind, sendto, recvfrom and getsockname. The remap must be on by default
  everywhere before cross-play. Also implement `getsockname` and `getpeername`, which are stubs
  today.
- **SDK tree.** The Android SDK tree must carry 0003 and 0010. Patches 0005 and 0006 are not there
  yet (see `docs/ANDROID.md`).
- **Version gate.** Both ends run the same game version. The patch-set hash goes in the session
  data, and a join is refused on mismatch.

#### Stage 4 test

1. Run a PC host with an Android client, then an Android host with a PC client, on one Wi-Fi
   network: discovery, join and 30 minutes of chapter 2.
2. Mix two PCs and two phones for four players.
3. Join with a patch-set mismatch: the join must be refused.
4. Repeat with the phone as a Wi-Fi hotspot (`ap0`).

## 4. Stage 1 patch list

Everything below is one group, enabled by `gow2_coop_players = N` (N >= 3; Stage 1 uses 4). The
patcher validates every row and then writes every row. A single failed check leaves the whole game
retail. The rows as built are `src/gow2_script_patch_table.cpp`, printed by `tools/script_patch.py`;
they also check the bytes between the edits (for example S1-1 +0x09 `16 04 3A`).

Notation:

- Bytes are as stored in the decompressed package, and offsets count from the start of the
  function's script.
- `[X]` is a 4-byte object operand: a package index on disk, an address in memory. It must equal
  the address of object X, resolved by name.
- Disk values are given in brackets so the offline tool can check them. "Context" bytes are checked
  but not changed.

| Id | Target (ScriptSize) | Offset | Old | New | Fail-closed check | Needed |
|---|---|---|---|---|---|---|
| S1-1 | `GearGame.GearGameSP_Base.AtCapacity` (17) | +0x0000; changes at +0x01 and +0x08 (upk 0x8B112D, 0x8B1134) | `04 9A 01 [Engine.GameInfo.NumPlayers] 2C 02 16` = `return NumPlayers == 2` (disk ref FFFFF5F8) | `04 99 01 [..] 2C NN 16` = `return NumPlayers >= N` (0x99 = native 153, `>=`) | Num == 17. Context +0x0A `04 3A [ReturnValue]`, +0x10 `53`. Operand +0x03 equals `Engine.GameInfo.NumPlayers`. | Yes: the cap at PreLogin, Login and PostLogin |
| S1-2 | `GearGame.GearGameSP_Base.SuggestPawnClassFor` (145 -> 279, grown) | Replace +0x0084..+0x0090; +0x0000..+0x0083 unchanged (upk 0x8B34B1) | `04 20 [GearGame.GearPawn_COGGear] 04 3A [ReturnValue] 53` = `return GearPawn_COGGear` (disk 6F95) | The 147-byte tail below: Ben Carmine, then RedShirt, by `DynamicLoadObject` | Num == Max == 145. Context +0x0061 `04 20 [GearPawn_COGMarcus]`, +0x007E `04 20 [GearPawn_COGDom]`. Every resolved object has the expected class (Class, Function, property). | Yes: gives players 3+ a body that is not Marcus or Dom |
| S1-3 | `GearGame.GearMenuGame.ChangeCoopInviteSetting` (178) | +0x0047; change at +0x58 (upk 0x7FD7B9) | `0F 19 00 [Coop] 00 05 00 04 01 [Engine.OnlineGameSettings.NumPublicConnections] 2C 02` = `Coop.NumPublicConnections = 2` (disk refs 00002F73, FFFFF5E7) | `... 2C NN` | Num == 178. `[Coop]` is the parameter of this function. Context +0x0086 `0F 01 [GameInfo.MaxPlayers] 92` (`MaxPlayers = Public + Private`). | Yes: keeps the beacon answering until N players are in |
| S1-4 | Default object `GearGame.Default__GearCoopGameSettings` | Native +132 (`NumPublicConnections`) | int 2 (disk: tagged IntProperty at upk 0x86E033, `00 00 00 02`) | int N | Name and outer match, and Class is `GearGame.GearCoopGameSettings`. Value at +132 == 2. | Yes, as a guard: settings objects created at DG:25438, 89251 and 92950 copy this default [I which paths skip S1-3] |
| S1-5 | `GearGame.GearCampaignLobbyGame_Base.InitGame` (169) | +0x009E; change at +0xA5 (upk 0x80118B) | `0F 01 [Engine.GameInfo.MaxPlayers] 2C 02` = `MaxPlayers = 2` (disk FFFFF5FA) | `... 2C NN` | Num == 169. Context +0x00A6 `04 0B`, +0x00A8 `53`. | Yes for joins in the GearStart lobby (`GameInfo.AtCapacity` reads MaxPlayers) |
| S1-6 | `GearGame.GearGameSP_Base.InitGame` (202) | +0x00B4; change at +0xBB (upk 0x8B282B) | `0F 01 [Engine.GameInfo.MaxPlayers] 2C 02` = `MaxPlayers = 2` | `... 2C NN` | Num == 202. Context +0x00BC `0F 01 [GameInfo.MaxDynamicBandwidth] 1D 00 00 1B 58`, +0x00C9 `53`. | Consistency only: not on the join path |

The offset +132 for S1-4 comes from `layout.py` on Engine.upk (`OnlineGameSettings`: Public +132,
Private +136, OpenPublic +140, `bIsLanMatch` at +156 mask 0x40000000). It agrees with two
disassembly anchors: `sub_828294A0` reads +140 and +156, and `sub_8282B370` passes +132 as `maxPub`
to `XSessionCreate`. [V]

S1-2 tail. In memory, each `[..]` is the object's address; on disk it is the package reference in
parentheses. The jump target 0x0110 is absolute.

```
+0x0084  0F 00 [ReturnValue (00004DB7)] 13 [GearGame.GearPawn (00006E55)]
         1C [Core.Object.DynamicLoadObject (FFFFFAB9)]
         1F "GearGameContent.GearPawn_COGBenjaminCarmine" 00
         20 [Core.Class (FFFFFE2C)] 27 16
         ; ReturnValue = class<GearPawn>(DynamicLoadObject("...BenjaminCarmine", class'Class', true))
+0x00C8  07 01 10 72 00 [ReturnValue] 2A 16
         ; if (ReturnValue == None) ... else goto 0x0110
+0x00D3  0F 00 [ReturnValue] 13 [GearGame.GearPawn] 1C [Core.Object.DynamicLoadObject]
         1F "GearGameContent.GearPawn_COGRedShirt" 00 20 [Core.Class] 27 16
+0x0110  04 3A [ReturnValue]
+0x0116  53
```

- `[ReturnValue]` is copied from the original operand at +0x008C, after the patcher checks that it
  is a property named `ReturnValue` whose outer is this function.
- `join-spawn/verify_patches.py` builds the same tail and decodes the resulting 279-byte script to
  its end. [V on disk bytes; the in-memory form has not run yet]
- If both loads return None, `GetDefaultPlayerClass` (DG:47025) falls back to the game's
  `DefaultPawnClass`, which is Marcus. That reintroduces the problem from section 1.3, so Stage 1
  runs only where Ben is loaded.
- Any spawn failure is a full-squad game over (`GameInfo.RestartPlayer+0x0136` -> `Dead`), so watch
  for it in the log.

Diagnostic variant D-1, off by default and exclusive with S1-2: swap the operand at +0x0086 of
`SuggestPawnClassFor` from `[GearGame.GearPawn_COGGear]` to `[GearGame.GearPawn_COGMarcus]` (disk
`04 20 00006F95` -> `04 20 00006FA9`, same size). Players 3+ become Marcus clones, which are always
loaded, have a mesh and survive chapter commits. Use D-1 only to separate network bring-up from the
grown-script path; section 1.3 explains why it breaks Kismet. It is built in, behind
`gow2_coop_marcus_clones`.

Deliberately not in Stage 1:

- `Default__GearGameSP_Base.MaxPlayersAllowed` (no effect, see 1.1).
- The `RestartPlayer` jump at +0x06EE (wrong target, see 1.1).
- `MaxDynamicBandwidth` (Stage 3).
- Any change to `PostLogin`.

## 5. Open questions

Settled from code while writing patch 0010:

- **`XSessionCreate` flags.** The flag builder `sub_8282A760` returns 0x21 (HOST |
  USES_PEER_NETWORK, the XDK's `XSESSION_CREATE_SYSTEMLINK` plus HOST) as soon as `bIsLanMatch`
  (+156, mask 0x40000000) is set, before it looks at any other setting, so a LAN host passes exactly
  0x21. The join path `sub_8282CA88` calls the same builder and clears bit 0 (`rlwinm r24,r3,0,0,30`),
  so a joiner passes 0x20. [V] `system_link_trace` prints the flags of every create to confirm it at
  run time.
- **XNKID marker.** The title never builds or tests an XNKID: it copies SessionInfo+12 into the
  beacon response (`sub_828294A0`) and hands it back to XNet (`XNetRegisterKey`,
  `XNetXnAddrToInAddr`). Only XNet reads the marker. The XDK convention is that a system-link XNKID
  has the top bits of byte 0 clear (`XNET_XNKID_SYSTEM_LINK` = 0x00 under mask 0xE0/0xF0); patch 0010
  clears the top nibble of a random XNKID. [V title side, I XDK constants]
- **SessionInfo zero-fill.** Yes. The host path `sub_8282AA10` allocates the 72-byte SessionInfo
  (`li r3,72` / `bl 0x827df658`), clears it with `memset(p, 0, 72)` (`bl 0x82a93150`) and stores it
  at Session+8 before `sub_8282B370` calls `XSessionCreate`. The block is the nonce (+0), the
  handle (+8) and `XSESSION_INFO` (+12, 60 bytes). A joiner reuses the block that `sub_8282A9A0` fills
  from the search result. [V]
- **`XSessionGetDetails` size.** The title's wrapper `sub_829A5810` requires at least 128 bytes
  (`sizeof(XSESSION_LOCAL_DETAILS)`) and takes `pcbResultsBuffer` as a pointer; the XGI message
  carries that pointer at +4, not the size. [V]

Network and SDK:

- Does Windows deliver the game's 255.255.255.255:14001 broadcast to other local instances bound to
  0.0.0.0 with `SO_REUSEADDR`? If it does, fan-out is needed only in `net_bind_ip` mode.
- Which server cap clamps client rates, `MaxClientRate` or `MaxInternetClientRate`?
- Does the client's System Link browser filter on `bAllowJoinInProgress` or
  `NumOpenPublicConnections` in native code?
- Is a 7000 bytes/s cap per client enough for 4 players? Stage 1 measures it.

Running four instances on one PC:

- Do the instances share save and profile folders?
- Does the game or the runtime enforce a single instance?
- How does input reach the right instance? Keyboard focus would be per window, but controller
  routing is unknown.
- How much CPU and GPU do four recompiled instances need?

Script and engine:

- `REX_HOOK_RAW(sub_8283F780)` links and replaces the call on Windows (section 2). Does it run (the
  `SCRIPTPATCH startup` log line), and does it link the same way on Android? Are Num and Max of
  `UStruct::Script` at +88 and +92? The checks fail closed if not.
- How does the native checkpoint loader match records to PCs, and who sets `bCheckpointDummy`?
- How does `UWorld::CommitMapChange` treat remote players' pawns once
  `bKillDuringLevelTransition` is cleared?
- Is `WorldInfo.bNoDefaultInventoryForPlayer` set in the Act 1 maps? The actor header parse failed.
  If it is set, extra players spawn unarmed.
- When a streaming sub-level that exported Ben Carmine unloads (chapters 3-4), does a live pawn keep
  the class alive? Can a client that has not streamed that sub-level resolve the class?
- Does the Minh_Kim skeleton have the player camera bones (`b_MF_Camera`, `b_MF_Camera_Root`)? Only
  a byte search supports it so far.
- When `DynamicLoadObject` misses (chapter 1), does it touch the disk or raise a disc error?
- Is `sub_825E2780` the only way pawns enter `PawnList`?
- The Kismet of `SP_Hospital_03` and `SP_Sinkhole_W` was not scanned (LZX decompression failed at
  chunk 20). The first is a chapter 2 map.
- Does the vehicle camera work for a rider whose view target is a vehicle he is not seated in?
- Does `Pawn.OnTeleport` fail silently for the third and fourth pawn sent to one destination?
- If Title Update 6 replaces `GearGame.xxx`, the patch table needs one variant per version, keyed
  by ScriptSize and the context bytes.
