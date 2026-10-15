# Playing over the internet

System Link was made for a LAN. Patch 0011 (`sdk/patches/0011-network-crossplay.patch`) lets one
player host a campaign that friends on other networks join: the host opens two UDP ports, and
every friend names the host with `net_peers`. This guide covers that set-up, what it exposes, and
two VPN alternatives that open no port at all.

Addresses below are examples from the documentation ranges: 203.0.113.10 is the host's public
address, 198.51.100.20 and 198.51.100.30 are friends, `gow2.example.com` is a DNS name the host
controls, and 192.168.1.10 is the host PC on its home network. Use your own values.

Status: PC and phone joined each other's campaign over one home LAN (NETWORK_PLAY.md,
2026-10-15), and the merged build joined on loopback. No session has crossed the internet yet;
see "Not verified yet".

## How the connection works
Only the host needs open ports. In the loopback runs (2026-10-14 and 2026-10-15), every datagram
the host sent went to an address and port it had first heard from: the client's ports 14001 and
the client's game socket. Only the client called XNetXnAddrToInAddr. [V on loopback, I over the
internet]

1. A friend's search goes as a unicast copy to each `net_peers` entry on UDP 14001. [V code]
2. The host answers with a broadcast and a unicast copy to the address the search came from;
   that unicast crosses the internet. [V code]
3. The beacon answer carries the host's XNADDR: its LAN address and, with `net_public_address`,
   its online address. [V code]
4. A peer outside the host's local network connects to the online address; a peer on the same
   LAN keeps using the LAN address (PeerInAddr in patch 0011). [V code]
5. Game traffic then goes to the host's UDP 41000: the game's UDP port 1000 plus
   `net_port_offset` (40000). The beacon port 14001 is above 1024 and keeps its number.
   [V code, unit tests]

A friend whose home network uses the same private range as the host (say 192.168.1.0/24) still
connects to the online address: a peer's LAN address counts as local only when the peer advertises
no online address or this side heard from that LAN address in the last 10 minutes. A phone on the
host's own Wi-Fi keeps using the LAN address. [V code]

## Host setup (PC)
Campaign co-op takes two players. For three or more, the host also sets `gow2_coop_players`
(co-op Stage 1, see [COOP_N_PLAYERS.md](COOP_N_PLAYERS.md)); that has not been tried over the
internet.

### Is the connection reachable at all?
Compare the WAN address on the router's status page with the public address that
https://api.ipify.org shows. If they differ and the router's WAN address is private (10.x,
172.16-31.x, 192.168.x) or in 100.64.0.0/10, either there is a second router in front of it
(double NAT, below) or the ISP shares one public address between customers (carrier-grade NAT).
Behind carrier-grade NAT no port forward can work: ask the ISP for a public address or use one of
the VPN options at the end. The game speaks IPv4 only (an XNADDR holds an IPv4 address), so IPv6
does not help.

### Ports to forward
Forward exactly UDP 14001 (System Link beacon) and UDP 41000 (game traffic) to the host PC. With
another `net_port_offset` the second port is 1000 plus that offset, and every player must use the
same offset; leave the default. No TCP port is needed for campaign co-op. TCP 8675 (versus party
beacon) must stay closed because stream sockets are not filtered by `net_allow_peers`. [V code]

UPnP does not help: the runtime's UPnP exports are stubs (`REX_EXPORT_STUB(__imp__NetDll_Upnp...)`
in the SDK's `src/kernel/xam/xam_net.cpp`), so the game opens no port by itself. [V code]

With one router:

| Device | Rule |
|---|---|
| Router (LAN 192.168.1.1) | UDP 14001 -> 192.168.1.10:14001, UDP 41000 -> 192.168.1.10:41000 |

With two routers in a row (an ISP modem-router, then your own router), every forward is needed
on both, each pointing at the next device inward:

| Device | Rule |
|---|---|
| Router 1, the ISP modem (LAN 192.168.0.1) | UDP 14001 and 41000 -> router 2's WAN address, for example 192.168.0.2 |
| Router 2 (WAN 192.168.0.2, LAN 192.168.1.1) | UDP 14001 and 41000 -> 192.168.1.10 |

