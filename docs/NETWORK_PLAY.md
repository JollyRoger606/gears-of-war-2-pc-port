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

## Troubleshooting

Turn on `system_link_trace` and look for these log lines:

- `[netlink] local address ... (default route)`
- `[netlink] resolved ...`
- `[netlink] broadcasts to port 14001 go out to [...]`
- `[netlink] dropped ... (net_allow_peers)`
- `[netlink] XNetXnAddrToInAddr ... -> ...`

If the PC advertises 127.0.0.1, set `net_local_ip` to its LAN address.
