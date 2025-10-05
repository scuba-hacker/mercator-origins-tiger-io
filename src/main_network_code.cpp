#ifdef BUILD_INCLUDE_MAIN_NETWORK_CODE

#include "logs_page.h"

////////////////////////////////////////////////////////////////////////
///////////////////////////////// WiFi/Network/OTA Functions
////////////////////////////////////////////////////////////////////////

// *************************** WiFi Persistence using Preferences ***************************

void saveLastConnectedSSID(const char* ssid) {
  persistedPreferences.putString("lastSSID", String(ssid));
  USB_SERIAL_PRINTF("Saved last SSID: %s\n", ssid);
}

String loadLastConnectedSSID() {
  String ssid = persistedPreferences.getString("lastSSID", "");
  if (ssid.length() > 0) {
    USB_SERIAL_PRINTF("Loaded last SSID: %s\n", ssid.c_str());
  } else {
    USB_SERIAL_PRINTLN("No saved SSID found");
  }
  return ssid;
}

void showOTARecoveryScreen()
{
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(TFT_GREEN);
  M5.Lcd.setCursor(5,5);
  M5.Lcd.setTextColor(TFT_BLACK,TFT_GREEN);
  M5.Lcd.setTextSize(3);

  if (otaActive)
  {
    M5.Lcd.println(" Tiger OTA\n    Ready\n");
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("");
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("%s  Build:\n",WiFi.localIP().toString().c_str());
  }
  else
  {
    M5.Lcd.print("OTA\nOff\nNo WiFi\n\n");
  }

  M5.Lcd.setTextSize(1);
  M5.Lcd.println("");
  M5.Lcd.setTextSize(2);
  M5.Lcd.println(buildTimestamp);
}

void disableAllWatchdogs() {
  // Disable task watchdog for current task
  esp_task_wdt_delete(NULL);

  // Disable system-wide task watchdogs
  esp_task_wdt_deinit();

  // Disable RTC watchdog
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  rtc_wdt_protect_on();
}

void disableFeaturesForOTA(bool screenToRed=true)
{
  haltAllProcessingDuringOTAUpload = true;

  // Keep serial logging active for the /logs page during OTA
  // writeLogToSerial = false;  // Commented out to allow logs page to work

  if (mapScreen.get())
    mapScreen.reset();      // delete mapscreen to save heapspace prior to OTA

  clockSprite->deleteSprite();
  
  // Don't close WebSerial connections - we want the /logs page to work
  // WebSerial.closeAll();   // close all websocket connetions for WebSerial
  
  // Critical: Stop ESP-NOW before OTA to prevent queue corruption
  if (ESPNowActive) {
    USB_SERIAL_PRINTLN("Stopping ESP-NOW for OTA...");
    esp_now_deinit();
    ESPNowActive = false;
    isPairedWithMako = false;
  }
  
  // Flush and safely handle the message queue
  if (espNOW_msgsReceivedQueue) {
    USB_SERIAL_PRINTLN("Flushing ESP-NOW message queue...");
    char tempBuffer[256];
    // Drain any remaining messages
    while (xQueueReceive(espNOW_msgsReceivedQueue, tempBuffer, 0) == pdTRUE) {
      // Just discard the messages
    }
  }
  
  // Small delay to ensure all operations complete
  delay(100);
}

bool systemStartupAndCheckForOTADemand()
{
  delay(750); // avoid all MCU starting simultaneously to avoid power spikes
  
  M5.begin();

  readPreferencesFromEEPROM();

  #ifndef USE_WEBSERIAL
    USB_SERIAL.begin(115200);
  #endif

  initRedLed();

  currentTarget[0]='\0';
  previousTarget[0]='\0';
  
  ssid_connected = ssid_not_connected;

  uint32_t endButtonCheckAt = millis() + 2000;
  while(millis() < endButtonCheckAt)
  {
    // cannot check for BtnA as Tiger is having it's button activated due to
    // it's red case having a BtnA cover pointing up - so it always activates.
    if (isTopReedClosed() || isButtonBPressed())
    {
      enableOTAServerAtStartup = true;
      topReedActiveAtStartup = true;
      haltAllProcessingDuringOTAUpload = true;
      forceLoopInitialOTAEnablement = true;
      disableFeaturesForOTA(); 
      break;
    }
    // Side Reed uses GPIO 0 which is a strapping pin
  }
  
  if (haltAllProcessingDuringOTAUpload)
    USB_SERIAL_PRINTLN("Detected OTA Demanded at startup");
  else
    USB_SERIAL_PRINTLN("Did not detect OTA Demanded at startup");


  return haltAllProcessingDuringOTAUpload;
}

