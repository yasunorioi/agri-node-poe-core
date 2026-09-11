# Writing a node's `ccm_pub.h` — UECS-CCM conventions

This is the authoring guide for the per-node `ccm_pub.h` that broadcasts a
node's readings as UECS-CCM. The core provides the envelope/datum/send
primitives in [`AgriCCM.h`](../src/AgriCCM.h); the rules below are about how a
node must *use* them so the data actually lands in ArSprout.

## The golden rule: **one `<DATA>` per packet**

Send each datum in its own UECS envelope (its own UDP packet). **Never pack
several `<DATA>` elements into one envelope.**

UECS the spec *allows* multiple `<DATA>` per envelope, but **ArSprout's CCM
receiver keeps only ONE datum per packet** — a multi-datum envelope is silently
truncated (all but one datum vanish, with no error anywhere). Every other CCM
node on a real ArSprout LAN also sends one `<DATA>` per packet; match them.

> Confirmed 2026-06-21: `agri-amp-wifi` sent two datums (region 31 + region 32)
> in one envelope. ArSprout received only the second; the first (region 31)
> disappeared. Splitting into two single-datum packets fixed it immediately.
> The same bug had been silently losing 3 of `agri-flow`'s 4 datums
> (WaterFlow/WaterCons × 2 channels) for a long time.

## Canonical pattern (copy this)

`agri-env-poe` is the reference implementation — one helper, called once per
value:

```cpp
inline bool ccmPublishOne(const char *type, float v, int decimals) {
  char buf[16];
  dtostrf(v, 1, decimals, buf);

  // Append the configured ArSprout node-type suffix: "<Type>.<ntype>"
  // (e.g. InAirTemp.cMC). Empty ntype = bare type.
  String fullType = type;
  if (g_cfg.common.ccm_ntype[0]) { fullType += '.'; fullType += g_cfg.common.ccm_ntype; }

  String xml = agri::ccmEnvelopeOpen();
  xml += agri::ccmDatum(fullType.c_str(),
                        g_cfg.common.ccm_room,
                        g_cfg.common.ccm_region,
                        /*order=*/1,
                        g_cfg.common.ccm_priority,
                        buf);
  xml += agri::ccmEnvelopeClose();
  return agri::ccmSend(xml);          // <-- one packet, one datum
}

inline bool ccmPublish() {
  if (!g_cfg.common.ccm_enabled) return false;
  bool any = false;
  any |= ccmPublishOne("InAirTemp",     g_temp_c,       2);
  any |= ccmPublishOne("InAirHumid",    g_humid_pct,    1);
  any |= ccmPublishOne("InAirPressure", g_pressure_hpa, 2);
  any |= ccmPublishOne("InAirCO2",  (float)g_co2_ppm,   0);
  return any;
}
```

For a multi-channel node where each channel has its own room/region/order, take
those as parameters (see `agri-flow-poe`'s `sendDatum` lambda) — but still
**one `ccmSend()` per datum**.

## Anti-pattern (what NOT to do)

```cpp
// WRONG — ArSprout keeps only one of these and drops the rest.
String xml = agri::ccmEnvelopeOpen();
xml += agri::ccmDatumNT("WaterFlow", ...);
xml += agri::ccmDatumNT("WaterCons", ...);   // silently lost in ArSprout
xml += agri::ccmEnvelopeClose();
agri::ccmSend(xml);
```

## Wire format (handled by `AgriCCM.h`)

```
UDP, port 16520, sent to BOTH 255.255.255.255 (broadcast) and 224.0.0.1 (multicast)
<?xml version="1.0"?><UECS ver="1.00-E10">
  <DATA type="InAirTemp.cMC" room="1" region="13" order="1" priority="1">25.33</DATA>
  <IP>192.168.1.27</IP>
</UECS>
```

- **Broadcast is mandatory for ArSprout** — it ignores 224.0.0.1-only senders
  (see the `CCM_BROADCAST` note in `AgriCCM.h`). `ccmSend()` sends to both.
- The `<IP>` element is required; receivers drop packets without it.
- Type is sent as `<Type>.<ntype>` (e.g. `InAirTemp.cMC`). `ntype` is the
  ArSprout "ノード種別" field; default `cMC`, empty = bare type.

## Field semantics & how ArSprout matches

A datum is identified by **`type` + `room` + `region` + `order`** (`priority` is
only for arbitration between identical senders). ArSprout has one *receive
component* per such tuple — you must register a matching component on the
ArSprout side for every datum you send, or it is ignored.

| field    | meaning |
|----------|---------|
| type     | CCM vocabulary word (`InAirTemp`, `WaterFlow`, `Current`, …) + `.ntype` |
| room     | usually 1 |
| region   | maps to a house on the pi4 `ccm_mqtt_bridge`; **must match the bridge's region→house map AND the ArSprout receive component** (wrong region routes to the wrong house) |
| order    | distinguishes multiple instances of the **same** type (e.g. two flow channels). Same-type datums in different houses are distinguished by region; same region needs distinct order |
| priority | arbitration only |

Common debugging mistake: building the ArSprout receive component by copying
another and forgetting to change `order` (or `region`) — the node sends `order=1`
but the component listens on `order=2`, so nothing arrives even though the packet
is on the wire.

## Verifying on the wire

From a PC on the same subnet, watch the broadcasts directly:

```python
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", 16520))
while True:
    d, a = s.recvfrom(2048)
    print(a[0], d.decode())
```

Each packet should contain exactly **one** `<DATA>`. If you see two, that node
still needs splitting. (On Windows, allow inbound UDP 16520 through the firewall
or the listener gets nothing.)

## Node status (2026-06-21)

| node          | datums | packing |
|---------------|--------|---------|
| agri-env-poe  | InAirTemp/Humid/Pressure/CO2 | ✅ 1/packet (reference) |
| agri-flow-poe | WaterFlow/WaterCons × 2ch     | ✅ fixed v0.5.1 (was multi-datum) |
| agri-amp-wifi | Current × 2ch                | ✅ fixed v0.4.2 (was multi-datum) |
| agri-rain-poe | WRainfallAmt                 | ✅ single value |
| agri-drain-poe| Drainage                     | ✅ single value |

## Authoring checklist for a new node

- [ ] One `ccmSend()` per datum (one `<DATA>` per packet).
- [ ] Type uses the right CCM vocabulary word + `.ntype` suffix.
- [ ] `region` agreed with the pi4 bridge's region→house map.
- [ ] `order` unique per same-type datum.
- [ ] CCM default **off** in config; enable per-house only after the region is
      confirmed (a wrong region publishes to the wrong house — see the `.165`
      mis-map incident).
- [ ] Register a matching receive component in ArSprout for each datum.
- [ ] Capture UDP 16520 and confirm one `<DATA>` per packet before declaring done.
