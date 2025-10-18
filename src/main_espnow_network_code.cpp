#ifdef BUILD_INCLUDE_MAIN_ESPNOW_NETWORK_CODE

const char podTigerHostName[] = "Tiger";
const char testTigerHostName[] = "Tiger-Test";
const char *tigerHostName = podTigerHostName;
const char makoHostName[] = "Mako";
const char oceanicHostName[] = "Oceanic";
const char silkyHostName[] = "Silky";

////////////////////////////////////////////////////////////////////////
///////////////////////////////// ESPNow Network Functions
////////////////////////////////////////////////////////////////////////
// Tiger STA Mac shown in router for Tiger: E8:9F:6D:09:3A:E8
// Tiger Soft AP for ESP Now: E8:9F:6D:09:3A:E9

uint8_t MAKO_MAC[6] = {0x94, 0xB9, 0x7E, 0xAC, 0xF5, 0x45}; // BSSID: the AP Mac, not STA Mac shown in router

bool pairWithKnownMAC(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const uint8_t* knownMAC);
void scanNetworksNoOp();

void configAndStartUpESPNow()
{  
  memset(&ESPNow_mako_peer, 0, sizeof(esp_now_peer_info_t));

  /*

  This properly initializes both interfaces from the start, eliminating the need for the dummy scan. ESP-NOW
  needs both AP (for hosting) and STA (for scanning/sending) interfaces to be active.
  Before this was being set to WIFI_AP. This is not sufficient for ESP Now.
  However, this was being masked by a call to WiFi.ScanNetworks() which enabled STA mode in addition to
  the existing WIFI_AP mode that was explicitly called here.
  If WIFI_AP mode is set here then the scanNetworksNoOp() fn has to be called before doing ESP Now setup.
  
  - ESP-NOW requires both AP and STA interfaces to be properly initialized for bidirectional communication

  When pairWithPeer works:
  - ESPNowScanForPeer() calls WiFi.scanNetworks()
  - This internally initializes the STA interface and puts WiFi in WIFI_AP_STA mode
  - Now ESP-NOW can send data properly

  When pairWithKnownMAC fails:
  - No scan occurs, so STA interface remains uninitialized
  - ESP-NOW pairing succeeds but sending fails because the STA interface isn't ready

  This properly initializes both interfaces from the start, eliminating the need for the dummy scan. ESP-NOW
  needs both AP (for hosting) and STA (for scanning/sending) interfaces to be active

Interesting that when it only has WIFI_AP it is still able to receive messages without being paired.

⏺ Yes, that's a key observation! ESP-NOW receiving works with just WIFI_AP mode because:

  Receiving (works with AP-only):
  - ESP-NOW can receive packets on the AP interface
  - The ESP32 listens for ESP-NOW packets on its AP radio
  - No additional interface initialization needed for incoming data

  Sending (requires STA interface):
  - ESP-NOW sending requires the STA interface to be active
  - The STA interface handles the transmission of ESP-NOW packets
  - Without STA mode, esp_now_send() fails with "Unknown Error"

  This asymmetric behavior explains why:
  - Your logs showed successful message reception even before pairing
  - Pairing worked (it's just adding peer info to a table)
  - But sending failed until the STA interface was initialized

  It's a subtle ESP32/ESP-NOW implementation detail - the receive path uses the AP radio, but the transmit path
  requires both interfaces to be properly configured in WIFI_AP_STA mode.
  */

  WiFi.mode(WIFI_AP_STA);
  
  tigerHostName = (reedSwitchesPrimaryControl ? podTigerHostName : testTigerHostName);

  // configure device AP mode
  String Prefix = String(tigerHostName) + ":";
  String Mac = WiFi.softAPmacAddress();
  String SSID = Prefix + Mac;
  String Password = "123456789";
  bool result = WiFi.softAP(SSID.c_str(), Password.c_str(), ESPNOW_CHANNEL, 0);

  if (!result)
  {
    USB_SERIAL_PRINTLN("AP Config failed.");
  }
  else
  {
    USB_SERIAL_PRINTF("AP Config Success. Broadcasting with AP: %s\n",String(SSID).c_str());
    USB_SERIAL_PRINTF("WiFi Channel: %d\n",WiFi.channel());
  }
  // This is the mac address of this peer in AP Mode
  USB_SERIAL_PRINTF("AP MAC: %s\n",WiFi.softAPmacAddress().c_str()); 

  if (esp_now_init() == ESP_OK)
  {
    USB_SERIAL_PRINTLN("configAndStartUpESPNow esp_now_init Success");
    ESPNowActive = true;
  }
  else
  {
    USB_SERIAL_PRINTLN("configAndStartUpESPNow: esp_now_init Failed");
    ESPNowActive = false;
  }
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info.
  esp_now_register_send_cb(OnESPNowDataSent);
  esp_now_register_recv_cb(OnESPNowDataRecv);
  
  pairWithMako();
}