uint32_t OTAUploadFlashLEDTimer = 0;
const uint32_t OTAUploadFlashAwaitLEDPeriodicity = 500;
const uint32_t OTAUploadFlashInProgressLEDPeriodicity = 250;

uint32_t OTAUploadFlashCurrentLEDPeriodicity = OTAUploadFlashAwaitLEDPeriodicity;
uint32_t recoveryScreenStartTime = 0;


bool cutShortLoopOnOTADemand()
{
  ////////////////////////////////////////////////////////////////////////////////////////////////////
  ////////////////// PROTECTED - DO NOT ADD CODE IN THE BELOW PROTECTED AREA - RISK OF OTA FAILURE
  ////////////////////////////////////////////////////////////////////////////////////////////////////  
  if (haltAllProcessingDuringOTAUpload)
  {
    // Handle OTA restart if scheduled
    if (restartForGoodOTAScheduled && millis() >= restartAfterGoodOTAUpdateAt) {
        ESP.restart();
    }

    if (forceLoopInitialOTAEnablement)
    {
      forceLoopInitialOTAEnablement = false;
      M5.Lcd.fillScreen(TFT_BLACK);
      const bool wifiOnly = false;
      const int maxWifiScanAttempts = 3;
      otaActive = connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts, "Enabling\nOTA\n");
    }

    if (!recoveryScreenShown)
    {
      showOTARecoveryScreen();
      recoveryScreenStartTime = millis();
      recoveryScreenShown = true;
    }

    if (millis() > OTAUploadFlashLEDTimer)
    {
      OTAUploadFlashLEDTimer += OTAUploadFlashCurrentLEDPeriodicity;
    }

    // After 5 seconds of recovery screen, allow restart if any reed 
    // is activated or button is pressed. Bypass normal button processingcode.
    if (recoveryScreenShown && (millis() - recoveryScreenStartTime > 5000)) 
    {            
      // check if either button is pressed once 5 seconds has passed since ota screen was shown
      
      if (isTopReedClosed() || isSideReedClosed() || isButtonAPressed() || isButtonBPressed()) {
        M5.Lcd.fillScreen(TFT_GREEN);
        M5.Lcd.setCursor(0,10);
        M5.Lcd.setTextSize(3);
        M5.Lcd.println(" ########### ");
        M5.Lcd.println("#           #");
        M5.Lcd.println("# Rebooting #");
        M5.Lcd.println("#           #");
        M5.Lcd.println(" ########### ");
        delay(1000);
        esp_restart();
      }
    }
  }
  return haltAllProcessingDuringOTAUpload;

  ////////////////////////////////////////////////////////////////////////////////////////////////////
  ////////////////// PROTECTED - DO NOT ADD CODE IN THE ABOVE PROTECTED AREA - RISK OF OTA FAILURE
  ////////////////////////////////////////////////////////////////////////////////////////////////////
}

const char* scanForKnownNetwork() // return first known network found
{
  const char* network = nullptr;

  M5.Lcd.println("Scan WiFi\nSSIDs...");
  int8_t scanResults = WiFi.scanNetworks();

  if (scanResults != 0)
  {
    for (int i = 0; i < scanResults; ++i) 
    {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);

      delay(10);
      
      // Check if the current device starts with the peerSSIDPrefix
      if (strcmp(SSID.c_str(), ssid_1) == 0)
        network=ssid_1;
      else if (strcmp(SSID.c_str(), ssid_2) == 0)
        network=ssid_2;
      else if (strcmp(SSID.c_str(), ssid_3) == 0)
        network=ssid_3;

      if (network)
        break;
    }    
  }

  if (network)
  {
      M5.Lcd.printf("Found:\n%s",network);

    USB_SERIAL_PRINTF("Found:\n%s\n",network);
  }
  else
  {
    M5.Lcd.println("None\nFound");
    USB_SERIAL_PRINTLN("No networks Found\n");
  }

  // clean up ram
  WiFi.scanDelete();

  return network;
}