Keep the targets fixed: reserve router 2's WAN address on router 1 and the PC's address on router
2, or the forwards break after a DHCP lease change. Alternatives to the forward on router 1 [I]:
set its DMZ host to router 2's WAN address (all unsolicited inbound traffic then reaches router 2,
whose own firewall still applies), or ask the ISP to put the modem in bridge mode so router 2 gets
the public address. Some ISPs lock parts of the modem's admin page; if the forward page is
missing, ask the ISP.

### DNS name (optional)
A literal public address works (`--net_public_address=203.0.113.10`), but home addresses are often
dynamic. A DNS name lets friends keep the same settings when it changes. In your domain's DNS
editor, add one of these records:
```
gow2   A       203.0.113.10        (TTL 300)
gow2   CNAME   <name>.duckdns.org.
```
A free dynamic DNS name such as DuckDNS follows the address when a scheduled task updates it every
few minutes (without an `ip=` parameter the service records the caller's address):
```
Invoke-RestMethod "https://www.duckdns.org/update?domains=<name>&token=<token>"
```
Check from any machine:
```
Resolve-DnsName gow2.example.com
```
The result must end in the host's current public address (https://api.ipify.org).

### Game settings
On PC, pass settings on the command line. Every build copies `config/gow2.toml` over
`out\build\release\gow2.toml`, so edits to the latter are lost, and the command line outranks
`gow2.toml`. See [NETWORK_PLAY.md](NETWORK_PLAY.md), "Settings".
```
gow2.exe --net_link_active=true --net_public_address=gow2.example.com --net_allow_peers=198.51.100.20,friend2.example.net --system_link_trace=true
```
`net_link_active` turns System Link on; it is off by default. `net_public_address` is advertised
as the online address (XNADDR inaOnline). `net_allow_peers` lists IPv4 addresses, a.b.c.d/n
ranges or DNS names that may send to the game; loopback and the local network always may. With
`net_public_address` set and `net_allow_peers` empty, only the local network and `net_peers` get
in: friends are dropped and the log warns
`[netlink] net_public_address is set but net_allow_peers is empty: ...` (fail closed).
`system_link_trace` logs network activity. Leave `net_port_offset` at 40000 on every player.
[V code]

Every player also sets a gamertag with `--user_name=<name>` (up to 15 characters; the default is
`User`). With `net_link_active` on, each install makes a random XUID once and keeps it in
`system_link_xuid.txt` in its default user data folder. Do not copy that file between installs:
two players with the same XUID would look like one player to the game. [V code for the file, I
for the effect] A `gow2_save_dir` that several PCs sync ([SAVES.md](SAVES.md)) does not carry it:
the file stays on each PC and the game passes it as `user_xuid`. [V code]

Friends find their public IPv4 at https://api.ipify.org. If a friend's address changes, use their
dynamic DNS name. An ISP range a.b.c.d/n also works, but lets in many more strangers.

Names resolve at start and every five minutes. A name lets nothing in until its first lookup
succeeds; when a lookup fails, the last good addresses stay in use. The online address enters the
XNADDR only after the host's own name resolves. Open the lobby only after the host log shows:
```
[netlink] resolved gow2.example.com -> 203.0.113.10
```
On the host, look for these lines:
```
[netlink] link active: local 192.168.1.10/24 (default-route adapter), bind 0.0.0.0, ...
[netlink] net_peers '', net_public_address 'gow2.example.com', net_allow_peers '...'
[netlink] first datagram from <friend-ip>:<port> on local port 14001 (<n> bytes)
[netlink] first datagram from <friend-ip>:<port> on local port 41000 (<n> bytes)
[netlink] dropped <n> bytes from <ip>:<port> (net_allow_peers)
```
The last line means a sender is not on the allow list. The first-datagram and dropped lines
appear only with `system_link_trace=true`. [V code]

### Windows Firewall (needs an administrator)
The first time a copy of `gow2.exe` listens, Windows asks whether to allow it. Allowing needs an
administrator; pressing Cancel as a standard user creates a Block rule for that exact path, and a
Block rule beats every Allow rule. Each build folder is a different program to the firewall.

Run these commands in an elevated PowerShell prompt. Replace `<exe>` with the full path to the
copy that hosts:
```
Get-NetFirewallApplicationFilter -Program '<exe>' | Get-NetFirewallRule | Where-Object Action -eq 'Block' | Remove-NetFirewallRule
netsh advfirewall firewall add rule name="gow2 System Link" dir=in action=allow program="<exe>" protocol=UDP localport=14001,41000 profile=public,private
```
A narrower option is to append `remoteip=LocalSubnet,198.51.100.20,198.51.100.30` so Windows also
drops everyone else. Disable the rule after the session and enable it again next time:
```
netsh advfirewall firewall set rule name="gow2 System Link" new enable=no
netsh advfirewall firewall set rule name="gow2 System Link" new enable=yes
```
A rule that allows every port for the executable (what clicking Allow creates) leaves the router
forwards as the only boundary: forward nothing beyond the two UDP ports.

## Friends' setup
Every friend needs the host's build and their own copy of the game's files: this project ships no
game data.

### PC
Use the same build as the host. No version check exists yet, and a build without patch 0011 uses
UDP 1000 instead of 41000 and cannot see a 0011 host.
```
gow2.exe --net_link_active=true --net_peers=gow2.example.com --user_name=<name>
```
Then choose CO-OP CAMPAIGN -> JOIN CO-OP CAMPAIGN to open the System Link browser; X (E on the
keyboard with `--mnk_mode=true`) refreshes. See [NETWORK_PLAY.md](NETWORK_PLAY.md) for the menu
path. No port forward or firewall rule is needed on a friend's side: answers come back on the
flows the friend opened. [I] With `net_peers` set, the friend's game accepts only its own local
network and the resolved host address. [V code]

### Android
Use the same APK build. Put a `gow2.toml` in
`/sdcard/Android/data/com.santiquiroz.gow2recomp/files/` with these lines:
```
net_link_active = true
net_peers = "gow2.example.com"
user_name = "<name>"
```
Copy it over USB, or push it from Git Bash (without `MSYS_NO_PATHCONV=1`, Git Bash rewrites the
`/sdcard/...` path):
```
MSYS_NO_PATHCONV=1 adb push gow2.toml /sdcard/Android/data/com.santiquiroz.gow2recomp/files/gow2.toml
```
With patch 0011 the phone binds the game's port 1000 as 41000, as the PC does
([android/README.md](../android/README.md)). [V code]

Wi-Fi or mobile data can join. [I] A phone on mobile data cannot host: carriers put phones behind
carrier-grade NAT. A phone on the host's home Wi-Fi needs no `net_peers` to join the PC over the
LAN; see [NETWORK_PLAY.md](NETWORK_PLAY.md).

Hosting on a phone at home is possible in principle [I]: point the forwards at the phone (with a
reserved address) instead of the PC, and put `net_public_address` and `net_allow_peers` in the
phone's `gow2.toml`. Android shows no inbound firewall prompt. Untested.

## Test before inviting friends
1. Put the forwards, the DNS record and the firewall rule in place. Confirm
   `Resolve-DnsName gow2.example.com` returns the public address.
2. On a phone, turn Wi-Fi off and mobile data on. Open https://api.ipify.org and note the phone's
   address.
3. Start the PC host with the command from "Game settings" and
   `--net_allow_peers=<phone's mobile address>`.
4. Put the Android `gow2.toml` from the Android subsection in place, then join the host.
5. Check the host log for `first datagram from <phone's mobile address>:<port> on local port
   14001`, then the same on local port 41000.

Do not test the internet path from a device on the home Wi-Fi: from inside, the public name leads
back through the router (hairpin NAT), which proves nothing about access from outside. Carriers
can change a phone's public address between connections. If the log shows
`dropped ... (net_allow_peers)` from a new address, update the allow list.

## Security trade-offs
- [I] System Link traffic is not encrypted or authenticated. The code that parses it was written
  in 2008 for a LAN, recompiled as is, and never reviewed for hostile input. A crafted datagram
  could crash the game or worse.
- [V code] `net_allow_peers` is the gate: a datagram from an unlisted address is dropped before
  any byte reaches the game. Keep the list to friends' addresses; avoid wide ranges.
- UDP source addresses can be forged. The allow list narrows who can reach the parser; it is not
  authentication.
- [V code] Reflection is capped: unicast copies of a beacon broadcast go only to senders heard in
  the last second on the beacon socket, at most 4 per broadcast and 2 per second per address. The
  host cannot be used as a traffic amplifier.
- TCP is not filtered. Forward no TCP port.
- Host only while playing: enable the two forwards (or the DMZ) and the firewall rule for the
  session, disable them after, and close `gow2.exe`. Nothing else needs to be exposed for the game.
- Everyone runs the same build. An older or different build can misread the traffic.

## Safer alternatives with no open ports
### Tailscale
Tailscale is a WireGuard mesh with a free personal plan and apps for Windows and Android.
Installing on Windows needs an administrator once. Everyone joins the host's tailnet by invitation
or by sharing the host machine. Peers authenticate through Tailscale; nothing is exposed to the
internet.

Tailscale carries unicast only, not broadcasts, so `net_peers` is required. The host must
advertise its Tailscale address because friends cannot reach its LAN address:
```
host:    gow2.exe --net_link_active=true --net_public_address=<host-100.x.y.z> --net_allow_peers=100.64.0.0/10
friends: gow2.exe --net_link_active=true --net_peers=<host-100.x.y.z>
```
100.64.0.0/10 covers every Tailscale address; Tailscale already decides who is in the tailnet. A
MagicDNS name works in place of the address. On Android, install the Tailscale app, then use this
in `gow2.toml`:
```
net_link_active = true
net_peers = "<host-100.x.y.z>"
```
The app keeps advertising the phone's Wi-Fi address: it ranks wlan0 first and never picks a VPN
interface. For a client, that is harmless. [I] The host's inbound firewall rule ("Windows
Firewall" above) is still needed. When no direct path exists, Tailscale relays traffic with more
latency. [I]

### ZeroTier
ZeroTier is a virtual Ethernet network with a free plan. It passes broadcasts (enabled per network
by default), so System Link discovery can work as on a LAN.

On Windows, the game broadcasts to its local subnet and to 255.255.255.255. Windows sends
255.255.255.255 out of one adapter only (see [NETWORK_PLAY.md](NETWORK_PLAY.md)). Set each PC's
ZeroTier address as its local address:
```
host:    gow2.exe --net_link_active=true --net_local_ip=<host-zerotier-ip>/<prefix>
friends: gow2.exe --net_link_active=true --net_local_ip=<own-zerotier-ip>/<prefix> --net_peers=<host-zerotier-ip>
```
The advertised address, the local network that the allow list always admits, and the directed
broadcast then all use the ZeroTier network, so `net_allow_peers` is not needed. `net_peers` on PC
clients is optional but costs nothing. On Android, install the ZeroTier One app and use this
`gow2.toml`:
```
net_link_active = true
net_local_ip = "<phone-zerotier-ip>/<prefix>"
net_peers = "<host-zerotier-ip>"
```
Broadcasts through the Android VPN app were not tested, hence `net_peers`. [I] Everyone in the
session, including a phone on the host's Wi-Fi, must be on the ZeroTier network. A host
advertising its ZeroTier address is unreachable for devices outside it.

### Comparison
| Option | Open ports | Admin on host | Broadcasts | Friends need |
|---|---|---|---|---|
| Port forwarding | UDP 14001 + 41000 on every router in the path | Router pages + firewall rule | n/a | `net_peers=<host name>` |
| Tailscale | None | Installer + firewall rule | No | Tailscale + `net_peers` |
| ZeroTier | None | Installer + firewall rule | Yes | ZeroTier + `net_local_ip`, `net_peers` |

## Not verified yet
- No System Link session has crossed the internet yet: the forwards, DNS name and firewall rule
  this guide describes have not been tried end to end.
- PC and phone on one LAN joined each other's campaign on 2026-10-15 with the crossplay build
  ([NETWORK_PLAY.md](NETWORK_PLAY.md)); the merged build was verified on loopback (two copies on
  one PC).
- Tailscale and ZeroTier have not been tried.
- A remote NAT that maps a friend's port to an external port below 1024 would be remapped wrongly.
  This is rare.
- There is no version gate.
