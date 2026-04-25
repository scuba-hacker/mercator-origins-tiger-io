# ESP-NOW Fixes for espressif32 6.x (ESP-IDF 5.x)

These changes were required on Tiger (M5StickC Plus, `espressif32@6.3.0`) after upgrading
from an unpinned `platform = espressif32`. The same fixes are needed on any M5StickC Plus
project using ESP-NOW with this platform version.

---

## Problem 1: WiFi fails to initialise (ESP_ERR_NO_MEM)

### Symptoms

Serial output at startup:
```
wifi:Expected to init 4 rx buffer, actual is 1
[E][WiFiGeneric.cpp:680] wifiLowLevelInit(): esp_wifi_init 257
wifi:Expected to init 4 rx buffer, actual is 0
[E][WiFiGeneric.cpp:680] wifiLowLevelInit(): esp_wifi_init 257
[E][WiFiAP.cpp:154] softAP(): enable AP first!
AP Config failed.
```

ESP-NOW send diagnostic (if added) shows `WiFiMode=0 WiFiCh=0` — WiFi is completely off.
ESP-NOW init and peer pairing appear to succeed, but all sends fail silently.

### Root Cause

In ESP-IDF 5.x, `esp_wifi_init()` requires more contiguous internal DRAM for its RX
buffers than earlier versions. On M5StickC Plus (no PSRAM — all heap is internal DRAM),
if heavy allocations happen before WiFi initialises, there is insufficient contiguous
memory left.

The culprit allocations:
- `clockSprite->createSprite(135, 170)` — 135×170×2 = ~45KB of internal DRAM
- `MapScreen_M5` construction — further heap

With these allocated first, the largest free heap block drops to ~11KB. WiFi needs more
than this, fails with error 257 (`ESP_ERR_NO_MEM`), and the radio never starts.

The insidious part: `esp_now_init()` and `esp_now_add_peer()` both succeed anyway because
ESP-NOW's bookkeeping layer doesn't require the WiFi radio to be running. So `isPairedWithMako`
and `ESPNowActive` are both true — everything looks fine until the first send.

### Fix

**Move WiFi/ESP-NOW initialisation to before heavy heap allocations in `setup()`.**

`readPreferencesFromEEPROM()` must still be called first (needed for `reedSwitchesPrimaryControl`
which is used inside `configAndStartUpESPNow()`), and the ESP-NOW receive queue must be
created before the call, but both are small and do not fragment the heap significantly.

```cpp
void setup()
{
  if (systemStartupAndCheckForOTADemand()) return;

  delay(1500);
  BUFFER_LOG_RESET();

  // Init WiFi/ESP-NOW before heavy heap allocations (sprite ~45KB, mapScreen) so
  // the WiFi driver can allocate its RX buffers from unfragmented internal DRAM.
  readPreferencesFromEEPROM();

  espNOW_msgsReceivedQueue = xQueueCreate(queueLength, sizeof(rxQueueItemBuffer));

  if (enableESPNow && espNOW_msgsReceivedQueue)
    configAndStartUpESPNow();

  // Heavy allocations AFTER WiFi is up
  clockSprite = std::make_shared<TFT_eSprite>(&M5.Lcd);
  clockSprite->createSprite(135, 170);

  mapScreen = std::make_unique<MapScreen_M5>(M5.Lcd);
  // ... rest of setup
}
```

**Result:** Largest free heap block jumps from ~11KB to ~32KB after the reorder.
WiFi initialises cleanly and `WiFiMode=3 (APSTA) WiFiCh=1` is confirmed at send time.

---

## Problem 2: "Peer channel is not equal to the home channel" (ESP_ERR_ESPNOW_ARG)

### Symptoms

```
E (xxxxx) ESPNOW: Peer channel is not equal to the home channel, send fail!
ESPNOW Invalid Argument
```

Only appears after Problem 1 is fixed (WiFi must be running for this check to trigger).

### Root Cause