void webSerialReceiveMessage(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }

  WebSerial.println(d);

  if (d == "ON"){
    setRedLEDOn();
    USB_SERIAL_PRINTLN("LED turned ON via WebSerial command");
  }
  else if (d=="OFF"){
    setRedLEDOff();
    USB_SERIAL_PRINTLN("LED turned OFF via WebSerial command");
  }
  else if (d=="serial-off")
  {
    writeLogToSerial = false;
    WebSerial.closeAll();
    USB_SERIAL_PRINTLN("Serial logging disabled via WebSerial command");
  }
  else if (d=="Cycle" || d=="cycle")
  {
    USB_SERIAL_PRINTLN("Display cycle requested via WebSerial command");
    cycleDisplays();
  }
  else if (d=="ZoomMap" || d=="zoommap")
  {
    USB_SERIAL_PRINTLN("Zoom map requested via WebSerial command");
    cycleDisplays(true, DISPLAY_MAP); // set to map screen

    if (mapScreen.get()) {
      mapScreen->cycleZoom();
      mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);
      USB_SERIAL_PRINTLN("Map zoom cycled");
    } else {
      USB_SERIAL_PRINTLN("Map screen not available");
    }
  }
  else if (d=="restart" || d=="reboot")
  {
    USB_SERIAL_PRINTLN("Reboot requested via WebSerial command");
    esp_restart();
  }
  else if (d=="ota-off")
  {
    USB_SERIAL_PRINTLN("OTA off requested via WebSerial command - will execute in main loop");
    // Set flag to execute OTA shutdown in main loop, not in callback
    otaShutdownRequested = true;
    otaShutdownStartTime = millis();
  }
  else if (d=="force-reeds-primary")
  {
    USB_SERIAL_PRINTLN("Force reeds to be primary control requested via WebSerial command - will execute in main loop");
    forceReedSwitchesPrimaryControl();
  }
  else if (d=="ota-only-mode")
  {
    USB_SERIAL_PRINTLN("Halt all processing and display green OTA screen");
    haltAllProcessingDuringOTAUpload = true;
  }  
  else
  {
    USB_SERIAL_PRINTF("Unknown WebSerial command: %s\n", d.c_str());
  }
}

void handleOTAShutdown() {
  static int shutdownStep = 0;
  uint32_t elapsed = millis() - otaShutdownStartTime;
  
  switch (shutdownStep) {
    case 0: // Initial acknowledgment
      WebSerial.println("OTA off requested via WebSerial command");
      Serial.println("OTA off requested via WebSerial command");
      shutdownStep++;
      otaShutdownStartTime = millis(); // Reset timer for countdown
      break;
      
    case 1: // 3 seconds
      if (elapsed >= 0) {
        WebSerial.println("OTA mode will be disabled in 3 seconds...");
        Serial.println("OTA mode will be disabled in 3 seconds...");
        shutdownStep++;
      }
      break;
      
    case 2: // 2 seconds  
      if (elapsed >= 1000) {
        WebSerial.println("OTA mode will be disabled in 2 seconds...");
        Serial.println("OTA mode will be disabled in 2 seconds...");
        shutdownStep++;
      }
      break;
      
    case 3: // 1 second
      if (elapsed >= 2000) {
        WebSerial.println("OTA mode will be disabled in 1 second...");
        Serial.println("OTA mode will be disabled in 1 second...");
        shutdownStep++;
      }
      break;
      
    case 4: // Final message and shutdown
      if (elapsed >= 3000) {
        WebSerial.println("OTA mode disabled - WebSerial connection will close");
        Serial.println("OTA mode disabled - WebSerial connection will close");
        delay(500); // Give time for final message to send
        
        // Perform the actual shutdown
        otaActive = false;
        haltAllProcessingDuringOTAUpload = false;
        ESPNowActive = false;
        isPairedWithMako = false;
        
        WebSerial.closeAll();
        Serial.println("WebSerial connections closed");
        
        asyncWebServer.end();
        Serial.println("Web server stopped");
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("WiFi disconnected and turned off");
        
        cycleDisplays(true, DISPLAY_CLOCK); // Go to clock display
        Serial.println("Display reset to clock mode");
        
        Serial.println("OTA mode disabled - normal operation resumed");
        
        // Reset shutdown state
        otaShutdownRequested = false;
        shutdownStep = 0;
      }
      break;
  }
}


