# Co-op Stage 2: cinematics, teleports, checkpoints and splits

This document details the `coop-scenes` script group. It implements Stage 2 items 4-7 of
[COOP_N_PLAYERS.md](COOP_N_PLAYERS.md): teleports, cinematics, checkpoints and co-op splits.
Facts tagged [V] were checked offline against the player's own packages; [I] marks an
inference. The in-game tests in section 5 ran on 2026-10-16; details in
COOP_N_PLAYERS.md, Stage 2 results.

## 1. What the group does and its switches

The group holds 6 rows of server-side GearGame script. The rows live in
`tools/script_patch_scenes.py` (rows, `--check`, `--cpp`). The C++ side is
`src/gow2_coop_scenes.cpp` (cvar plus self-registration); the rows are printed into
`src/gow2_script_patch_scenes_rows.inc`.

It applies only when `gow2_coop_players` is 3..255 AND `gow2_coop_scenes` is true
(default true). With the default `gow2_coop_players=2` the hook returns before any group,
so the game stays byte-for-byte retail. Every player must still run the same build: a v0.1.1 peer lacks the d074c0d codegen fix and is expected to crash at the chapter 2 -> 3 map commit even in two-player co-op [I].

`gow2_coop_scene_probe` (default false) is a read-only diagnostic for test runs. Every 2 s it
logs, when a line changes, each GearPC's DefaultPawnClass, `bCinematicMode`, `bInvisible`,
its pawn's class, `bHidden`, location and distance to Marcus's pawn:
`SCENEPROBE <pc> dpc=<class> cine=<0|1> invis=<0|1> pawn=<class> hidden=<0|1> loc=<x,y,z>
dMarcus=<uu>`. It starts with the group, also when `gow2_coop_scenes` is false. The offsets
(Controller.Pawn +444, Pawn.Controller +456, Actor.bHidden +172 mask 0x40000000) come from
the package layouts; when no pawn points back at its controller for three sweeps in a row
it logs `SCENEPROBE layout check failed` and stops.

It is an independent fail-closed group. The hook applies registered groups in name order
(`src/gow2_script_patch_groups.h`) last, after `coop`, `survival`, `coop-body` and
`coop-ride`, and only when `coop` applied; otherwise it logs
`SCRIPTPATCH coop-scenes: off (the coop group was not applied)`. One failed check skips only
`coop-scenes` and logs `SCRIPTPATCH coop-scenes: skipped (<row>: <reason>); the other groups
stay applied`.

Definitions: an extra PC is a GearPC whose DefaultPawnClass is not None, not
GearPawn_COGMarcus and not GearPawn_COGDom. A hero PC is one whose DefaultPawnClass is
Marcus or Dom.

Three rules are built into every row. No row stores into ReturnValue: EX_ReturnNothing
zeroes the result in this build (Stage 1 lesson). Every call is a final call
(EX_FinalFunction) to a function with no override below the target class, so no FName
operands are needed. Every object operand lives in a root-set startup package (Core,
Engine, GearGame).

## 2. The rows

| Row | Function | Kind | Size | Effect |
|---|---|---|---|---|
| SC-1 | GearGame.GearPC.SetCinematicMode | grow | 506 -> 679 | When Marcus's PC changes cinematic mode, every extra PC gets the same call (Dom's PC never mirrors) |
| SC-2 | GearGame.GearGameSP_Base.RestartPlayer | grow | 1931 -> 2403 | When an extra restarts, destroy bodies of his class that an exact GearAI_Dom drives (orphans), then apply Marcus's current cinematic state to him |
| SC-3 | GearGame.GearPC.OnCheckpoint | grow + 1 byte edit in the kept prefix (+0x02A7 D2 -> D4) | 725 -> 1029 | At checkpoints whose TeleportClasses lists Marcus, far extras are pulled too |
| SC-4 | GearGame.GearPawn.OnTeleport | grow | 184 -> 607 | After a Kismet teleport of a hero's pawn, extras paired with that hero who walk (GearPawn, not vehicle) and are more than 1500 uu away are placed with the hero PC's FindAvailableTeleportSpot |
| SC-5 | GearGame.GearPC.SetSquadName | grow | 159 -> 371 | A hero's squad change is copied to the extras paired with him |
| SC-6 | GearGame.GearGameSP_Base.PostLogin | same-size operand swap at +0x0413 (LocalPlayer(PC.Player) becomes Object(PC.Player)) | 1690 -> 1690 | A late join never cancels the running cinematic |

