// AgriCCM.h — UECS-CCM envelope builder + per-datum helper.
//
// Packet shape (matches ccm_rp2350_relay / OGMS):
//   <UECS ver="1.00-E10">
//     <DATA type="InAirTemp.cMC" room="1" region="11" order="1"
//           priority="29" lv="S" cast="uni">23.5</DATA>
//     ...
//   </UECS>
//
// Usage
//   agri::ccmBegin();              // open UDP socket (once, after DHCP)
//   String x = agri::ccmEnvelopeOpen();
//   x += agri::ccmDatum("InAirTemp", cfg.ccm_room, cfg.ccm_region,
//                       cfg.ccm_order_temp, cfg.ccm_priority, "23.50");
//   x += agri::ccmEnvelopeClose();
//   agri::ccmSend(x);

#pragma once

#include <Arduino.h>
#include <NetworkUdp.h>
#include <ETH.h>
#include "AgriCommonConfig.h"

namespace agri {

inline const IPAddress  CCM_MULTICAST(224, 0, 0, 1);
// ArSprout Pi's CCM receiver listens on the LIMITED BROADCAST address, not
// the UECS multicast group. A node that only sends to 224.0.0.1 is silently
// ignored by ArSprout (this is why agri-env's InAirTemp never registered,
// despite byte-perfect packets — confirmed 2026-06-10 against ArSprout Pi
// 1.17 with a packet emulator: same packet to 255.255.255.255 was ingested
// within seconds, to 224.0.0.1 never). We send to both so the node works
// with ArSprout (broadcast) and spec-compliant multicast managers alike.
inline const IPAddress  CCM_BROADCAST(255, 255, 255, 255);
inline constexpr uint16_t CCM_PORT       = 16520;
inline const char *const UECS_VERSION    = "1.00-E10";

inline NetworkUDP &ccmSocket() {
  static NetworkUDP s;
  return s;
}

inline void ccmBegin() {
  // Send-only. begin(0) opens an ephemeral UDP socket. beginMulticast()
  // would additionally call igmp_joingroup(), which errors with
  // "could not join igmp: 125" on 224.0.0.1 (every netif implicitly
  // joins the all-hosts group).
  ccmSocket().begin(0);
}

inline String ccmEnvelopeOpen() {
  String s;
  s.reserve(64);
  s  = "<?xml version=\"1.0\"?><UECS ver=\"";
  s += UECS_VERSION;
  s += "\">";
  return s;
}

// UECS-CCM requires the sender <IP> element; receivers (e.g. ArSprout)
// drop packets without it.
inline String ccmEnvelopeClose() {
  String s = "<IP>";
  s += ETH.localIP().toString();
  s += "</IP></UECS>";
  return s;
}

// Build a single `<DATA>` element. `region` is taken as an argument
// instead of read from CommonConfig so the project can apply per-sensor
// offsets (e.g. WRainfallAmt uses region + 30).
inline String ccmDatum(const char *type, int room, int region, int order,
                       int priority, const char *value) {
  String s;
  s.reserve(160);
  s  = "<DATA type=\"";
  s += type;
  s += "\" room=\"";
  s += room;
  s += "\" region=\"";
  s += region;
  s += "\" order=\"";
  s += order;
  s += "\" priority=\"";
  s += priority;
  s += "\">";
  s += value;
  s += "</DATA>";
  return s;
}

// Like ccmDatum but appends the configured UECS node-type suffix to the
// type: "<type>.<ntype>" (e.g. "Drainage.cMC"). Empty ntype = bare type.
// Pass g_cfg.common.ccm_ntype. Lets every node match ArSprout's
// "CCM識別子 + ノード種別" receive form without per-node string plumbing.
inline String ccmDatumNT(const char *type, const char *ntype,
                         int room, int region, int order,
                         int priority, const char *value) {
  String t = type;
  if (ntype && ntype[0]) { t += '.'; t += ntype; }
  return ccmDatum(t.c_str(), room, region, order, priority, value);
}

inline bool ccmSendTo(const IPAddress &dest, const String &xml) {
  NetworkUDP &u = ccmSocket();
  if (!u.beginPacket(dest, CCM_PORT)) return false;
  u.write((const uint8_t*)xml.c_str(), xml.length());
  return u.endPacket();
}

inline bool ccmSend(const String &xml) {
  // Broadcast is what reaches ArSprout; multicast keeps spec-compliant
  // managers working. Succeed if either leaves the wire.
  bool bc = ccmSendTo(CCM_BROADCAST, xml);
  bool mc = ccmSendTo(CCM_MULTICAST, xml);
  bool ok = bc || mc;
  if (ok) Serial.printf("[CCM] TX %u bytes (bc=%d mc=%d)\n",
                        (unsigned)xml.length(), bc, mc);
  return ok;
}

// UECS node-condition heartbeat ("cnd"). Sent each CCM cycle as a liveness
// signal; region should match the node's data region. NOTE: ArSprout does
// NOT require cnd to accept a node's data CCM — the real gate was the
// destination address (see CCM_BROADCAST above). cnd is kept for spec
// completeness and managers that display node liveness.
inline bool ccmAnnounce(int room, int region, int order, int priority) {
  String x = ccmEnvelopeOpen();
  x += ccmDatum("cnd.cMC", room, region, order, priority, "0");
  x += ccmEnvelopeClose();
  return ccmSend(x);
}

} // namespace agri