void uploadOTABeginCallback(AsyncElegantOtaClass* originator)
{
  USB_SERIAL_PRINTLN("OTA Upload starting - closing WebSerial connections");
  
  // Now that actual upload is starting, close WebSerial for safety
  WebSerial.closeAll();
  writeLogToSerial = false;
  
  disableFeaturesForOTA(false);   // prevent LCD call due to separate thread calling this
}

void uploadOTAProgressCallback(AsyncElegantOtaClass* originator, size_t progress, size_t total) 
{
  static uint32_t otaProgressScreenUpdateAt = 0;
  const uint32_t otaProgressUpdateDisplayDutyCycle = 200;

  // Skip if no total size available
  if (total == 0) {
      USB_SERIAL_PRINTF("OTA Progress: skipping, total=0\n");
      return;
  }
  
  if (millis() > otaProgressScreenUpdateAt) 
  {
    OTAUploadFlashCurrentLEDPeriodicity = OTAUploadFlashInProgressLEDPeriodicity;
    otaProgressScreenUpdateAt = millis() + otaProgressUpdateDisplayDutyCycle;
    M5.Lcd.setCursor(3, 60);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("%7lu / %-7lu B", progress, total);
  }  
}

void uploadOTASucceededCallback(AsyncElegantOtaClass* originator)
{
    restartAfterGoodOTAUpdateAt = millis() + 3000;
    restartForGoodOTAScheduled = true;
}

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED)
    return true;

  bool forcedCancellation = false;

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);

  bool connected = false;

  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  if (reedSwitchesPrimaryControl)
    WiFi.setHostname("tiger");
  else
    WiFi.setHostname("tiger-test");
  
  WiFi.begin(_ssid, _password);

  // Wait for connection for max of timeout milliseconds
  const int cycleTime = 500;
  int count = timeout / cycleTime;

  if (wifiOnly)
    M5.Lcd.println("Connect\nWiFi\n");
  else
    M5.Lcd.println("Connect\nWiFi (OTA)\n");

  while (WiFi.status() != WL_CONNECTED && --count > 0)
  {
    M5.Lcd.print(".");
    delay(cycleTime);
  }

  if (WiFi.status() == WL_CONNECTED )
  {
    saveLastConnectedSSID(_ssid);
    if (wifiOnly == false && !otaActive)
    {
      asyncWebServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request)
      {
        request->send(200, "text/plain", "To upload firmware use /update. For buffer log /buffer-log or /reset-buffer-log. For USB logs /logs");
      });

      auto bufferResetHandler = [](AsyncWebServerRequest * request)
      {
        BUFFER_LOG_RESET();
        request->send(200, "text/plain", "Buffer Log Reset");
      };

      asyncWebServer.on("/reset-buffer-log", HTTP_GET, bufferResetHandler);
      asyncWebServer.on("/buffer-log-reset", HTTP_GET, bufferResetHandler);
      asyncWebServer.on("/buffer-reset", HTTP_GET, bufferResetHandler);

      asyncWebServer.on("/logs", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send_P(200, "text/html", LOGS_PAGE_HTML);
      });
        
      asyncWebServer.on("/buffer-log", HTTP_GET, [](AsyncWebServerRequest * request)
      {
        AsyncWebServerResponse *response = request->beginChunkedResponse("text/txt", [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t 
        {
          static uint16_t currentByteForCompleteResponse = 0;

          // Reset on first chunk
          if (index == 0)
            currentByteForCompleteResponse = 0;

          size_t written = 0;

          int max_buffer_log_size = BUFFER_LOG_SIZE;
          int log_length = BUFFER_LOG_GET_LENGTH();
          const char* buffer_log = BUFFER_LOG_GET_BUFFER();

          if (currentByteForCompleteResponse >= log_length)
            return 0;

          int bytes_to_copy = (currentByteForCompleteResponse + maxLen > log_length ? log_length - currentByteForCompleteResponse : maxLen);

          memcpy(buffer, buffer_log+currentByteForCompleteResponse, bytes_to_copy);
          written += bytes_to_copy;
          currentByteForCompleteResponse += written;

          return written;
        });

        request->send(response);
      });
      
      AsyncElegantOTA.setID(MERCATOR_OTA_DEVICE_LABEL);
      AsyncElegantOTA.setUploadBeginCallback(uploadOTABeginCallback);
      AsyncElegantOTA.setUploadProgressCallback(uploadOTAProgressCallback);
      AsyncElegantOTA.setUploadSucceededCallback(uploadOTASucceededCallback);
      AsyncElegantOTA.begin(&asyncWebServer);

      static bool webSerialInitialised = false;

      if (!webSerialInitialised)
      {
        WebSerial.begin(&asyncWebServer);
        WebSerial.msgCallback(webSerialReceiveMessage);
        webSerialInitialised = true;
      }

      asyncWebServer.begin();

      M5.Lcd.setRotation(0);

      otaActive = true;
      connected = true;
    }
  }
  else
  {
    M5.Lcd.printf("No Conn %s",_ssid);
  }

  return connected;
}


