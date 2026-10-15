# Network play: System Link on a LAN and over the internet

This guide covers System Link co-op between a PC and a phone, on one home
network or over the internet.

## What you need

- Every player runs a build with patch 0011. Use the same build everywhere.
  There is no automatic version check yet, so check by hand.
- Every player uses the same `net_port_offset`. Leave the default.

## Settings

Put these in `gow2.toml` next to `gow2.exe` on PC (the build copies
`config/gow2.toml` there; its Network section lists them commented out), as
`key = "value"`. You can also pass them on the command line as
`--name=value`. On Android the app detects the Wi-Fi address itself.

| Setting | Default | Effect |
|---|---|---|
| `net_port_offset` | 40000 | Guest ports below 1024 move up by this offset on the host. The game's UDP 1000 becomes 41000. The beacon port 14001 stays as it is. |
| `net_local_ip` | empty | This machine's LAN address, as a.b.c.d or a.b.c.d/prefix (prefix 24 by default). Empty means the app's detected address, then `net_bind_ip`, then the default-route address, then 127.0.0.1. |
| `net_peers` | empty | Comma-separated host[:port] list (IPv4 or DNS names). Every broadcast the game sends also goes to each peer as a unicast copy. |
| `net_public_address` | empty | host[:port] (IPv4 or DNS name) advertised to other players. |
| `net_allow_peers` | empty | Only these IPv4 addresses, a.b.c.d/n ranges or DNS names reach the game. Empty means the peers plus the local subnet when `net_peers` is set, else everyone. |
| `system_link_trace` | false | Set to true to log network activity under `[netlink]`. |

## PC and phone on the same Wi-Fi

The PC sits on Ethernet at 192.168.10.10/24. The phone (Galaxy S25 Ultra)
sits on Wi-Fi at 192.168.10.114/24. Both are on the same subnet.

Let the phone host:

1. Start a System Link co-op game on the phone.
2. Search for System Link games on the PC and join.

To run the PC side from a script, `scripts\run-instances.ps1 -Names pc
-Lan` starts one windowed copy with `system_link_trace` on and its own
log, without the loopback settings of "Testing on one PC" below, and
`scripts\instance-input.ps1` sends it keys. The menu path is the
client's path from that section.

The phone hosts because the PC's network profile is Public, and inbound
rules exist only for the main tree's `gow2.exe` path. A `gow2.exe` at any
other path (for example a worktree build) gets no unsolicited inbound
traffic. That copy can join a phone's game but cannot host, until a rule
exists for its path.

To let a PC host, add an inbound rule for its `gow2.exe` path. Run this in
an elevated prompt (replace the path with the full path to that copy):

```
netsh advfirewall firewall add rule name="gow2 System Link" dir=in action=allow program="<full path to gow2.exe>" protocol=UDP localport=14001,41000 profile=public,private
```

To check the existing rules, run:

```
netsh advfirewall firewall show rule name=gow2 verbose
```

The existing rule for
`C:\personal\gow2-recomp\out\build\release\gow2.exe` already lets that
copy host.

A phone acting as a Wi-Fi hotspot also works. The app picks the hotspot
address (swlan0/ap0) itself.

## Over the internet (host on a PC)

1. On the router, forward UDP 14001 (System Link beacon) and UDP 41000
   (game traffic, guest port 1000 + 40000) to the hosting PC
   (192.168.10.10). TCP is not needed for campaign co-op (TCP 8675 is the
   versus party beacon).
2. DNS: create an A record such as `gow2.suruworks.com` that points at the
   home's public IP. If the ISP changes the address, use dynamic DNS.
   Check for CGNAT first: if the router's WAN address differs from the
   public address that a "what is my IP" page shows, port forwarding
   cannot work. Then use a virtual LAN such as Tailscale or ZeroTier and
   put the other players' virtual addresses in `net_peers`, because
   broadcasts do not cross those either.
3. The host sets `net_public_address = "gow2.suruworks.com"`. It can also
   set `net_allow_peers` with the friends' addresses.
4. Each friend sets `net_peers = "gow2.suruworks.com"` and searches for
   System Link games as usual.
5. The host's Windows Firewall needs the inbound rule for its `gow2.exe`
   path (see the command above).
6. Phones on mobile data sit behind carrier NAT in most cases and cannot
   host. They can join.

## Security note

`net_allow_peers` limits who reaches the game. Without it, any address
that can reach the forwarded ports can send datagrams to the game. The
game's System Link traffic is not encrypted.

## Testing on one PC

Two copies of `gow2.exe` can play System Link on one PC over loopback
[V]: the host binds 127.0.0.1, the client 127.0.0.2. Verified on
2026-10-14 with the crossplay build (patch 0011): the client found the
host's campaign in the System Link Browser, joined the lobby, loaded Act
1 chapter 2 with the host and played as Dom. A second run repeated it
with the first-datagram trace (see Troubleshooting). Evidence:
`out\pcpc-test\run2` and `out\pcpc-test\run3` (logs, screenshots,
`netlink-evidence.txt`). `out\` is not in git.

`scripts\run-instances.ps1` starts the copies and supervises them. Each
copy gets its own `LOCALAPPDATA` under the run folder (the game keeps
profile, saves and caches in `%LOCALAPPDATA%\GearsOfWar2PC`, so the
script seeds each copy with a copy of the real one), its own log, XUID
(`B13E0000000000` + 2 hex digits) and gamertag (the instance name),
`--net_bind_ip`, `--net_broadcast_targets` listing every copy,
`--system_link_trace=true`, and a windowed 1152x648 window placed side
by side. It stops every copy it started when the file `stop` appears in
the run folder, when all copies exit, or after `-MaxSeconds` (600 by
default). It writes `instances.json`.

Example:

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-instances.ps1 -OutDir out\pcpc-test\run1 -ExtraArgs "--audio_mute=true --resolution_scale=1"
```

