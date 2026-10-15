# Saves: where they live and how to keep them safe

Status (October 2026): the campaign saves, the game's profile settings and the achievement unlocks
live in one folder per device. The `gow2_save_dir` setting in `gow2.toml` moves that folder anywhere,
for example into OneDrive, Google Drive or Dropbox, so the cloud client backs it up and syncs it.
Nothing in the port uploads anything by itself.

## Steam Cloud: not possible

Steam Cloud cannot hold this port's saves, whether the game is added to Steam as a non-Steam
shortcut, run on a Steam Deck, or wrapped in a Steam Input layout:

- Steam Cloud is a per-app service the game's developer sets up in the Steamworks App Admin panel:
  the byte quota and the number of files per user, and for Auto-Cloud the root folders to sync, all
  belong to that app's configuration. Valve's own wording: "This quota is enforced on each
  Cloud-enabled game, on a per-user-per-game basis", and Auto-Cloud needs "Configuration of file paths
  required on Steamworks website" ([Steam Cloud, Steamworks docs][steam-cloud]).
- The cloud then works for the people who own a license for that app ("the cloud icon will be visible
  in the Steam client for anyone that owns your game", same page). A library shortcut is only a
  launcher entry; it has no Steamworks app behind it, so there is nothing for Steam to sync to.
- An App ID of our own is out of reach too: Steam Direct charges US$100 per product and its content
  rules exclude "Content you don't own or have adequate rights to" ([Steam Direct][steam-direct]).
  Gears of War 2 belongs to Microsoft and is not sold on Steam.

The only ways to borrow Steam Cloud would be to pass the port off as another game's App ID, which
breaks Steam's rules and can cost the account; this project does not document or support that.
What Steam still does well for the port: a library entry, Steam Input layouts, Remote Play and, on a
Steam Deck, the Game Mode launcher. For the saves, use one of the routes below.

## Where the port writes

| | Windows | Android |
|---|---|---|
| Saves, profile settings, achievements (user data root) | `%LOCALAPPDATA%\GearsOfWar2PC\user` | `/storage/emulated/0/Android/data/com.santiquiroz.gow2recomp/files/gow2` |
| Cache (shader cache, edited Coalesced copies; rebuilt when missing) | `%LOCALAPPDATA%\GearsOfWar2PC\cache` | `.../files/gow2/cache` (inside the user data root) |
| Settings (`gow2.toml`) | next to `gow2.exe` | `.../files/gow2.toml` |
| Logs | `logs\` next to `gow2.exe` | `.../files/logs` |

Where those come from: on Windows `Gow2App::OnConfigurePaths` (`src/gow2_app.h`) sets the user data
and cache roots under `%LOCALAPPDATA%\GearsOfWar2PC`. On Android the activity sets `XDG_DATA_HOME`
and `REX_EXECUTABLE_FOLDER` to the app's external files folder
(`android/app/src/main/java/com/santiquiroz/gow2recomp/NativeEnvironment.java`), and the runtime
puts the user data in `$XDG_DATA_HOME/gow2` with the cache inside it (`ReXApp::SetupEnvironment`,
`src/ui/rex_app.cpp` in the SDK).

Inside the user data root (SDK `src/system/xam/content_manager.cpp`, `user_profile.cpp`,
`kernel_state.cpp`):

```
<user data root>/
  B13EBABEBABEBABE/4D53082D/00000001/Gears2Checkpoint/Gears2Checkpoint0.sav   campaign checkpoint
  B13EBABEBABEBABE/4D53082D/Headers/00000001/Gears2Checkpoint.header          its content header
  4D53082D/profile/User/63E83FFD, 63E83FFE, 63E83FFF                          the game's profile settings
  achievements/4D53082D.toml                                                  achievement unlocks
```

`B13EBABEBABEBABE` is the runtime's single offline profile (XUID), `4D53082D` the game's title ID,
`00000001` the Xbox content type for saved games, and `User` the profile name. A real run on the
development PC left exactly that layout: `Gears2Checkpoint0.sav` (133 855 bytes),
`Gears2Checkpoint.header` (328 bytes) and the three profile setting files (`63E83FFF`, 617 bytes).
Everything worth keeping is under 1 MB; the cache next to it was 16 MB and can be thrown away.

Settings that move the data:

- `gow2_save_dir` (below): the user data root only. The cache stays where it is.
- The runtime's own `--user_data_root` flag does not work on Windows: `OnConfigurePaths` replaces it
  with the `%LOCALAPPDATA%` folder. Use `gow2_save_dir`.
- `GOW2_HOME` only moves `gow2_game_path.txt` (the remembered game folder), not the saves.

## `gow2_save_dir`

One line in `gow2.toml` (or `--gow2_save_dir=<folder>` on the command line):

```toml
gow2_save_dir = '%OneDrive%\GearsOfWar2\saves'      # OneDrive (Windows sets %OneDrive%)
gow2_save_dir = 'G:\My Drive\GearsOfWar2\saves'     # Google Drive for desktop
gow2_save_dir = 'D:\Dropbox\GearsOfWar2\saves'      # Dropbox
gow2_save_dir = 'saves'                             # portable: a folder next to gow2.exe
```

Single quotes keep backslashes as typed (a TOML literal string); forward slashes work as well.

What happens at start-up (`src/gow2_save_dir.cpp`, applied in `Gow2App::OnFinalizePaths`, which runs
after `gow2.toml` is loaded and before the runtime is built):

1. Empty setting: nothing changes.
2. `%VARIABLES%` are expanded on Windows; a relative path starts at the folder that holds `gow2.toml`.
3. The folder is created if needed and a probe file is written and removed, so a folder the game cannot
   write to is caught before any save is lost.
4. If the folder is empty and the old save folder is not, the old saves are copied into it once
   (never overwriting a file, never deleting anything, never copying the cache). Your old folder stays
   as a backup.
5. The log says where the saves are, for example
   `saves, profile settings and achievements in C:\Users\you\OneDrive\GearsOfWar2\saves (gow2_save_dir)`.
   If the folder cannot be used, a warning box names the reason and the saves stay in the default
   folder for that run.

The setting takes effect on the next start.

## Keeping saves safe and synced

### 1. A cloud folder (the one-line setup)

Point `gow2_save_dir` at a folder your cloud client already syncs. That gives an off-site copy and
the same campaign on every PC signed in to the same account.

- Play on one device at a time with a given folder, and let the client finish syncing before you
  start the game elsewhere. Two devices writing the same checkpoint make the client keep both
  (OneDrive and Dropbox add the device name to one copy); the game only reads the original name.
- OneDrive with Files On-Demand: mark the folder "Always keep on this device" so the game never waits
  for a download while loading a checkpoint. Google Drive for desktop in streaming mode: make the
  folder available offline for the same reason.
- A synced folder mirrors mistakes too (a corrupted or deleted save syncs like any other change).
  Keep version history on (OneDrive, Dropbox and Google Drive all keep earlier versions of a file) or
  add the versioned backup below.

### 2. Ludusavi + rclone (versioned backups, any cloud)

[Ludusavi][ludusavi] (MIT) backs up game saves and keeps backup versions; its cloud backup uploads them
through rclone to "Google Drive, OneDrive, Dropbox, Box, FTP, SMB, and WebDAV" or any other rclone remote
([cloud backup][ludusavi-cloud]). The port is not in Ludusavi's game list, so add it as a custom game.
In the GUI: *Custom games* > add, name `Gears of War 2 (PC port)`, file path
`<winLocalAppData>/GearsOfWar2PC/user` (or your `gow2_save_dir` folder). The same entry in Ludusavi's
`config.yaml` (`%APPDATA%\ludusavi\config.yaml`, schema in [config.yaml][ludusavi-schema]):

```yaml
customGames:
  - name: Gears of War 2 (PC port)
    integration: override
    files:
      - <winLocalAppData>/GearsOfWar2PC/user
```

`<winLocalAppData>` is Ludusavi's placeholder for `%LOCALAPPDATA%` ([manifest placeholders][ludusavi-manifest]).
To back up on every play session, launch the game through `ludusavi wrap`, which restores before the
game starts and backs up after it exits ([game launch wrapping][ludusavi-wrap]). For a non-Steam
shortcut its docs say: set the shortcut's target to `ludusavi.exe` and its launch options to
`wrap --name "Gears of War 2 (PC port)" --gui -- "C:\path\to\gow2.exe"`.

Ludusavi's automatic cloud sync only uploads, and only when local and cloud copies already match; a
mismatch is reported and you pick upload or download yourself ([cloud backup][ludusavi-cloud]). That
makes it a backup, not a two-way sync: use it with route 1 or 3, not instead of them, if you play on
more than one device.

### 3. Syncthing between devices (no cloud account)

[Syncthing][syncthing] syncs a folder directly between your devices, with optional
[file versioning][syncthing-versioning]. Between PCs (or a PC and a home server) share the
`gow2_save_dir` folder, or `%LOCALAPPDATA%\GearsOfWar2PC\user`, and turn on staggered versioning.

On the phone it does not work today. Android 11 and newer forbid every app from reading another
app's folder under `Android/data` ("apps can no longer access files in _any_ other app's dedicated,
app-specific directory within external storage", [Android 11 storage updates][android11-storage]),
and even All files access does not open it ([manage all files][android-all-files]). So Syncthing-Fork
([F-Droid][syncthing-fork]; the official Android app was discontinued in 2024,
[announcement summary][syncthing-android-end]) cannot see the game's default save folder. Pointing `gow2_save_dir` at shared storage (for example
`/storage/emulated/0/GearsOfWar2/saves`) would let Syncthing see it, but then the game must read files
another app wrote, which on Android 11+ needs the All files access permission
(`MANAGE_EXTERNAL_STORAGE`). The APK does not request it yet, so expect the start-up probe to fail
(the game then keeps the default folder) or a synced checkpoint to be unreadable; neither case has
been tried on the phone. Follow-up for the Android app: either request
All files access (fine for a sideloaded APK; Google Play restricts it) or add save export/import
through the system folder picker.

### 4. Later: your own server

Any of the routes above can end at a server you run (for example behind `suruworks.com`): an
always-on Syncthing device as a third peer that keeps versions, or an rclone remote (WebDAV, SFTP,
S3-compatible) as Ludusavi's cloud target. Neither needs code in the port. A sync built into the game
itself is not planned until one of these has been used for a while.

## A PC save on the phone (and back)

The folders are interchangeable as they are:

- same title ID (`4D53082D`): both builds run the same `default.xex`;
- same profile: the runtime uses one fixed offline XUID (`B13EBABEBABEBABE`) and the name `User` on
  every platform (`UserProfile::UserProfile`, SDK `src/system/xam/user_profile.cpp`);
- same files: the `.sav` holds the game's own bytes; the `.header` is the runtime's
  `XCONTENT_AGGREGATE_DATA` with big-endian fields, written by the same code on two little-endian CPUs
  (x86-64 and arm64); the profile settings use the same serializer.

Not verified yet: the phone had no save of its own when this was written
(`adb shell find .../files/gow2` listed only `cache`), and no PC checkpoint has been loaded on the
phone so far.

Moving a save by USB (close the game on both devices first; copy the target's folder somewhere safe
before overwriting it). From PowerShell on the PC:

```powershell
$phone = '/storage/emulated/0/Android/data/com.santiquiroz.gow2recomp/files/gow2'
$pc = "$env:LOCALAPPDATA\GearsOfWar2PC\user"   # or your gow2_save_dir folder

# PC -> phone
adb push "$pc\B13EBABEBABEBABE" "$pc\4D53082D" "$phone/"
# phone -> PC (the phone keeps its cache inside gow2/: copy only the two save folders)
adb pull "$phone/B13EBABEBABEBABE" "$phone/4D53082D" "$pc\"
```

Add `achievements` to either list once that folder exists. From Git Bash set `MSYS_NO_PATHCONV=1`
first, or it rewrites the `/storage/...` path into a Windows path. The same `Android/data` folder is
also visible over a USB file transfer (MTP) connection from Windows Explorer.

To use `gow2_save_dir` on the phone, put a `gow2.toml` with that line in
`.../com.santiquiroz.gow2recomp/files/` (see the Syncthing note above for what works there today).

## Sources

- [Steam Cloud, Steamworks documentation][steam-cloud]
- [Steam Direct][steam-direct]
- [Ludusavi][ludusavi], [cloud backup][ludusavi-cloud], [custom games][ludusavi-custom],
  [game launch wrapping][ludusavi-wrap], [config schema][ludusavi-schema],
  [manifest placeholders][ludusavi-manifest]
- [Syncthing][syncthing], [file versioning][syncthing-versioning],
  [Syncthing-Fork on F-Droid][syncthing-fork], [Syncthing Android app discontinued][syncthing-android-end]
- [Storage updates in Android 11][android11-storage], [Manage all files on a storage device][android-all-files]

[steam-cloud]: https://partner.steamgames.com/doc/features/cloud
[steam-direct]: https://partner.steamgames.com/steamdirect
[ludusavi]: https://github.com/mtkennerly/ludusavi
[ludusavi-cloud]: https://github.com/mtkennerly/ludusavi/blob/master/docs/help/cloud-backup.md
[ludusavi-custom]: https://github.com/mtkennerly/ludusavi/blob/master/docs/help/custom-games.md
[ludusavi-wrap]: https://github.com/mtkennerly/ludusavi/blob/master/docs/help/game-launch-wrapping.md
[ludusavi-schema]: https://github.com/mtkennerly/ludusavi/blob/master/docs/schema/config.yaml
[ludusavi-manifest]: https://github.com/mtkennerly/ludusavi-manifest
[syncthing]: https://syncthing.net/
[syncthing-versioning]: https://docs.syncthing.net/users/versioning.html
[syncthing-fork]: https://f-droid.org/packages/com.github.catfriend1.syncthingfork/
[syncthing-android-end]: https://alternativeto.net/news/2024/10/the-syncthing-android-app-is-being-discontinued-but-alternatives-remain-available
[android11-storage]: https://developer.android.com/about/versions/11/privacy/storage
[android-all-files]: https://developer.android.com/training/data-storage/manage-all-files