bool connectToLastConnectedWifiNetwork(const bool wifiOnly)
{
  const uint32_t lastKnownNetworkTimeout = 10000;

  String lastSSID = loadLastConnectedSSID();
  M5.Lcd.printf("LastConn:%s",lastSSID.c_str());
  // allow twice timeout delay as connecting to last connected SSID.
  if (lastSSID.length() > 0)
  {
    if (lastSSID.equals(ssid_1))
    {
      if (setupOTAWebServer(ssid_1, password_1, label_1, timeout_1 * 2, wifiOnly))
      {
        M5.Lcd.printf("Conn SSID:%s\n",ssid_1);
        saveLastConnectedSSID(ssid_1);
      }
    }
    else if (lastSSID.equals(ssid_2))
    {
      if (setupOTAWebServer(ssid_2, password_2, label_2, timeout_2 * 2, wifiOnly))
      {
        M5.Lcd.printf("Conn SSID:%s\n",ssid_2);
        saveLastConnectedSSID(ssid_2);
      }
    }
    else if (lastSSID.equals(ssid_3))
    {
      if (setupOTAWebServer(ssid_3, password_3, label_3, timeout_3 * 2, wifiOnly))
      {
        M5.Lcd.printf("Conn SSID:%s\n",ssid_3);
        saveLastConnectedSSID(ssid_3);
      }
    }
    else
    {
      M5.Lcd.println("No conn SSIDs");
    }
  }

  return WiFi.status() == WL_CONNECTED;
}

bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts, const char* message)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED ||
      otaActive)
    return true;

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);

  M5.Lcd.println(message);

  if (connectToLastConnectedWifiNetwork(wifiOnly) == false)
  {
    // Fallback: Normal scan and connect process
    while (repeatScanAttempts-- &&
          (WiFi.status() != WL_CONNECTED ||
            WiFi.status() == WL_CONNECTED && wifiOnly == false && otaActive == false ) )
    {
      const char* network = scanForKnownNetwork();
    
      if (!network)
      {
        delay(500);
        continue;
      }

      int connectToFoundNetworkAttempts = 5;
      const int repeatDelay = 1000;
    
      if (strcmp(network,ssid_1) == 0)
      {
        while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(ssid_1, password_1, label_1, timeout_1, wifiOnly))
          delay(repeatDelay);
      }
      else if (strcmp(network,ssid_2) == 0)
      {
        while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(ssid_2, password_2, label_2, timeout_2, wifiOnly))
          delay(repeatDelay);
      }
      else if (strcmp(network,ssid_3) == 0)
      {
        while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(ssid_3, password_3, label_3, timeout_3, wifiOnly))
          delay(repeatDelay);
      }
      
      delay(repeatDelay);
    }
  }

  bool connected = WiFi.status() == WL_CONNECTED;
  
  if (connected)
  {
    ssid_connected = WiFi.SSID();
    saveLastConnectedSSID(ssid_connected.c_str());
  }
  else
  {
    ssid_connected = ssid_not_connected;
    saveLastConnectedSSID("-");
  }
  
  return connected;
}

#endif