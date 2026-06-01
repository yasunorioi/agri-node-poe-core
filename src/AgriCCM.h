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
  s += "\" lv=\"S\" cast=\"uni\">";
  s += value;
  s += "</DATA>";
  return s;
}

inline bool ccmSend(const String &xml) {
  NetworkUDP &u = ccmSocket();
  if (!u.beginPacket(CCM_MULTICAST, CCM_PORT)) return false;
  u.write((const uint8_t*)xml.c_str(), xml.length());
  bool ok = u.endPacket();
  if (ok) Serial.printf("[CCM] TX %u bytes\n", (unsigned)xml.length());
  return ok;
}

} // namespace agri