bool pairWithMako()
{
  const bool pairWithKnown = true;

  if (ESPNowActive && !isPairedWithMako)
  {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Lcd.setCursor(0,0);
    const int pairAttempts = 5;
    
    if (pairWithKnown)
    {
      // enable fast pairing
      // Mako is used as an extra private attribute that is set for our purposes - it's not used by ESPNow
      // but can identify the source of the data.
      isPairedWithMako = pairWithKnownMAC(ESPNow_mako_peer,"Mako",MAKO_MAC);
    }
    else
    {
      isPairedWithMako = pairWithPeer(ESPNow_mako_peer,"Mako",pairAttempts); // 5 connection attempts
    }

    M5.Lcd.fillScreen(TFT_BLACK);
  }

  return isPairedWithMako;
}

bool pairWithKnownMAC(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const uint8_t* mac_addr)
{
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  USB_SERIAL_PRINTF("pairWithKnownMAC: %s %s\n",peerSSIDPrefix, macStr);

  // Skip scanning - use known MAC directly
  // IMPORTANT: Reset peer structure first (same as ESPNowScanForPeer does)
  memset(&peer, 0, sizeof(peer));
  
  // Setup peer structure with known MAC
  memcpy(peer.peer_addr, mac_addr, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = 0;
  peer.priv = (void*)peerSSIDPrefix;        // This is for our own purpose - ESPNow doesn't use this.
  
  USB_SERIAL_PRINTF("pairWithKnownMAC: peer.peer_addr = %02X:%02X:%02X:%02X:%02X:%02X\n",
        peer.peer_addr[0],
        peer.peer_addr[1],
        peer.peer_addr[2],
        peer.peer_addr[3],
        peer.peer_addr[4],
        peer.peer_addr[5]);
              
  return ESPNowManagePeer(peer);
}

// The prior method of scanning SSIDs and finding MAC address of the AP of interest.
bool pairWithPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, int maxAttempts)
{
  memset(&peer, 0, sizeof(peer));

  bool isPaired = false;
  while(maxAttempts-- && !isPaired)
  {
    USB_SERIAL_PRINTF("pairWithPeer: Scan for Peer: %s\n",peerSSIDPrefix);
    bool result = ESPNowScanForPeer(peer,peerSSIDPrefix);

    // check if peer channel is defined
    if (result && peer.channel == ESPNOW_CHANNEL)
    { 
      isPaired = ESPNowManagePeer(peer);
      M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
      M5.Lcd.printf("pairWithPeer: %s Pair\nok\n",peerSSIDPrefix);
      M5.Lcd.setTextColor(TFT_WHITE);
      USB_SERIAL_PRINTF("pairWithPeer: Peer Found and Paired: %s\n",peerSSIDPrefix);

    }
    else
    {
      peer.channel = ESPNOW_NO_PEER_CHANNEL_FLAG;
      M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
      M5.Lcd.printf("pairWithPeer: %s Pair\nfail\n",peerSSIDPrefix);
      USB_SERIAL_PRINTF("pairWithPeer: Pair Fail: %s\n",peerSSIDPrefix);
      M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    }
  }

  delay(1000);
  
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setCursor(0,0);
  
  return isPaired;
}

const uint8_t ESPNOW_PRINTSCANRESULTS = 1;

// Mitigation needed for fixing Send failing due to no WIFI_AP_STA mode being set before
void scanNetworksNoOp()
{
  USB_SERIAL_PRINTLN("scanNetworksNoOp: scanNetworks");
  WiFi.scanNetworks();
  USB_SERIAL_PRINTLN("scanNetworksNoOp: scanDelete");
  WiFi.scanDelete();
}

