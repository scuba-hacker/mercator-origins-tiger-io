#ifdef BUILD_INCLUDE_MAIN_ESPNOW_NETWORK_CODE

////////////////////////////////////////////////////////////////////////
///////////////////////////////// ESPNow Network Functions
////////////////////////////////////////////////////////////////////////

uint8_t MAKO_MAC[6] = {0x94, 0xB9, 0x7E, 0xAC, 0xF5, 0x45};       // AP Mac, not STA Mac shown in router
bool pairWithKnownMAC(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const uint8_t* knownMAC);

void configAndStartUpESPNow()
{  
  //Set device in AP mode to begin with
  WiFi.mode(WIFI_AP);
  
  // configure device AP mode
  String Prefix = "Tiger:";
  String Mac = WiFi.macAddress();
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
  USB_SERIAL_PRINT("AP MAC: "); 
  USB_SERIAL_PRINTLN(WiFi.softAPmacAddress());

  if (esp_now_init() == ESP_OK)
  {
    USB_SERIAL_PRINTLN("ESPNow Init Success");
    ESPNowActive = true;
  }
  else
  {
    USB_SERIAL_PRINTLN("ESPNow Init Failed");
    ESPNowActive = false;
  }
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info.
  esp_now_register_send_cb(OnESPNowDataSent);
  esp_now_register_recv_cb(OnESPNowDataRecv);
}

bool pairWithMako()
{
  if (ESPNowActive && !isPairedWithMako)
  {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Lcd.setCursor(0,0);
    const int pairAttempts = 5;
    //isPairedWithMako = pairWithPeer(ESPNow_mako_peer,"Mako",pairAttempts); // 5 connection attempts
    isPairedWithMako = pairWithKnownMAC(ESPNow_mako_peer,"Mako",MAKO_MAC);
    if (isPairedWithMako)
    {
      // send message to tiger to give first target
      publishToMakoTestMessage("Conn Ok");
    }
  }

  return isPairedWithMako;
}

// callback when data is sent from Master to Peer
void OnESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  if (status == ESP_NOW_SEND_SUCCESS)
  {
    ESPNowMessagesDelivered++;
  }
  else
  {
    ESPNowMessagesFailedToDeliver++;
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
  USB_SERIAL_PRINTF("Last Packet Recv from: %s\n",macStr);
  USB_SERIAL_PRINTF("Last Packet Recv 1st Byte: '%c'\n",*data);
  USB_SERIAL_PRINTF("Last Packet Recv Length: %d\n",data_len);
  USB_SERIAL_PRINTLN((char*)data);

  if (msgsReceivedQueue && ESPNowActive)
    xQueueSend(msgsReceivedQueue, (void*)data, (TickType_t)0);  // don't block on enqueue, just drop if queue is full
}

bool TeardownESPNow()
{
  bool result = false;

  if (enableESPNow && ESPNowActive)
  {
    USB_SERIAL_PRINTLN("Tearing down ESP-NOW...");
    
    // Properly deinitialize ESP-NOW
    esp_now_deinit();
    WiFi.disconnect();
    ESPNowActive = false;
    isPairedWithMako = false;
    result = true;
    
    USB_SERIAL_PRINTLN("ESP-NOW teardown complete");
  }
  
  return result;
}