## 3. Evidence

### Item 5: cinematics

- 151 of 254 SeqAct_ToggleCinematicMode targets are hero-only. [V]
- Kismet goes PlayerController.OnToggleCinematicMode, then the virtual SetCinematicMode.
  GearPC.SetCinematicMode returns at +0x0000 when Role < ROLE_Authority, so on the server
  it is the one choke point. No class below GearPC overrides it. [V]
- The mirrored call is idempotent: Engine.PlayerController.SetCinematicMode only adjusts
  move and look input when the state changes. [V]
- Correction: PostLogin+0x040D does more than CANCELCINE. It also stops the host's Bink
  movie (ClientStopMovie), and its else-branch at +0x04D6 plays the host's movie on the
  joiner. [V]
- Correction: SyncKismetSetProperties copies from the first other GearPC in ControllerList
  order, which is newest first (the latest joiner, not necessarily the host). [V]
- PostLogin+0x0395 syncs the joiner's cinematic flags before his pawn exists (+0x03CB).
  A joiner whose class already has a pawn (chapter 2's NPC Ben) does not restart at
  +0x03CB but later through ServerRestartPlayer, so the spawn-time cinematic sync belongs
  in RestartPlayer. [V]
- Only 4 toggles use the Toggle input. All target Player, all are in MountKadar, all have
  bHidePlayer=False. They stay consistent because the Player variable lists newest
  controllers first, so the host's PC is processed last. [I]

### Item 4: teleports

- 371 SeqAct_Teleport target links move only Marcus and/or Dom: 213 Marcus-only,
  157 Dom-only, 1 both. 48 target Player. [V]
- Open question answered: Pawn.OnTeleport (DE:31182) calls SetLocation(dest) and silently
  does nothing when it fails. No retry, no log. [V]
- When Marcus is in a vehicle, the vehicle is teleported (GearVehicle.OnTeleport calls
  Controller.OnTeleport(None)) and GearPawn.OnTeleport never runs, so extras are not moved
  then. [V]
- FindAvailableTeleportSpot revives a downed extra, breaks cover, leaves turrets and picks
  a free nav point within 1024 uu behind the hero with a clear trace. It can find none and
  do nothing. [V]

### Item 7: checkpoints

- 175 checkpoints. 29 restrict TeleportClasses with an exact-class Find: 28 to
  [GearPawn_COGMarcus, GearPawn_COGDom, Vehicle_Jack] and 1 to [Marcus, Dom]. All 29 list
  Marcus. [V]
- They are in later chapters (Sinkhole, Riftworm, Palace and others). Every chapter 2
  checkpoint is unrestricted. [V]
- New [V script]: on a checkpoint load each absent player's record becomes a dummy GearPC.
  GearPC.Destroyed (+0x0063, +0x01C7, +0x031A) spawns an exact GearAI_Dom on the restored
  body of any GearPawn_COGGear subclass (Ben included) and fires every
  SeqEvent_CoopPlayerLeft, so each load would leave an AI-driven duplicate of every extra,
  and the rejoining extra waits for RT because PostLogin finds a pawn of his class. [V]
- GearAI_Carmine, GearAI_Dizzy, GearAI_Niles, GearAI_Randy and GearAI_Tai extend GearAI_Dom
  (export table), which is why SC-2 tests the exact class. An IsA test would destroy
  chapter 2's NPC Ben. [V]
- Destroying a possessed pawn runs Controller.PawnDied but not Died/Killed, so
  EndGameCondition does not run. [I]
- Native record matching is still not traced. [I]

### Item 6: co-op splits

- 19 SeqAct_NotifyCoopSplit objects in 12 packages. The older text says 11 maps because it
  counts Nexis_7_S and Nexis_7_SB once. Both objects in Nexis_5_SB are unlinked. [V]
- Split start fires before the teleports in Intervention_10 and Sinkhole_02, after them in
  Outpost_07a, and on sibling branches in Palace_3/6. [V]
- Marcus is teleported first in Intervention_10, Nexis_7_SB and Palace_3/6. Dom is
  teleported first in Outpost_07a. [V]
- Squads: Kismet moves Dom to dom, DomSquad, DOMS, CarmineSquad or TaiSquad, then back to
  Player, Alpha or MarcusSquad. [V]
- AI revive during a co-op split requires the same squad (TeamHasAnyChanceOfSurvival+0x01FA)
  and humans never revive during a split (+0x0258). [V]
- Script never writes bInCoopSplit or bInSoloSplit, so the native NotifyCoopSplit does.
  SC-4 accepts either flag. [I]

## 4. Pairing of extras with heroes

Extras are numbered by their position among extras in WorldInfo.AllControllers order.
Outside a co-op split every extra follows Marcus's teleports. While GearGame.bInCoopSplit
or bInSoloSplit is set, even positions (0, 2, ...) follow Marcus and odd positions follow
Dom. With one extra, he always goes with Marcus.

Squads (SC-5) use the same parity at all times: even extras copy Marcus's squad changes,
odd extras copy Dom's, so Dom's restore at split end brings odd extras back.

Pairing can swap after a checkpoint load because controllers are re-created. Squads then
follow the new pairing at the next change. When Dom is AI (player 2 absent),
GearPC.SetSquadName never runs for Dom, so odd extras keep their squad.

## 5. Checks

Offline:

- `python -I tools/script_patch_scenes.py --check <folder with the decompressed
  Core/Engine/GameFramework/GearGame packages>`: each row prints its decoded new
  statements, `decodes to its end: True` and the goto-target check. It ends with
  `== coop-scenes: 6 rows OK`.
- `python -I tools/script_patch_scenes.py --cpp`: equals
  `src/gow2_script_patch_scenes_rows.inc`.
- `python -m pytest tools/tests -q`: synthetic tests in
  `tools/tests/test_script_patch_scenes.py`.

Expected in-game log lines, after the Stage 1 lines:

- `SCRIPTPATCH coop-scenes SC-1 GearGame.GearPC.SetCinematicMode: script grown to
  679 bytes`
- `SCRIPTPATCH coop-scenes SC-2 GearGame.GearGameSP_Base.RestartPlayer: script grown to
  2403 bytes`
- `SCRIPTPATCH coop-scenes SC-3 GearGame.GearPC.OnCheckpoint: script grown to 1029 bytes,
  with 1 write(s)`
- `SCRIPTPATCH coop-scenes SC-4 GearGame.GearPawn.OnTeleport: script grown to 607 bytes`
- `SCRIPTPATCH coop-scenes SC-5 GearGame.GearPC.SetSquadName: script grown to 371 bytes`
- `SCRIPTPATCH coop-scenes SC-6 GearGame.GearGameSP_Base.PostLogin: 1 write(s)`
- `SCRIPTPATCH coop-scenes: 6 rows applied for 4 players, set hash cddd0eadc988a154`

The hash depends only on the rows and the player count, so it must be the same on every
instance and run with 4 players. It was computed by the real C++ patcher applying the generated
rows to a fake guest (the Stage 1 core test's `FakeGuest`, in a local harness that is not part
of the build): 49 checks passed, covering the six log lines, each grown buffer byte by byte,
SC-3's prefix edit, SC-6's operand, a second apply that reports `already applied` without
allocating, and a flipped check byte that skips the whole group.

With `--gow2_coop_scenes=false`: `SCRIPTPATCH coop-scenes: off (gow2_coop_scenes=false)`.
With `gow2_coop_players=2`: only `SCRIPTPATCH coop: off (gow2_coop_players=2)`, no
coop-scenes line.

In-game tests (2026-10-16; details in COOP_N_PLAYERS.md, Stage 2 results).
Setup S: `scripts/coop-instances.ps1 start -Count 4` with
`--gow2_coop_players=4 --gow2_coop_census=true`, a save at Act 1 chapter 2, host HOST
CO-OP CAMPAIGN -> slot -> LAUNCH, instance 2 joins first (Dom), then 3 and 4 (System Link
browser, A, then RT).

| Test | Rows | What to do | Result |
|---|---|---|---|
| T1 | SC-1 | Tai cinematic or chapter-end cinematic in Act 1 ch2: extras hidden, no HUD, input ignored, visible again after; control --gow2_coop_scenes=false leaves both Bens visible | pass: hero-only CE gogo toggle hid both extras within 1 s (cine=1 invis=1 hidden=1); chapter 13 start hid and restored them (0 and 115 units from Marcus); Act 2 intro and convoy cutscenes hid all four, back after |
| T2 | SC-2 | An extra joins during a cinematic: hidden until it ends | not run |
| T3 | SC-2 | Checkpoint reload with 4 players: census shows GearPawn_COGBenjaminCarmine x3 (NPC Ben + 2 extras), not x4/x5, also after a second reload; the NPC Ben survives | pass for GearPawn_COGGear bodies: no orphans after reload; Ben census not rechecked with coop-body |
| T4 | SC-6 | A join more than 15 s into the level while a cinematic runs: the matinee continues to its natural end; a late Dom waits and takes over Dom after the cinematic | not run |
| T5 | SC-4 | Walk extras more than 1500 uu away before a scripted Marcus teleport in SP_Hospital_01_S: they end beside Marcus; short re-placements (< 10 m) do not move them | pass in sp_hospital_p with a loaded destination (CE TestEndCine twice: extras 213-220 units from Marcus); fail when the destination level is not loaded yet (leash covers it) |
| T6 | SC-4/SC-5 | A co-op split (SP_Outpost_07a_S, SP_Sinkhole_02_S, SP_Intervention_10_S, SP_Nexis_7_S/SB) or the SP_Hospital_02_S Dom-to-TaiSquad change: one extra on Dom's path / squad, one on Marcus's | not reached |
| T7 | SC-3 | A restricted checkpoint in a Sinkhole, Riftworm or Palace chapter: straggling extras are pulled; ch2 checkpoints behave as before | not run |

Every test also runs with `gow2_coop_players=2` (no change). Adding
`--gow2_coop_scene_probe=true` to the host gives objective lines for T1, T2 and T5: during a
cinematic that hides Marcus both Bens should log `cine=1 hidden=1`, and after a Marcus
teleport their `dMarcus` should drop under about 1100.

## 6. Known limits and merge rules

- An orphan cleanup loses the extra's saved inventory and health. He respawns near a
  teammate with class defaults.
- Extras are not moved when Marcus is teleported inside a vehicle, nor pulled at
  checkpoints while in a vehicle (GearPawn test).
- Pairing depends on the native setting bInCoopSplit/bInSoloSplit.
- FindAvailableTeleportSpot can find no free spot.
- SC-6 also stops a late Dom from skipping a long cinematic-mode section, so Dom waits.
- Any section that never leaves cinematic mode would keep Dom waiting. [I]
- Marcus-only toggles also freeze extras, as intended.
- GearPC.Destroyed (group a, item 3) also fires SeqEvent_CoopPlayerLeft for dummies.
  SC-2 cleans the orphans either way.
- Function ownership for merges: coop-scenes grows SetCinematicMode, RestartPlayer,
  OnCheckpoint, OnTeleport and SetSquadName (no other group may grow or edit them) and
  makes one same-size edit in PostLogin at +0x0413 (others may make same-size edits
  elsewhere in PostLogin, e.g. +0x03A4, but must not grow it: a grown function fails
  every other group's Script.Num check on it).