// Scan for peers in AP mode
bool ESPNowScanForPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix)
{
  bool peerFound = false;
  
  M5.Lcd.printf("ESPNowScanForPeer: Scan For\n%s\n",peerSSIDPrefix);
  int8_t scanResults = WiFi.scanNetworks();
  
  // reset on each scan 
  memset(&peer, 0, sizeof(peer));

  USB_SERIAL_PRINTLN("");

  if (scanResults == 0) 
  {   
    USB_SERIAL_PRINTLN("ESPNowScanForPeer: No WiFi devices in AP Mode found");
    peer.channel = ESPNOW_NO_PEER_CHANNEL_FLAG;
  } 
  else 
  {
    USB_SERIAL_PRINTF("ESPNowScanForPeer: Found %i devices in AP Mode\n",scanResults); 
    
    for (int i = 0; i < scanResults; ++i) 
    {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);

      if (ESPNOW_PRINTSCANRESULTS) 
      {
        USB_SERIAL_PRINT("Item #");
        USB_SERIAL_PRINT(i + 1);
        USB_SERIAL_PRINT(": ");
        USB_SERIAL_PRINT(SSID);
        USB_SERIAL_PRINT(" (");
        USB_SERIAL_PRINT(RSSI);
        USB_SERIAL_PRINT(")");
        USB_SERIAL_PRINTLN("");
      }
      
      delay(10);
      
      // Check if the current device starts with the peerSSIDPrefix
      if (SSID.indexOf(peerSSIDPrefix) == 0) 
      {
        // SSID of interest
        USB_SERIAL_PRINTF("ESPNowScanForPeer: Found a peer: %s (peerSSIDPrefix: %s)\n", SSID.c_str(),peerSSIDPrefix);
        USB_SERIAL_PRINT(i + 1); USB_SERIAL_PRINT(": "); USB_SERIAL_PRINT(SSID); USB_SERIAL_PRINT(" ["); USB_SERIAL_PRINT(BSSIDstr); USB_SERIAL_PRINT("]"); USB_SERIAL_PRINT(" ("); USB_SERIAL_PRINT(RSSI); USB_SERIAL_PRINT(")"); USB_SERIAL_PRINTLN("");
                
        // Get BSSID => Mac Address of the Slave
        int mac[6];
        if ( 6 == sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x",  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5] ) ) 
        {
          for (int ii = 0; ii < 6; ++ii ) 
          {
            peer.peer_addr[ii] = (uint8_t) mac[ii];     //  SET PEER FIELD
          }
        }

        peer.channel = ESPNOW_CHANNEL;                  //  SET PEER FIELD
        peer.encrypt = 0;                               //  SET PEER FIELD - no encryption
        peer.priv = (void*)peerSSIDPrefix;              //  SET PEER FIELD - distinguish between different peers

        USB_SERIAL_PRINTF("ESPNowScanForPeer: peer.peer_addr = %02X:%02X:%02X:%02X:%02X:%02X\n",
              peer.peer_addr[0],
              peer.peer_addr[1],
              peer.peer_addr[2],
              peer.peer_addr[3],
              peer.peer_addr[4],
              peer.peer_addr[5]);

        peerFound = true;
        break;
      }
    }
  }

  // clean up ram
  WiFi.scanDelete();

  return peerFound;
}

// Check if the peer is already paired with the master.
// If not, pair the peer with master
bool ESPNowManagePeer(esp_now_peer_info_t& peer)
{
  bool result = false;

  if (peer.channel == ESPNOW_CHANNEL)
  {
    if (ESPNOW_DELETEBEFOREPAIR)
    {
      ESPNowDeletePeer(peer);
    }

    USB_SERIAL_PRINT("ESPNowManagePeer: Peer Status: ");

    // check if the peer exists
    bool exists = esp_now_is_peer_exist(peer.peer_addr);

    if (exists)
    {
      // Peer already paired.
      USB_SERIAL_PRINTLN("ESPNowManagePeer: Already Paired");

      M5.Lcd.println("Already paired");
      result = true;
    }
    else
    {
      USB_SERIAL_PRINTLN("ESPNowManagePeer: Not already Paired");

      // Peer not paired, attempt pair
      esp_err_t addStatus = esp_now_add_peer(&peer);

      if (addStatus == ESP_OK)
      {
        // Pair success
        USB_SERIAL_PRINTLN("ESPNowManagePeer: Pair success");
        M5.Lcd.println("Pair success");
        result = true;
      }
      else if (addStatus == ESP_ERR_ESPNOW_EXIST)
      {
        USB_SERIAL_PRINTLN("ESPNowManagePeer:eer Exists");
        result = true;
      }
      else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT)
      {
        // How did we get so far!!
        USB_SERIAL_PRINTLN("ESPNowManagePeer: ESPNOW Not Init");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_ARG)
      {
        USB_SERIAL_PRINTLN("ESPNowManagePeer: nvalid Argument");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_FULL)
      {
        USB_SERIAL_PRINTLN("ESPNowManagePeer: Peer list full");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_NO_MEM)
      {
        USB_SERIAL_PRINTLN("ESPNowManagePeer: Out of memory");
        result = false;
      }
      else
      {
        USB_SERIAL_PRINTLN("ESPNowManagePeer:Not sure what happened");
        result = false;
      }
    }
  }
  else
  {
    // No peer found to process
    USB_SERIAL_PRINTLN("ESPNowManagePeer:No Peer found to process");

    M5.Lcd.println("ESPNowManagePeer:No Peer found to process");
    result = false;
  }

  if (!result)
    peer.channel = ESPNOW_NO_PEER_CHANNEL_FLAG;
  
  return result;
}