bool pairWithKnownMAC(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const uint8_t* knownMAC)
{
  // Skip scanning - use known MAC directly
  // IMPORTANT: Reset peer structure first (same as ESPNowScanForPeer does)
  memset(&peer, 0, sizeof(peer));
  
  // Setup peer structure with known MAC
  memcpy(peer.peer_addr, knownMAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = 0;
  peer.priv = (void*)peerSSIDPrefix;
  
  // Add some delay to allow ESP-NOW to settle
  delay(100);
  
  bool isPaired = ESPNowManagePeer(peer);
  
  if (!isPaired)
  {
    peer.channel = ESPNOW_NO_PEER_CHANNEL_FLAG;
  }

  return isPaired;
}

// Scan for peers in AP mode
bool ESPNowScanForPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const bool suppressPeerFoundMsg)
{
  bool peerFound = false;
  
  M5.Lcd.printf("Scan For\n%s\n",peerSSIDPrefix);
  int8_t scanResults = WiFi.scanNetworks();
  
  // reset on each scan 
  memset(&peer, 0, sizeof(peer));

  USB_SERIAL_PRINTLN("");

  if (scanResults == 0) 
  {   
    USB_SERIAL_PRINTLN("No WiFi devices in AP Mode found");

    peer.channel = ESPNOW_NO_PEER_CHANNEL_FLAG;
  } 
  else 
  {
    USB_SERIAL_PRINT("Found "); USB_SERIAL_PRINT(scanResults); USB_SERIAL_PRINTLN(" devices ");
    
    for (int i = 0; i < scanResults; ++i) 
    {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);

      if (ESPNOW_PRINTSCANRESULTS) 
      {
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
        USB_SERIAL_PRINTLN("Found a peer.");
        USB_SERIAL_PRINT(i + 1); USB_SERIAL_PRINT(": "); USB_SERIAL_PRINT(SSID); USB_SERIAL_PRINT(" ["); USB_SERIAL_PRINT(BSSIDstr); USB_SERIAL_PRINT("]"); USB_SERIAL_PRINT(" ("); USB_SERIAL_PRINT(RSSI); USB_SERIAL_PRINT(")"); USB_SERIAL_PRINTLN("");
                
        // Get BSSID => Mac Address of the Slave
        int mac[6];
        if ( 6 == sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x",  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5] ) ) 
        {
          for (int ii = 0; ii < 6; ++ii ) 
          {
            peer.peer_addr[ii] = (uint8_t) mac[ii];
          }
        }

        peer.channel = ESPNOW_CHANNEL; // pick a channel
        peer.encrypt = 0; // no encryption

        peer.priv = (void*)peerSSIDPrefix;   // distinguish between different peers

        peerFound = true;
        // we are planning to have only one slave in this example;
        // Hence, break after we find one, to be a bit efficient
        break;
      }
    }
  }

  if (!suppressPeerFoundMsg)
  {
    if (peerFound)
    {
      M5.Lcd.println("Peer Found");
      USB_SERIAL_PRINTLN("Peer Found, processing..");
    } 
    else 
    {
      M5.Lcd.println("Peer Not Found");
      USB_SERIAL_PRINTLN("Peer Not Found, trying again.");
    }
  }
    
  // clean up ram
  WiFi.scanDelete();

  return peerFound;
}

bool pairWithPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, int maxAttempts)
{
  bool isPaired = false;
  while(maxAttempts-- && !isPaired)
  {
    bool result = ESPNowScanForPeer(peer,peerSSIDPrefix);

    // check if peer channel is defined
    if (result && peer.channel == ESPNOW_CHANNEL)
    { 
      isPaired = ESPNowManagePeer(peer);
      M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
      M5.Lcd.printf("%s Pair\nok\n",peerSSIDPrefix);
      M5.Lcd.setTextColor(TFT_WHITE);
    }
    else
    {
      peer.channel = ESPNOW_NO_PEER_CHANNEL_FLAG;
      M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
      M5.Lcd.printf("%s Pair\nfail\n",peerSSIDPrefix);
      M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    }
  }

  delay(1000);
  
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setCursor(0,0);
  
  return isPaired;
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

    USB_SERIAL_PRINT("Peer Status: ");

    // check if the peer exists
    bool exists = esp_now_is_peer_exist(peer.peer_addr);

    if (exists)
    {
      // Peer already paired.
      USB_SERIAL_PRINTLN("Already Paired");

      M5.Lcd.println("Already paired");
      result = true;
    }
    else
    {
      // Peer not paired, attempt pair
      esp_err_t addStatus = esp_now_add_peer(&peer);

      if (addStatus == ESP_OK)
      {
        // Pair success
        USB_SERIAL_PRINTLN("Pair success");
        M5.Lcd.println("Pair success");
        result = true;
      }
      else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT)
      {
        // How did we get so far!!
        USB_SERIAL_PRINTLN("ESPNOW Not Init");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_ARG)
      {
        USB_SERIAL_PRINTLN("Invalid Argument");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_FULL)
      {
        USB_SERIAL_PRINTLN("Peer list full");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_NO_MEM)
      {
        USB_SERIAL_PRINTLN("Out of memory");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_EXIST)
      {
        USB_SERIAL_PRINTLN("Peer Exists");
        result = true;
      }
      else
      {
        USB_SERIAL_PRINTLN("Not sure what happened");
        result = false;
      }
    }
  }
  else
  {
    // No peer found to process
    USB_SERIAL_PRINTLN("No Peer found to process");

    M5.Lcd.println("No Peer found to process");
    result = false;
  }

  return result;
}

void ESPNowDeletePeer(esp_now_peer_info_t& peer)
{
  if (peer.channel != ESPNOW_NO_PEER_CHANNEL_FLAG)
  {
    esp_err_t delStatus = esp_now_del_peer(peer.peer_addr);

    USB_SERIAL_PRINT("Peer Delete Status: ");
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

#endif