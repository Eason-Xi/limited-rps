<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Limited RPS hub (kj_hub)

The computer-side service that lets every AI Passport play over the venue Wi-Fi: it relays game frames between
devices, serves the page phones use to register a nickname by QR code, streams the host's game to the browser
dashboard, and records every game for CSV export. It needs only the Python 3.9+ standard library; nothing to
install.

The hub is optional. The firmware's default **direct** mode plays without any computer or router (see the
[project README](../../README.md#two-ways-to-connect)). To play through the hub, switch every device to it under
"Settings → switch to the computer service"; the device restarts and, if it has never been set up, opens the Wi-Fi
setup hotspot. Devices in direct mode never contact the hub.

## Running

```bash
python3 tools/kj_hub/hub.py
```

At startup the terminal prints:

- the local dashboard `http://127.0.0.1:47180/board` (opened in the browser automatically; `--no-browser` skips
  that);
- the hub home page `http://127.0.0.1:47180/` (device list, renaming, clearing nicknames, exports);
- a dashboard link for other computers or tablets (with a `?k=…` token — give it only to the host / organizer);
- the computer's LAN address and the data directory.

Press Ctrl+C to stop. While the hub is stopped or the computer sleeps the whole table pauses; after a restart the
devices find it again automatically.

| Option | Meaning |
| --- | --- |
| `--data DIR` | Data directory, default `~/kj-hub-data`. |
| `--http PORT` | Web port, default 47180 (registration page and dashboard; devices learn it from the OFFER). |
| `--tcp PORT` | Port the host's dashboard link connects to, default 47103 (devices learn it from the OFFER). |
| `--udp PORT` | Port for device datagrams, default 47101. The firmware always sends to 47101; leave it. |
| `--subnet CIDR` | Subnet for directed broadcasts, e.g. `192.168.1.0/24` (default: the /24 of this computer). |
| `--sweep CIDR` | Every 15 s, unicast an OFFER to every address in this subnet. Use it when the router drops broadcasts. |
| `--bind ADDR` | Listen on one interface only; default all. |
| `-v` | Debug logging. |

## Network requirements

- Devices support **2.4 GHz** Wi-Fi only. Bring your own router if you can, and connect the computer to it with a
  **cable**.
- Devices must be able to reach the computer: guest networks, captive-portal networks and networks with "AP /
  client isolation" usually do not work.
- A phone hotspot is only good for a few devices while testing (iOS hotspots usually allow about 5).
- Home routers can become unstable beyond about 30 clients; for big groups use a better access point and make
  sure the DHCP pool is large enough.
- Phones must be on the **same Wi-Fi** to open the registration page.

### Firewall

- **macOS**: the first run asks whether python3 may accept incoming network connections — allow it. If you
  denied it earlier, change Python to "allow" under System Settings → Network → Firewall → Options.
- **Windows**: mark the current network as "Private" and allow Python on private networks when the firewall
  asks.
- Turn off VPNs that capture all traffic.

## How devices find the computer

1. On Wi-Fi a device broadcasts a DISCOVER (UDP 47101) every second; if it remembers the last hub address it
   alternates the broadcast with a unicast to it.
2. The hub answers with an OFFER (web and dashboard ports, the device's nickname) and also broadcasts an OFFER to
   the subnet every 2 seconds.
3. Once found, the device sends a keepalive every 5 seconds and starts searching again after 12 seconds without
   any datagram from the hub.

If the router drops broadcasts: expand "Advanced" on the device's Wi-Fi setup page and enter this computer's IP,
or start the hub with `--sweep 192.168.1.0/24`.

## Nickname registration

- When a player device has no nickname (or under Settings → register nickname), it shows a QR code for
  `http://<computer>:47180/j/<one-time code>`. Scan it with **WeChat** or the phone **camera** and enter a nickname
  on the page.
- Nickname rules: at most 24 bytes and a display width of 16 (Chinese characters count 2, letters 1 — about 8
  Chinese characters), only characters the device font has (`tools/kj_charset.py`: printable ASCII, the middle
  dot, GB2312 Chinese characters and a few common name characters), and no duplicates. Characters the device
  cannot show are listed one by one so the player can pick another name.
- One-time codes expire after 15 minutes; pressing OK on the device shows a new QR code.
- The hub's registry is authoritative: renaming or clearing on the dashboard or home page reaches the device
  within seconds. Devices also remember their own nickname, so after switching computers or deleting the data
  directory the nickname is restored when the device reconnects.
- WeChat may first warn that the page is not an official WeChat page; choose to continue. On iOS WeChat needs
  "Local Network" permission; if the page does not open, scan with the system camera instead (it opens Safari).

## Dashboard

- Open `http://127.0.0.1:47180/board`. The header shows whether the host is connected; with several games on one
  hub you can switch between them.
- It shows everyone's locked cards, so it is **for the host and the audience only**: the dashboard and the admin
  APIs open directly only on the computer running the hub; other devices need the token link printed in the
  terminal (the token is kept in `config.json` in the data directory).
- Nicknames on the player cards can be edited in place; changes go to the registry and sync to the devices.
- Everything else matches the standalone dashboard: start / end / new game / add or remove computer players /
  reset / kick, time limit, cover (H) and demo.

## Data and export

The data directory (default `~/kj-hub-data`) contains personal data such as nicknames — **never commit it**; delete
the whole directory after the event if you like.

| File | Contents |
| --- | --- |
| `config.json` | Hub ID and dashboard token |
| `registry.json` | Device → nickname registry (deleted names keep a "deleted" mark so devices clear them) |
| `registry.log.jsonl` | Rename history |
| `sessions/<start>_<game>_g<round>.jsonl` | Each game's summaries, player state changes and events, one JSON per line |
| `hub.log` | Log file (rotated automatically) |

The home page exports CSV (UTF-8 with BOM, opens directly in Excel / WPS):

- **Player results**: game, round, seat, nickname, device, status, stars, remaining cards of each kind, wins /
  losses / draws, final reason;
- **Game events**: time, event, both seats and nicknames, cards, winner, duel ID, extra value (the press-time gap
  of a bump match);
- **Nickname registry**.

## Troubleshooting

| Symptom | Check first |
| --- | --- |
| The title screen says "direct · no computer needed" and the device never shows up | The device is still in direct mode: choose "Settings → switch to the computer service" |
| Device keeps "searching for the hub" | Is the hub running; same Wi-Fi as the computer; firewall; AP isolation; try the computer IP under "Advanced" on the setup page, or `--sweep` |
| Device keeps "connecting to Wi-Fi" | Is it 2.4 GHz; is the password right (Settings → redo Wi-Fi); too far from the router |
| Phone cannot open the registration page | Same Wi-Fi as the computer; scan with the system camera; the computer's firewall |
| Dashboard says the host is not connected | Has the host device chosen the host role; does its footer say the dashboard link is up |
| Dashboard returns 403 | Not the computer running the hub: use the token link from the terminal |
| Device reports a version mismatch | Device firmware and hub are different versions; update both to the same version |

## Code layout

| File | Role |
| --- | --- |
| `hub.py` | Entry point: options, logging, network and web threads |
| `wire.py` | Wire protocol matching `main/kj_hubproto.h` (golden vectors shared with C: `tests/data/kj_hub_vectors.txt`) |
| `core.py` | Core logic: device table and relay, discovery, nicknames and registration, board-line parsing, records, SSE (no sockets; testable) |
| `netio.py` | `selectors` network loop: UDP relay / discovery, host dashboard TCP |
| `web.py` | Web: registration page, dashboard, admin APIs (local or token only), SSE, CSV export |
| `store.py` | Data directory: config, registry, game records, CSV |
| `static/` | Registration, result, expired and home pages |

Tests: `python3 tests/test_kj_hub.py` (also run by `tools/validate.sh`).