`scripts\instance-input.ps1` brings one copy to the front and sends it
keys, clicks and screenshots, for example:

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\instance-input.ps1 -OutDir out\pcpc-test\run1 -Instance client -Steps "focus,key:S:200,wait:800,Space,wait:3000,shot:after-join"
```

Steps: a key name (`System.Windows.Forms.Keys`) or
`key:<name>:<holdMs>`, `wait:<ms>`, `shot:<label>` (PNG in
`<run>\shots`), `click` (left button at the window centre), `focus`. The
game only reads input from the focused window.

Keys in the menus with `--mnk_mode=true`: W/S move the selection (left
stick; the arrow keys are the right stick and do not move menus), Space
= A (select), F = B (back), E = X, Q = Y, Return = Start, left mouse
button = right trigger.

Menu path that worked:

1. Host: PRESS START (Return) -> main menu, S to CO-OP CAMPAIGN, Space
   -> CAMPAIGN MENU, HOST CO-OP CAMPAIGN (Space) -> SELECT SAVE SLOT
   (Space) -> CAMPAIGN LOBBY. CO-OP MODE already reads "System Link".
   The session is created when the lobby opens: the host log shows
   `[netlink] XGISessionCreate ... flags=0x00000021 ... host=true ...
   host_ip=127.0.0.1`. LAUNCH CAMPAIGN (Space) once the client is in the
   lobby.
2. Client: PRESS START -> CO-OP CAMPAIGN -> JOIN CO-OP CAMPAIGN (S,
   Space) opens the SYSTEM LINK BROWSER directly. E refreshes; the
   host's campaign appears under its gamertag; Space joins and both
   lobbies list both players. After the host launches, the client sees
   "PRESS RT TO BEGIN!"; the right trigger (left mouse button) spawns
   the client as Dom.

What the logs show:

```
client: [netlink] broadcasts to port 14001 go out to [127.0.0.1:14001]
client: [netlink] XGISessionCreate ... flags=0x00000020 ... host=false known=true xnkid=00F4837B3BB2B61B host_ip=127.0.0.1
client: [netlink] XNetXnAddrToInAddr caller=1 ina=127.0.0.1 inaOnline=0.0.0.0 -> 127.0.0.1 ret=0x0
client: [netlink] sendto port 1000 -> 41000 (net_port_offset)
host:   [netlink] bind port 1000 -> 41000 (net_port_offset)
host:   [netlink] broadcasts to port 14001 go out to [127.0.0.2:14001]
client: [netlink] first datagram to 127.0.0.1:14001 from local port 14001 (16 bytes)
host:   [netlink] first datagram from 127.0.0.2:14001 on local port 14001 (16 bytes)
host:   [netlink] first datagram to 127.0.0.2:14001 from local port 14001 (190 bytes)
client: [netlink] first datagram from 127.0.0.1:14001 on local port 14001 (190 bytes)
client: [netlink] first datagram to 127.0.0.1:41000 from local port 54430 (55 bytes)
host:   [netlink] first datagram from 127.0.0.2:54430 on local port 41000 (55 bytes)
```

The 16-byte datagram is the client's System Link query and the 190-byte
one the host's beacon answer; game traffic then goes to the host's
remapped port 41000.

The client's XNKID equals the one the host created, which is how the
beacon answer reached it.

Loopback traffic does not cross Windows Firewall, so this test says
nothing about the firewall; the PC-phone test does.

## Troubleshooting

Turn on `system_link_trace` and look for these log lines:

- `[netlink] local address ... (default route)`
- `[netlink] resolved ...`
- `[netlink] broadcasts to port 14001 go out to [...]`
- `[netlink] dropped ... (net_allow_peers)`
- `[netlink] XNetXnAddrToInAddr ... -> ...`
- `[netlink] first datagram from <ip>:<port> on local port <port> (<n>
  bytes)` and `[netlink] first datagram to <ip>:<port> from local port
  <port> (<n> bytes)`: logged once per socket handle and peer in each
  direction, even after the trace's rate limit (first 50 calls, then
  every 500th) is spent. Windows reuses socket handles, so a reconnect
  to the same peer on a new socket may not log again. On a PC that joins a phone's game, "first
  datagram from <phone>:14001 on local port 14001" means the phone's
  beacon answer got through Windows Firewall; its absence while the
  phone's log shows the answer going out points at the firewall.

The successful and the failed (would-block) recvfrom/sendto calls now
have separate trace budgets, so the polling of an idle socket no longer
hides real datagrams.


If the PC advertises 127.0.0.1, set `net_local_ip` to its LAN address.