In ESP-IDF 5.x, `esp_now_send()` strictly validates that the channel stored in the peer's
`esp_now_peer_info_t::channel` field matches the WiFi radio's current home channel. In
earlier ESP-IDF versions this check was lenient or absent.

This was masked on Tiger because Problem 1 meant WiFi was never running — once WiFi is
fixed, this check may surface if anything causes the radio's home channel to drift from
the registered peer channel.

### Resolution

On Tiger, once Problem 1 was fixed the home channel was stable at 1 until startup NTP was
enabled. The NTP path connects as a STA to infrastructure WiFi, which moves the radio
home channel to the router's channel. It then disabled WiFi while leaving the ESP-NOW
state and peer channel from the earlier startup initialisation.

Fix the NTP path by treating it as an interruption of ESP-NOW:

1. Stop/deinit ESP-NOW before connecting to infrastructure WiFi for NTP.
2. Fetch NTP and update the RTC.
3. Disconnect STA WiFi and set `WIFI_OFF`.
4. Restart ESP-NOW so the SoftAP and peer are rebuilt on `ESPNOW_CHANNEL`.

This makes the radio home channel and the peer's registered channel match again before
the next `esp_now_send()`.

---

## Diagnostic Print

Add this immediately before `esp_now_send()` in any send function to confirm WiFi state:

```cpp
USB_SERIAL_PRINTF("ESPNow send diag: WiFiMode=%d WiFiCh=%d peerCh=%d peerIfidx=%d ESPNowActive=%d\n",
  WiFi.getMode(), WiFi.channel(), ESPNow_mako_peer.channel, ESPNow_mako_peer.ifidx, ESPNowActive);
```

Expected values when healthy:
- `WiFiMode=3` — WIFI_AP_STA
- `WiFiCh=1` — home channel matches `ESPNOW_CHANNEL`
- `peerCh=1` — peer registered with `ESPNOW_CHANNEL`
- `peerIfidx=0` — STA interface (default, correct for sending)
- `ESPNowActive=1`

---

## Error Code Reference

Add `ESP_ERR_ESPNOW_IF` to your send result handler — it is missing from ESP-IDF 4.x
error lists but present in 5.x:

```cpp
void toSerialESPNowSendDataResult(const esp_err_t result)
{
  if (result == ESP_OK)
    USB_SERIAL_PRINTLN("ESP_OK");
  else if (result == ESP_ERR_ESPNOW_NOT_INIT)
    USB_SERIAL_PRINTLN("ESP_NOT_INIT");
  else if (result == ESP_ERR_ESPNOW_ARG)
    USB_SERIAL_PRINTLN("ESPNOW Invalid Argument");
  else if (result == ESP_ERR_ESPNOW_INTERNAL)
    USB_SERIAL_PRINTLN("ESPNOW Internal Error");
  else if (result == ESP_ERR_ESPNOW_NO_MEM)
    USB_SERIAL_PRINTLN("ESPNOW No Memory");
  else if (result == ESP_ERR_ESPNOW_NOT_FOUND)
    USB_SERIAL_PRINTLN("ESPNOW Peer not found.");
  else if (result == ESP_ERR_ESPNOW_IF)
    USB_SERIAL_PRINTLN("ESPNOW Interface error (wrong ifidx or interface not ready).");
  else
    USB_SERIAL_PRINTF("ESPNOW Unknown Error: 0x%x\n", result);
}
```

`ESP_ERR_ESPNOW_IF` (0x306c) means the WiFi interface is not ready — usually indicates
Problem 1 (WiFi never initialised due to OOM).

---

## Summary of Changes for Mako

| File | Change |
|---|---|
| `setup()` in main .cpp | Move `readPreferencesFromEEPROM()`, queue creation, and `configAndStartUpESPNow()` to before sprite and mapScreen allocations |
| ESP-NOW send result handler | Add `ESP_ERR_ESPNOW_IF` case; change unknown error to print hex code |
| `platformio.ini` | Pin `platform = espressif32@6.5.0` (Mako's version) and do not change — keep Tiger and Mako on their respective pinned versions |