// callback when data is sent from Master to Peer
void OnESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  if (status == ESP_NOW_SEND_SUCCESS)
  {
    ESPNowMessagesDelivered++;
    USB_SERIAL_PRINTF("OnESPNowDataSent: Message Delivered %i\n",ESPNowMessagesDelivered);
  }
  else
  {
    ESPNowMessagesFailedToDeliver++;
    USB_SERIAL_PRINTF("OnESPNowDataSent: Messages Not Delivered %i\n",ESPNowMessagesFailedToDeliver);
  }
}

// callback when data is recv from Master
void OnESPNowDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
  // CRITICAL: Don't process ESP-NOW messages during OTA to prevent queue corruption
  if (haltAllProcessingDuringOTAUpload || otaActive) {
    return; // Silently drop messages during OTA
  }
  
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  USB_SERIAL_PRINTF("OnESPNowDataRecv: Last Packet Recv from: %s\n",macStr);
  USB_SERIAL_PRINTF("OnESPNowDataRecv: Last Packet Recv 1st Byte: '%c'\n",*data);
  USB_SERIAL_PRINTF("OnESPNowDataRecv: Last Packet Recv Length: %d\n",data_len);
  USB_SERIAL_PRINTF("OnESPNowDataRecv: message: %s\n",(char*)data);

  if (espNOW_msgsReceivedQueue && ESPNowActive)
  {
    xQueueSend(espNOW_msgsReceivedQueue, (void*)data, (TickType_t)0);  // don't block on enqueue, just drop if queue is full
  }
}

bool TeardownESPNow()
{
  bool result = false;

  if (enableESPNow && ESPNowActive)
  {
    USB_SERIAL_PRINTLN("TeardownESPNow:: Tearing down ESP-NOW...");
    
    ESPNowDeletePeer(ESPNow_mako_peer);

    esp_now_deinit();
    WiFi.disconnect();
    ESPNowActive = false;
    isPairedWithMako = false;
    result = true;
    
    USB_SERIAL_PRINTLN("TeardownESPNow:: ESP-NOW teardown complete");
  }
  
  return result;
}
        
void ESPNowDeletePeer(esp_now_peer_info_t& peer)
{
  if (peer.channel != ESPNOW_NO_PEER_CHANNEL_FLAG)
  {
    esp_err_t delStatus = esp_now_del_peer(peer.peer_addr);

    USB_SERIAL_PRINT("ESPNowDeletePeer:: Peer Delete Status: ");
    if (delStatus == ESP_OK)
    {
      // Delete success
      USB_SERIAL_PRINTLN("ESPNowDeletePeer::Success");
    }
    else if (delStatus == ESP_ERR_ESPNOW_NOT_INIT)
    {
      // How did we get so far!!
      USB_SERIAL_PRINTLN("ESPNowDeletePeer::ESPNOW Not Init");
    }
    else if (delStatus == ESP_ERR_ESPNOW_ARG)
    {
      USB_SERIAL_PRINTLN("ESPNowDeletePeer::Invalid Argument");
    }
    else if (delStatus == ESP_ERR_ESPNOW_NOT_FOUND)
    {
      USB_SERIAL_PRINTLN("ESPNowDeletePeer::Peer not found.");
    }
    else
    {
      USB_SERIAL_PRINTLN("Not sure what happened");
    }
  }
}

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
  else
    USB_SERIAL_PRINTLN("ESPNOW Unknown Error");
}


#endif