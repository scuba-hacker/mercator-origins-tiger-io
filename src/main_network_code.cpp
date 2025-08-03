
#ifdef BUILD_INCLUDE_MAIN_NETWORK_CODE

#include "logs_page.h"

////////////////////////////////////////////////////////////////////////
///////////////////////////////// WiFi/Network/OTA Functions
////////////////////////////////////////////////////////////////////////



void showOTARecoveryScreen()
{
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(TFT_GREEN);
  M5.Lcd.setCursor(5,5);
  M5.Lcd.setTextColor(TFT_BLACK,TFT_GREEN);
  M5.Lcd.setTextSize(3);

  if (otaActive)
  {
    M5.Lcd.println(" Lemon OTA\n    Ready\n");
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

  writeLogToSerial = false;

  if (mapScreen.get())
    mapScreen.reset();      // delete mapscreen to save heapspace prior to OTA

  WebSerial.closeAll();   // close all websocket connetions for WebSerial
  
  // Critical: Stop ESP-NOW before OTA to prevent queue corruption
  if (ESPNowActive) {
    USB_SERIAL_PRINTLN("Stopping ESP-NOW for OTA...");
    esp_now_deinit();
    ESPNowActive = false;
    isPairedWithMako = false;
  }
  
  // Flush and safely handle the message queue
  if (msgsReceivedQueue) {
    USB_SERIAL_PRINTLN("Flushing ESP-NOW message queue...");
    char tempBuffer[256];
    // Drain any remaining messages
    while (xQueueReceive(msgsReceivedQueue, tempBuffer, 0) == pdTRUE) {
      // Just discard the messages
    }
  }
  
  // Small delay to ensure all operations complete
  delay(100);
}

bool systemStartupAndCheckForOTADemand()
{
  M5.begin();

  USB_SERIAL.begin(115200);

  initRedLed();

  currentTarget[0]='\0';
  previousTarget[0]='\0';
  
  ssid_connected = ssid_not_connected;

  uint32_t endButtonCheckAt = millis() + 2000;
  while(millis() < endButtonCheckAt)
  {
    if (isTopReedClosed() || isButtonAPressed())
    {
      enableOTAServerAtStartup = true;
      topReedActiveAtStartup = true;
      haltAllProcessingDuringOTAUpload = true;
      forceLoopInitialOTAEnablement = true;
      disableFeaturesForOTA(); 
      break;
    }

    if (isSideReedClosed())
    {
      // Cannot use this as side GPIO = 0 , a strapping pin
      break;
    }
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
      toggleRedLED();
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
  }
  else if (d=="OFF"){
    setRedLEDOff();
  }
  else if (d=="serial-off")
  {
    writeLogToSerial = false;
    WebSerial.closeAll();
  }
}


void uploadOTABeginCallback(AsyncElegantOtaClass* originator)
{
  disableFeaturesForOTA(false);   // prevent LCD call due to separate thread calling this
  dumpHeapUsage("uploadOTABeginCallback: ");
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
  {
    USB_SERIAL_PRINTF("setupOTAWebServer: attempt to connect wifiOnly, already connected - otaActive=%i\n",otaActive);

    return true;
  }

  USB_SERIAL_PRINTF("setupOTAWebServer: attempt to connect %s wifiOnly=%i when otaActive=%i\n",_ssid, wifiOnly,otaActive);

  bool forcedCancellation = false;

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  bool connected = false;
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname("tiger");

  WiFi.begin(_ssid, _password);

  // Wait for connection for max of timeout/1000 seconds
  M5.Lcd.printf("%s Wifi", label);
  int count = timeout / 500;
  while (WiFi.status() != WL_CONNECTED && --count > 0)
  {
    M5.Lcd.print(".");
    delay(500);
  }
  M5.Lcd.print("\n\n");

  if (WiFi.status() == WL_CONNECTED )
  {
    if (wifiOnly == false && !otaActive)
    {
      dumpHeapUsage("setupOTAWebServer(): after WiFi connect");

      USB_SERIAL_PRINTLN("setupOTAWebServer: WiFi connected ok, starting up OTA");

      USB_SERIAL_PRINTLN("setupOTAWebServer: calling asyncWebServer.on");

      asyncWebServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(200, "text/plain", "To upload firmware use /update");
      });

      asyncWebServer.on("/logs", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send_P(200, "text/html", LOGS_PAGE_HTML);
      });
        
      USB_SERIAL_PRINTLN("setupOTAWebServer: calling AsyncElegantOTA.begin");

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

      USB_SERIAL_PRINTLN("setupOTAWebServer: calling asyncWebServer.begin");

      asyncWebServer.begin();

      dumpHeapUsage("setupOTAWebServer(): after asyncWebServer.begin");

      USB_SERIAL_PRINTLN("setupOTAWebServer: OTA setup complete");

      M5.Lcd.setRotation(0);
      
      M5.Lcd.fillScreen(TFT_BLACK);
      M5.Lcd.setCursor(0,155);
      M5.Lcd.setTextSize(2);
      M5.Lcd.printf("%s\n\n",WiFi.localIP().toString());
      M5.Lcd.println(WiFi.macAddress());
      connected = true;
      otaActive = true;
  
      M5.Lcd.qrcode("http://"+WiFi.localIP().toString()+"/update",0,0,135);
  
      delay(2000);

      connected = true;
    }
  }
  else
  {
    USB_SERIAL_PRINTF("setupOTAWebServer: WiFi failed to connect %s\n",_ssid);

    M5.Lcd.print("No Connect");
  }

  M5.Lcd.fillScreen(TFT_BLACK);

  dumpHeapUsage("setupOTAWebServer(): end of function");

  return connected;
}

bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts, const char* message)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED)
    return true;

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);

  M5.Lcd.println(message);

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

    int connectToFoundNetworkAttempts = 3;
    const int repeatDelay = 500;
  
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

  bool connected=WiFi.status() == WL_CONNECTED;
  
  if (connected)
  {
    ssid_connected = WiFi.SSID();
  }
  else
  {
    ssid_connected = ssid_not_connected;
  }
  
  return connected;
}

#endif