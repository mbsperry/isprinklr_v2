#include "net.h"

#include "config.h"

#ifndef NET_USE_ETHERNET
#define NET_USE_ETHERNET 1
#endif

#if NET_USE_ETHERNET
#include <ETH.h>
#else
#include <WiFi.h>
#endif

namespace net {
namespace {

bool hasAddress() {
#if NET_USE_ETHERNET
  return ETH.localIP()[0] != 0;
#else
  return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
#endif
}

bool waitForAddress(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (hasAddress()) return true;
    delay(250);
  }
  return hasAddress();
}

}  // namespace

bool begin() {
#if NET_USE_ETHERNET
  Serial.println("[net] starting Ethernet (W5500)");
  if (!ETH.begin(ETH_PHY_W5500, config::kEthAddr, config::kEthCsPin,
                 config::kEthIntPin, config::kEthRstPin, SPI3_HOST,
                 config::kEthSclkPin, config::kEthMisoPin,
                 config::kEthMosiPin)) {
    Serial.println("[net] ETH.begin() failed");
    return false;
  }
#else
  Serial.println("[net] starting WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

  if (!waitForAddress(config::kNetworkTimeoutMs)) {
    Serial.println("[net] no address before timeout");
    return false;
  }

  Serial.printf("[net] up: %s %s\n", transport(), localIP().toString().c_str());
  return true;
}

bool isUp() { return hasAddress(); }

void reconnect() {
#if NET_USE_ETHERNET
  // The W5500 driver reacquires DHCP on its own once the link returns; there is
  // no safe re-begin() without tearing down SPI. Log and let it recover.
  Serial.println("[net] link down, waiting for Ethernet to recover");
#else
  Serial.println("[net] reconnecting WiFi");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
}

IPAddress localIP() {
#if NET_USE_ETHERNET
  return ETH.localIP();
#else
  return WiFi.localIP();
#endif
}

const char* transport() {
#if NET_USE_ETHERNET
  return "Ethernet";
#else
  return "WiFi";
#endif
}

void describe(JsonObject obj) {
  obj["transport"] = transport();
  obj["up"] = isUp();
  obj["ip"] = localIP().toString();

#if NET_USE_ETHERNET
  obj["mac"] = ETH.macAddress();
  obj["gateway"] = ETH.gatewayIP().toString();
  obj["subnet"] = ETH.subnetMask().toString();
  obj["link_speed_mbps"] = ETH.linkSpeed();
  obj["full_duplex"] = ETH.fullDuplex();
#else
  obj["ssid"] = WiFi.SSID();
  obj["rssi"] = WiFi.RSSI();
  obj["gateway"] = WiFi.gatewayIP().toString();
  obj["subnet"] = WiFi.subnetMask().toString();
#endif
}

}  // namespace net
