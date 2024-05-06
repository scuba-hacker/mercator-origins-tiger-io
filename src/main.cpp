#include <Arduino.h>

#include <M5StickCPlus.h>
#include <MapScreen_M5.h>

#include <WebSerial.h>

//#define USE_WEBSERIAL

#ifdef USE_WEBSERIAL
  #define USB_SERIAL WebSerial
#else
  #define USB_SERIAL Serial
#endif

#include <esp_now.h>
#include <WiFi.h>
#include <freertos/queue.h>

#define MERCATOR_ELEGANTOTA_TIGER_BANNER
#define MERCATOR_OTA_DEVICE_LABEL "TIGER-IO"
#define RED_LED_GPIO 10

#include <Update.h>             // OTA updates
#include <AsyncTCP.h>           // OTA updates
#include <ESPAsyncWebServer.h>  // OTA updates
#include <AsyncElegantOTA.h>    // OTA updates
AsyncElegantOtaClass AsyncElegantOTA;

#include <memory.h>
#include <time.h>

// rename the git file "mercator_secrets_template.c" to the filename below, filling in your wifi credentials etc.
#include "mercator_secrets.c"

bool writeLogToSerial=false;
bool testPreCannedLatLong=false;       // test that animates the diver sprite through slow movements across the lake.
bool goProButtonsPrimaryControl = true;

bool enableOTAServerAtStartup=false; // OTA updates - don't set true without disabling mapscreen, insufficient heap

const bool enableESPNow = !enableOTAServerAtStartup; // cannot have OTA server on regular wifi and espnow concurrently running

const String ssid_not_connected = "-";
String ssid_connected;

// ************** ESPNow variables **************

uint16_t ESPNowMessagesDelivered = 0;
uint16_t ESPNowMessagesFailedToDeliver = 0;

const uint8_t ESPNOW_CHANNEL=1;
const uint8_t ESPNOW_NO_PEER_CHANNEL_FLAG = 0xFF;
const uint8_t ESPNOW_PRINTSCANRESULTS = 0;
const uint8_t ESPNOW_DELETEBEFOREPAIR = 0;

esp_now_peer_info_t ESPNow_mako_peer;
bool isPairedWithMako = false;

const int RESET_ESPNOW_SEND_RESULT = 0xFF;
esp_err_t ESPNowSendResult=(esp_err_t)RESET_ESPNOW_SEND_RESULT;

char mako_espnow_buffer[256];
char currentTime[9];

QueueHandle_t msgsReceivedQueue;

bool ESPNowActive = false;

const int SCREEN_LENGTH = 240;
const int SCREEN_WIDTH = 135;

const uint8_t BUTTON_REED_TOP_PIN=25;
const uint8_t BUTTON_REED_SIDE_PIN=0;
const uint8_t UNUSED_GPIO_36_PIN=36;
const uint8_t M5_POWER_SWITCH_PIN=255;
const uint32_t MERCATOR_DEBOUNCE_MS=100;
const uint8_t PENETRATOR_LEAK_DETECTOR_PIN=26;

Button ReedSwitchGoProTop = Button(BUTTON_REED_TOP_PIN, true, MERCATOR_DEBOUNCE_MS);    // from utility/Button.h for M5 Stick C Plus
Button ReedSwitchGoProSide = Button(BUTTON_REED_SIDE_PIN, true, MERCATOR_DEBOUNCE_MS); // from utility/Button.h for M5 Stick C Plus
Button LeakDetectorSwitch = Button(PENETRATOR_LEAK_DETECTOR_PIN, true, MERCATOR_DEBOUNCE_MS); // from utility/Button.h for M5 Stick C Plus
uint16_t sideCount = 0, topCount = 0;

bool topReedActiveAtStartup = false;
bool sideReedActiveAtStartup = false;


const uint16_t mode_label_y_offset = 170;

AsyncWebServer asyncWebServer(80);      // OTA updates
bool otaActive=false;   // OTA updates toggle

Button* p_primaryButton = NULL;
Button* p_secondButton = NULL;

bool primaryButtonIsPressed = false;
uint32_t primaryButtonPressedTime = 0;
uint32_t lastPrimaryButtonPressLasted = 0;

bool secondButtonIsPressed = false;
uint32_t secondButtonPressedTime = 0;
uint32_t lastSecondButtonPressLasted = 0;

bool primaryButtonIndicatorNeedsClearing = false;
bool secondButtonIndicatorNeedsClearing = false;

std::unique_ptr<MapScreen_M5> mapScreen;
double latitude=51.460015;
double longitude=-0.548316;
double heading=0.0;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;        // timezone offset
int   daylightOffset_sec = 3600;   // DST offset

RTC_TimeTypeDef RTC_TimeStruct;
RTC_DateTypeDef RTC_DateStruct;

const char* leakAlarmMsg = "\nWATER\n\nLEAK\n\nALARM";

int mode_ = 3; // clock

const int defaultBrightness = 100;

bool showPowerStats=false;

char rxQueueItemBuffer[256];
const uint8_t queueLength=4;

char currentTarget[128];
char previousTarget[128];
bool refreshTargetShown = false;

void initialiseRTCfromNTP();
bool cycleDisplays(const bool refreshCurrentDisplay = false);
bool checkReedSwitches();
void publishToMakoTestMessage(const char* testMessage);
void publishToMakoReedActivation(const bool topReed, const uint32_t ms);
void publishToMakoLeakDetected();
void vfd_3_line_clock();
void vfd_5_current_target();
void vfd_6_map();
void getTime(char* time);
void draw_digits(int h1, int h2, int i1, int i2, int s1, int s2);
void draw_digit_text(int h1, int h2, int i1, int i2, int s1, int s2);
void resetCurrentTarget();
void resetMap();
void resetClock();
void fadeToBlackAndShutdown();
const char* scanForKnownNetwork();
bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly = false);
void toggleOTAActiveAndWifiIfUSBPowerOff();
void updateButtonsAndBuzzer();
void readAndTestGoProReedSwitches();
bool leakAlarmActive = false;
void checkForLeak(const char* msg, const uint8_t pin);
void InitESPNow();
void configAndStartUpESPNow();
void configESPNowDeviceAP();
void OnESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnESPNowDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len);
bool ESPNowScanForPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const bool suppressPeerFoundMsg = true);
bool pairWithMako();
bool pairWithPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, int maxAttempts);
bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts);
bool ESPNowManagePeer(esp_now_peer_info_t& peer);
void ESPNowDeletePeer(esp_now_peer_info_t& peer);
bool TeardownESPNow();

uint8_t redLEDStatus = HIGH;

void toggleRedLED()
{
  redLEDStatus = (redLEDStatus == HIGH ? LOW : HIGH );
  digitalWrite(RED_LED_GPIO, redLEDStatus);
}

void  initialiseRTCfromNTP()
{
  const uint8_t max_NTP_connect_attempts=10;

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0,0);

  const bool wifiOnly = true;

  M5.Lcd.println("Get Time...\n\n");
  delay(1000);

  const int maxWifiScanAttempts = 2;  
  if (WiFi.status() == WL_CONNECTED || connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts))
  {
    M5.Lcd.println("Wifi OK");
  
    //init and get the time
    _initialiseTimeFromNTP:
    
    struct tm timeinfo;
    for (uint8_t i=0; i<max_NTP_connect_attempts; i++)
    {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      if(!getLocalTime(&timeinfo))
      {
        if (writeLogToSerial)
          USB_SERIAL.println("No time available (yet)");
        // Let RTC continue with existing settings
        M5.Lcd.println("Wait for NTP Time\n");
        delay(500);
      }
      else
      {
        break;        
      }
    }

    if(!getLocalTime(&timeinfo))
    {      
      // Let RTC continue with existing settings
      M5.Lcd.println("No NTP Server\n");
    }
    else
    {
      if (writeLogToSerial)
        USB_SERIAL.println("NTP time received");
      // Use NTP to update RTC
    
      RTC_TimeTypeDef TimeStruct;
      TimeStruct.Hours   = timeinfo.tm_hour;
      TimeStruct.Minutes = timeinfo.tm_min;
      TimeStruct.Seconds = timeinfo.tm_sec;
      M5.Rtc.SetTime(&TimeStruct);

      RTC_DateTypeDef DateStruct;
      DateStruct.Month = timeinfo.tm_mon+1;
      DateStruct.Date = timeinfo.tm_mday;
      DateStruct.Year = timeinfo.tm_year+1900;
      DateStruct.WeekDay = timeinfo.tm_wday;
      M5.Rtc.SetDate(&DateStruct);    
      if (daylightOffset_sec == 0)
        M5.Lcd.println("RTC to GMT");
      else
        M5.Lcd.println("RTC set to BST");
  
      delay(300);
    }

    if (daylightOffset_sec == 0)
    {
      // check if British Summer Time

      int day = timeinfo.tm_wday;
      int date = timeinfo.tm_mday;
      int month = timeinfo.tm_mon;

      if (month == 2 && date > 24)    // is date after or equal to last Sunday in March?
      {
        if (date - day >= 25)
        {
          daylightOffset_sec=3600;
          // reinitialise time from NTP with correct offset.
          // this doesn't deal with the exact changeover time for BST, but doesn't matter
          goto _initialiseTimeFromNTP;
        }
      }
    }

    if (!enableOTAServerAtStartup)
    {
        //disconnect WiFi as it's no longer needed
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        M5.Lcd.println("WiFi Off");
    }
  }
  else
  {
    M5.Lcd.println(" FAILED");
    delay(5000);
  }

  M5.Lcd.fillScreen(BLACK);
  
  M5.Lcd.setRotation(0);

  resetClock();
}

void dumpHeapUsage(const char* msg)
{  
  if (writeLogToSerial)
  {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
    USB_SERIAL.printf("\n%s : free heap bytes: %i  largest free heap block: %i min free ever: %i\n",  msg, info.total_free_bytes, info.largest_free_block, info.minimum_free_bytes);
  }
}
void showOTARecoveryScreen()
{
  M5.Lcd.fillScreen(TFT_GREEN);
  M5.Lcd.setCursor(5,5);
  M5.Lcd.setTextColor(TFT_WHITE,TFT_GREEN);
  M5.Lcd.setTextSize(3);

  if (otaActive)
  {
    M5.Lcd.printf("OTA\nRecover\nActive\n\n%s",WiFi.localIP().toString());
  }
  else
  {
    M5.Lcd.print("OTA\nRecover\nNo WiFi\n\n");
  }

  while (true)
    delay(1000);
}


bool haltAllProcessingDuringOTAUpload = false;

void disableFeaturesForOTA(bool screenToRed=true)
{
  haltAllProcessingDuringOTAUpload = true;

  mapScreen.reset();      // delete mapscreen to save heapspace prior to OTA

  WebSerial.closeAll();   // close all websocket connetions for WebSerial
}

void uploadOTABeginCallback(AsyncElegantOtaClass* originator)
{
  disableFeaturesForOTA(false);   // prevent LCD call due to separate thread calling this
}

void setup()
{
  M5.begin();
  
  dumpHeapUsage("Setup(): start");
  currentTarget[0]='\0';
  previousTarget[0]='\0';

  pinMode(RED_LED_GPIO, OUTPUT); // Red LED - the interior LED to M5 Stick
  digitalWrite(RED_LED_GPIO, redLEDStatus);

#ifndef USE_WEBSERIAL
  if (writeLogToSerial)
    USB_SERIAL.begin(115200);
#endif

  ssid_connected = ssid_not_connected;

  uint32_t start = millis();
  while(millis() < start + 2000)
  {
    if (digitalRead(BUTTON_REED_TOP_PIN) == false)
    {
      enableOTAServerAtStartup = true;
      topReedActiveAtStartup = true;
      break;
    }

    if (digitalRead(BUTTON_REED_SIDE_PIN) == false)
    {
      sideReedActiveAtStartup = true;
      // no function currently - could be used for show test track
      break;
    }
  }

  if (enableOTAServerAtStartup)
  {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(5,5);
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("Start\nOTA\n\n");
    delay(1000);
    const bool wifiOnly = false;
    const int maxWifiScanAttempts = 3;
    connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts);
  }

  if (topReedActiveAtStartup)
    showOTARecoveryScreen();
  
  strncpy(previousTarget,"None",sizeof(previousTarget));
  strncpy(currentTarget,"  No\nTarget\n  Set\n From\n Mako",sizeof(currentTarget));

  pinMode(UNUSED_GPIO_36_PIN,INPUT);

  mapScreen = std::make_unique<MapScreen_M5>(M5.Lcd);
  mapScreen->provideLoggingHook(USB_SERIAL);

  mapScreen->setDrawAllFeatures(true);
  mapScreen->setUseDiverHeading(true);

  msgsReceivedQueue = xQueueCreate(queueLength,sizeof(rxQueueItemBuffer));

  if (writeLogToSerial)
  {
    if (msgsReceivedQueue == nullptr)
      USB_SERIAL.println("Failed to create queue");
    else
      USB_SERIAL.println("Created msg queue");
  }

  if (msgsReceivedQueue)
  {
    M5.Lcd.println("Created msg queue");
  }

  if (goProButtonsPrimaryControl)
  {
    p_primaryButton = &ReedSwitchGoProTop;
    p_secondButton = &ReedSwitchGoProSide;
  }
  else
  {
    p_primaryButton = &M5.BtnA;
    p_secondButton = &M5.BtnB;
  }

  M5.Beep.setBeep(1200, 100);

  M5.Lcd.setTextSize(2);
  M5.Axp.ScreenBreath(defaultBrightness);

  initialiseRTCfromNTP();

  // override clock screen to be test for target received from espnow
  mode_ = 3;

  if (enableESPNow && msgsReceivedQueue)
  {
    configAndStartUpESPNow();
    // defer pairing with mako for sending messages to mako until first message received from mako.
  }

  dumpHeapUsage("Setup(): end ");
}

bool pairWithMako()
{
  if (ESPNowActive && !isPairedWithMako)
  {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Lcd.setCursor(0,0);
    const int pairAttempts = 5;
    isPairedWithMako = pairWithPeer(ESPNow_mako_peer,"Mako",pairAttempts); // 5 connection attempts

    if (isPairedWithMako)
    {
      // send message to tiger to give first target
      publishToMakoTestMessage("Conn Ok");
    }
  }

  return isPairedWithMako;
}

void checkForLeak(const char* msg, const uint8_t pin)
{
  bool leakStatus = false;

  if (pin == M5_POWER_SWITCH_PIN && M5.Axp.GetBtnPress())
  {
    leakStatus = true;
  }
  else
  {
    leakStatus = !(digitalRead(pin));
  }

  if (leakStatus)
  {
    leakAlarmActive = true;

    M5.Lcd.fillScreen(TFT_RED);
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(5, 10);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
    M5.Lcd.print(msg);
    M5.Beep.setBeep(1200, 100);
    M5.Beep.beep();
    delay(100);
    updateButtonsAndBuzzer();

    M5.Lcd.fillScreen(TFT_ORANGE);
    M5.Lcd.setCursor(5, 10);
    M5.Lcd.setTextColor(TFT_YELLOW, TFT_ORANGE);
    M5.Lcd.print(msg);
    M5.Beep.setBeep(1500, 100);
    M5.Beep.beep();
    delay(100);

    updateButtonsAndBuzzer();
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Beep.mute();

    publishToMakoLeakDetected();
  }
}

void readAndTestGoProReedSwitches()
{
  updateButtonsAndBuzzer();

  bool btnTopPressed = p_primaryButton->pressedFor(15);
  bool btnSidePressed = p_secondButton->pressedFor(15);

  if (btnTopPressed && btnSidePressed)
  {
    sideCount++;
    topCount++;
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.printf("TOP+SIDE %d %d", topCount, sideCount);
  }
  else if (btnTopPressed)
  {
    topCount++;
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.printf("TOP %d", topCount);
  }
  else if (btnSidePressed)
  {
    sideCount++;
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.printf("SIDE %d", sideCount);
  }
}

void resetCurrentTarget()
{
  refreshTargetShown = true;
  M5.Lcd.fillScreen(BLACK);
  mode_ = 5;
}

void resetMap()
{
  mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);
  mode_ = 6;
}

void resetClock()
{
  M5.Lcd.fillScreen(BLACK);
  mode_ = 3; // change back to 3
}

bool cycleDisplays(const bool refreshCurrentDisplay)
{
    bool changeMade = true;

    if (refreshCurrentDisplay)
    {
      if (mode_ == 3)
        resetClock();
      else if (mode_ == 5)
        resetCurrentTarget();
      else if (mode_ == 6)
        if (mapScreen.get())    // OTA enabling has to delete the map screen
          resetMap();
        else
          resetClock();   // OTA enabled, go back to clock
      else
      {
        if (writeLogToSerial)
          USB_SERIAL.println("cycleDisplays Error: invalid mode_");
        changeMade = false;
      }
    }
    else
    {
      if (mode_ == 3) // clock mode, next is show current target
        resetCurrentTarget();
      else if (mode_ == 5)     // show current target, next is map
      {
        if (mapScreen.get())    // OTA enabling has to delete the map screen
          resetMap();
        else
          resetClock();   // OTA enabled, go back to clock
      }
      else if (mode_ == 6)     // show map, next is clock
        resetClock();
      else
      {
        if (writeLogToSerial)
          USB_SERIAL.println("cycleDisplays Error: invalid mode_");
        changeMade = false;
      }
    }

    return changeMade;
}

bool checkReedSwitches()
{
  bool changeMade = false;

  bool reedSwitchTop;
  uint32_t activationTime=0;
    
  updateButtonsAndBuzzer();

  int pressedPrimaryButtonX, pressedPrimaryButtonY, pressedSecondButtonX, pressedSecondButtonY;

  pressedPrimaryButtonX = 110;
  pressedPrimaryButtonY = 5; 

  pressedSecondButtonX = 5;
  pressedSecondButtonY = 210;
    
  if (primaryButtonIsPressed && millis()-primaryButtonPressedTime > 250)
  {
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
    M5.Lcd.setCursor(pressedPrimaryButtonX,pressedPrimaryButtonY);
    M5.Lcd.printf("%i",(millis()-primaryButtonPressedTime)/1000);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    primaryButtonIndicatorNeedsClearing=true;
  }
  else
  {
    if (primaryButtonIndicatorNeedsClearing)
    {
      primaryButtonIndicatorNeedsClearing = false;
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.setCursor(pressedPrimaryButtonX,pressedPrimaryButtonY);
      M5.Lcd.print(" ");
    }
  }

  if (secondButtonIsPressed && millis()-secondButtonPressedTime > 250)
  {
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
    M5.Lcd.setCursor(pressedSecondButtonX,pressedSecondButtonY);
    M5.Lcd.printf("%i",(millis()-secondButtonPressedTime)/1000);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    secondButtonIndicatorNeedsClearing=true;
  }
  else
  {
    if (secondButtonIndicatorNeedsClearing)
    {
      secondButtonIndicatorNeedsClearing = false;
      
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.setCursor(pressedSecondButtonX,pressedSecondButtonY);
      M5.Lcd.print(" ");
    }
  }

  if (p_primaryButton->wasReleasefor(100)) // show next display
  {
    activationTime = lastPrimaryButtonPressLasted;
    reedSwitchTop = true;
    changeMade = true;

    if (writeLogToSerial)
      USB_SERIAL.println("Cycle To Next Display");

    cycleDisplays();
  }

  // press second button for 5 seconds to attempt WiFi connect and enable OTA
  if (p_secondButton->wasReleasefor(10000))
  { 
    if (writeLogToSerial)
      USB_SERIAL.println("Reboot");

     esp_restart();
  }
  else if (p_secondButton->wasReleasefor(5000))
  { 
    if (mode_ != 6)   // map screen has to be deleted for OTA to be enabled, cannot do this if already in map mode
    {
      activationTime = lastSecondButtonPressLasted;
      reedSwitchTop = false;

      TeardownESPNow();
      isPairedWithMako = false;

      dumpHeapUsage("checkReedSwitches(): begin switch to OTA");

      // enable OTA
      const bool wifiOnly = false;
      M5.Lcd.fillScreen(TFT_BLACK);
      M5.Lcd.setCursor(0,0);
      M5.Lcd.println("Start\n  OTA\n\n");
      delay(1000);
      const int maxWifiScanAttempts = 3;
      connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts);

      changeMade = true;
      const bool refreshCurrentScreen=true;
      cycleDisplays(refreshCurrentScreen);
    }
    if (writeLogToSerial)
      USB_SERIAL.println("Enable OTA Mode");
  }
  // press second button for 1 second...
  else if (p_secondButton->wasReleasefor(1000))
  {
    activationTime = lastSecondButtonPressLasted;
    reedSwitchTop = false;

    if (mode_ == 6)    // toggle showing all features on the map
    {
      mapScreen->toggleDrawAllFeatures();
      mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);
      changeMade = true;
    }
    if (writeLogToSerial)
      USB_SERIAL.println("Toggle show all map features");
  }
  // press second button for 0.1 second...
  else if (p_secondButton->wasReleasefor(100))
  {
    activationTime = lastSecondButtonPressLasted;
    reedSwitchTop = false;

    if (mode_ == 6) // map mode - cycle zoom
    {
      mapScreen->cycleZoom(); changeMade = true;
      mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);

      if (writeLogToSerial)
        USB_SERIAL.println("Cycle zoom level on map");
    }
  }
  
  if (activationTime > 0)
  {
    if (writeLogToSerial)
      USB_SERIAL.println("Reed Activated...");

    publishToMakoReedActivation(reedSwitchTop, activationTime);
  }
  
  return changeMade;
}

void publishToMakoTestMessage(const char* testMessage)
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"T%s",testMessage);
    if (writeLogToSerial)
    {
      USB_SERIAL.println("Sending ESP T msg to Mako...");
      USB_SERIAL.println(mako_espnow_buffer);
    }

    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
  }
}

void publishToMakoReedActivation(const bool topReed, const uint32_t ms)
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"R%c%lu       ",(topReed ? 'T' : 'B'),ms);
    if (writeLogToSerial)
    {
      USB_SERIAL.println("Sending ESP R msg to Mako...");
      USB_SERIAL.println(mako_espnow_buffer);
    }
    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
  }
  else
  {
    if (writeLogToSerial)
      USB_SERIAL.println("ESPNow inactive - not sending ESP R msg to Mako...");
  }
}

uint32_t  nextLeakMessagePublishTime = 0;
const uint32_t leakMessageDutyCycle = 3000;

void publishToMakoLeakDetected()
{
  if (millis() > nextLeakMessagePublishTime)
  {
    nextLeakMessagePublishTime = millis() + leakMessageDutyCycle;

    if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
    {
      snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"L");
      if (writeLogToSerial)
      {
        USB_SERIAL.println("Sending ESP L msg to Mako...");
        USB_SERIAL.println(mako_espnow_buffer);
      }
      ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
    }
    else
    {
      if (writeLogToSerial)
        USB_SERIAL.println("ESPNow inactive - not sending ESP L msg to Mako...");
    }
  }
}

void loop()
{
  if (haltAllProcessingDuringOTAUpload)
  {
    delay(500);
    toggleRedLED();
    return;
  }

  if (msgsReceivedQueue && !otaActive)
  {
    if (xQueueReceive(msgsReceivedQueue,&(rxQueueItemBuffer),(TickType_t)0))
    {
      if (!isPairedWithMako)    // only pair with Mako once first message received from Mako.
        pairWithMako();

      switch(rxQueueItemBuffer[0])
      {
        case 'l':   // Bright light detected by Mako - switch to next screen
        {
          cycleDisplays();
          break;
        }

        case 'c':   // current target
        {
          if (strcmp(rxQueueItemBuffer+1,currentTarget) != 0)
          {
            strncpy(previousTarget,currentTarget,sizeof(previousTarget));
            strncpy(currentTarget,rxQueueItemBuffer+1,sizeof(currentTarget));
            refreshTargetShown = true;
          }
          break;
        }

        case 'X':   // location, heading and current Target info.
        {
          // format: targetCode[7],lat,long,heading,targetText
          const int targetCodeOffset = 1;
          const int latitudeOffset = 8;
          const int longitudeOffset = 16;
          const int headingOffset = 24;
          const int currentTargetOffset = 32;
                    
          char targetCode[7];

          double old_latitude = latitude;
          double old_longitude = longitude;
          double old_heading = heading;

          strncpy(targetCode,rxQueueItemBuffer + targetCodeOffset,sizeof(targetCode));
          memcpy(&latitude,  rxQueueItemBuffer + latitudeOffset,  sizeof(double));
          memcpy(&longitude, rxQueueItemBuffer + longitudeOffset, sizeof(double));
          memcpy(&heading,   rxQueueItemBuffer + headingOffset, sizeof(double));

          if (*currentTarget == '\0' ||
              strcmp(rxQueueItemBuffer+currentTargetOffset,currentTarget) != 0)
          {
            strncpy(previousTarget,currentTarget,sizeof(previousTarget));
            strncpy(currentTarget,rxQueueItemBuffer+currentTargetOffset,sizeof(currentTarget));
            refreshTargetShown = true;
          }

          if (writeLogToSerial)
          {
            USB_SERIAL.printf("targetCode: %s\n",targetCode);
            USB_SERIAL.printf("latitude: %f\n",latitude);
            USB_SERIAL.printf("longitude: %f\n",longitude);
            USB_SERIAL.printf("heading: %f\n",heading);
          }

          mapScreen->setTargetWaypointByLabel(targetCode);

          if (testPreCannedLatLong)
          {
            latitude = old_latitude;
            longitude = old_longitude+0.00001;
            heading = static_cast<int>((old_heading + 5)) % 360;
          }

          if (mode_ == 6) // map on screen
            mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);
          else if (mode_ == 5 && refreshTargetShown)
            resetCurrentTarget();
        }
        default:
        {
          break;
        }
      }
    }
  }

  if ( mode_ == 6) { vfd_6_map();}
  if ( mode_ == 5) { vfd_5_current_target();}
  if ( mode_ == 3 ){ vfd_3_line_clock();}   // hh,mm,ss, optional dd mm

//  readAndTestGoProReedSwitches();

  for (int m=0;m<10;m++)
  {
    delay(50);
    checkForLeak(leakAlarmMsg,PENETRATOR_LEAK_DETECTOR_PIN);

    if (checkReedSwitches()) // If a change occurred break out of wait loop to make change asap.
    {
      break;
    }
  }
}

void vfd_6_map()
{
  // do nothing
}

void vfd_5_current_target()
{
  if (refreshTargetShown)
  {
    M5.Lcd.fillScreen(TFT_BLACK);
    refreshTargetShown = false;
  }

  if (ESPNowActive && isPairedWithMako)
  {    
    M5.Lcd.setCursor(0,0);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
    M5.Lcd.println(currentTarget);
  }
  else
  {
    M5.Lcd.setCursor(0,0);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);

    if (!ESPNowActive)
      M5.Lcd.println("  No\nTarget\n\nESPNow\nIs Off\n");
    else
      M5.Lcd.println("  No\nTarget\n\nESPNow\nNo Pair\n");
    
    if (otaActive)
    {
      M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
      M5.Lcd.println("OTA On\n");        
    }
    else
    { 
      M5.Lcd.println("OTA Off\n");
    }
  }

  if (otaActive)
  {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, mode_label_y_offset+10);
    M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Lcd.printf("%s",WiFi.localIP().toString());
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("");
    M5.Lcd.setTextSize(3);
  }
  else
  {
    M5.Lcd.setCursor(0, mode_label_y_offset+10);
    M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Lcd.println("Towards");
  }  
  M5.Lcd.setCursor(28, mode_label_y_offset+38);
  M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
  getTime(currentTime);
  M5.Lcd.printf("%s",currentTime);  
}

void draw_digits(int h1, int h2, int i1, int i2, int s1, int s2)
{
  draw_digit_text(h1,h2,i1,i2,s1,s2);
}


void draw_digit_text(int h1, int h2, int i1, int i2, int s1, int s2)
{
  M5.Lcd.setTextSize(9);
  M5.Lcd.setTextFont(0);
  M5.Lcd.setTextColor(TFT_ORANGE,TFT_BLACK);

  M5.Lcd.setCursor(5,5);
  M5.Lcd.printf("%i%i",h1,h2);
  M5.Lcd.setCursor(M5.Lcd.getCursorX()-10,M5.Lcd.getCursorY());
  M5.Lcd.printf(":\n",h1,h2);
  M5.Lcd.setCursor(M5.Lcd.getCursorX()+5,M5.Lcd.getCursorY());
  M5.Lcd.printf("%i%i",i1,i2);

  if (s1 != -1 && s2 != -1)
  {
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(50,120);
    M5.Lcd.printf("%i%i",s1,s2);
  }
}

void vfd_3_line_clock()
{    // Clock mode - Hours, mins, secs with optional date
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  int h1 = int(RTC_TimeStruct.Hours / 10 );
  int h2 = int(RTC_TimeStruct.Hours - h1*10 );
  int i1 = int(RTC_TimeStruct.Minutes / 10 );
  int i2 = int(RTC_TimeStruct.Minutes - i1*10 );
  int s1 = int(RTC_TimeStruct.Seconds / 10 );
  int s2 = int(RTC_TimeStruct.Seconds - s1*10 );

  // print current and voltage of USB
  if (showPowerStats)
  {
    M5.Lcd.setCursor(5,5);
    M5.Lcd.printf("USB %.1fV, %.0fma\n",  M5.Axp.GetVBusVoltage(),M5.Axp.GetVBusCurrent());
    M5.Lcd.printf("Batt Charge %.0fma\n",  M5.Axp.GetBatChargeCurrent());
    M5.Lcd.printf("Batt %.1fV %.0fma\n",  M5.Axp.GetBatVoltage(), M5.Axp.GetBatCurrent());
  }
  else
  {
    draw_digits(h1, h2, i1, i2, s1, s2);

    M5.Lcd.setTextSize(2);
     
    M5.Lcd.setCursor(35, mode_label_y_offset+28);
    if (otaActive)
    {
      M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
      M5.Lcd.printf("OTA On");
    }
    else if (isPairedWithMako && ESPNowActive)
    {
      M5.Lcd.setCursor(25, mode_label_y_offset+28);
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
      M5.Lcd.printf("ESPNow+");
    }
    else if (!isPairedWithMako)
    {
      M5.Lcd.setCursor(25, mode_label_y_offset+28);
      M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
      M5.Lcd.printf("Paired-");
    }
    
    // M5.Lcd.print("Time");
  }
}

void getTime(char* time)
{    // Clock mode - Hours, mins, secs with optional date
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  int h = int(RTC_TimeStruct.Hours);
  int m = int(RTC_TimeStruct.Minutes);
  snprintf(time,sizeof(currentTime),"%02d:%02d",h,m);
}

void updateButtonsAndBuzzer()
{
  p_primaryButton->read();
  p_secondButton->read();
  LeakDetectorSwitch.read();
  M5.Beep.update();

  if (p_primaryButton->isPressed())
  {
    if (!primaryButtonIsPressed)
    {
      primaryButtonIsPressed=true;
      primaryButtonPressedTime=millis();
    }
  }
  else
  {
    if (primaryButtonIsPressed)
    {
      lastPrimaryButtonPressLasted = millis() - primaryButtonPressedTime;
      primaryButtonIsPressed=false;
      primaryButtonPressedTime=0;
    }
  }

  if (p_secondButton->isPressed())
  {
    if (!secondButtonIsPressed)
    {
      secondButtonIsPressed=true;
      secondButtonPressedTime=millis();
    }
  }
  else
  {
    if (secondButtonIsPressed)
    {
      lastSecondButtonPressLasted = millis() - secondButtonPressedTime;
      secondButtonIsPressed=false;
      secondButtonPressedTime=0;
    }
  }
}

void configAndStartUpESPNow()
{  
  //Set device in AP mode to begin with
  WiFi.mode(WIFI_AP);
  
  // configure device AP mode
  configESPNowDeviceAP();
  
  // This is the mac address of this peer in AP Mode
  if (writeLogToSerial)
  {
    USB_SERIAL.print("AP MAC: "); 
    USB_SERIAL.println(WiFi.softAPmacAddress());
  }
  // Init ESPNow with a fallback logic
  InitESPNow();
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info.
  esp_now_register_send_cb(OnESPNowDataSent);
  esp_now_register_recv_cb(OnESPNowDataRecv);
}

void configESPNowDeviceAP()
{
  String Prefix = "Tiger:";
  String Mac = WiFi.macAddress();
  String SSID = Prefix + Mac;
  String Password = "123456789";
  bool result = WiFi.softAP(SSID.c_str(), Password.c_str(), ESPNOW_CHANNEL, 0);

  if (writeLogToSerial)
  {
    if (!result)
    {
      USB_SERIAL.println("AP Config failed.");
    }
    else
    {
      USB_SERIAL.printf("AP Config Success. Broadcasting with AP: %s\n",String(SSID).c_str());
      USB_SERIAL.printf("WiFi Channel: %d\n",WiFi.channel());
    }
  }
}

void InitESPNow()
{
  WiFi.disconnect();
  if (esp_now_init() == ESP_OK)
  {
    if (writeLogToSerial)
      USB_SERIAL.println("ESPNow Init Success");
    ESPNowActive = true;
  }
  else
  {
    if (writeLogToSerial)
      USB_SERIAL.println("ESPNow Init Failed");
    ESPNowActive = false;
  }
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
  if (writeLogToSerial)
  {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    USB_SERIAL.printf("Last Packet Recv from: %s\n",macStr);
    USB_SERIAL.printf("Last Packet Recv 1st Byte: '%c'\n",*data);
    USB_SERIAL.printf("Last Packet Recv Length: %d\n",data_len);
    USB_SERIAL.println((char*)data);
  }

  xQueueSend(msgsReceivedQueue, (void*)data, (TickType_t)0);  // don't block on enqueue, just drop if queue is full
}

bool TeardownESPNow()
{
  bool result = false;

  if (enableESPNow && ESPNowActive)
  {
    WiFi.disconnect();
    ESPNowActive = false;
    result = true;
  }
  
  return result;
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

    if (writeLogToSerial)
      USB_SERIAL.printf("Found:\n%s\n",network);
  }
  else
  {
    M5.Lcd.println("None\nFound");
    if (writeLogToSerial)
      USB_SERIAL.println("No networks Found\n");
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
    digitalWrite(RED_LED_GPIO, LOW);
  }
  else if (d=="OFF"){
    digitalWrite(RED_LED_GPIO, HIGH);
  }
  else if (d=="serial-off")
  {
    writeLogToSerial = false;
    WebSerial.closeAll();
  }
}

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED)
  {
    if (writeLogToSerial)
      USB_SERIAL.printf("setupOTAWebServer: attempt to connect wifiOnly, already connected - otaActive=%i\n",otaActive);

    return true;
  }

  if (writeLogToSerial)
    USB_SERIAL.printf("setupOTAWebServer: attempt to connect %s wifiOnly=%i when otaActive=%i\n",_ssid, wifiOnly,otaActive);

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

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: WiFi connected ok, starting up OTA");

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: calling asyncWebServer.on");

      asyncWebServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(200, "text/plain", "To upload firmware use /update");
      });
        
      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: calling AsyncElegantOTA.begin");

      AsyncElegantOTA.setID(MERCATOR_OTA_DEVICE_LABEL);
      AsyncElegantOTA.setUploadBeginCallback(uploadOTABeginCallback);
      AsyncElegantOTA.begin(&asyncWebServer);    // Start AsyncElegantOTA

      static bool webSerialInitialised = false;

      if (!webSerialInitialised)
      {
        WebSerial.begin(&asyncWebServer);
        WebSerial.msgCallback(webSerialReceiveMessage);
        webSerialInitialised = true;
      }

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: calling asyncWebServer.begin");

      asyncWebServer.begin();

      dumpHeapUsage("setupOTAWebServer(): after asyncWebServer.begin");

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: OTA setup complete");

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
    if (writeLogToSerial)
      USB_SERIAL.printf("setupOTAWebServer: WiFi failed to connect %s\n",_ssid);

    M5.Lcd.print("No Connect");
  }

  M5.Lcd.fillScreen(TFT_BLACK);

  dumpHeapUsage("setupOTAWebServer(): end of function");

  return connected;
}

bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED)
    return true;

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);

  while (repeatScanAttempts-- &&
         (WiFi.status() != WL_CONNECTED ||
          WiFi.status() == WL_CONNECTED && wifiOnly == false && otaActive == false ) )
  {
    const char* network = scanForKnownNetwork();

    if (!network)
    {
      delay(1000);
      continue;
    }
  
    int connectToFoundNetworkAttempts = 3;
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
    
    delay(1000);
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

// Scan for peers in AP mode
bool ESPNowScanForPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const bool suppressPeerFoundMsg)
{
  bool peerFound = false;
  
  M5.Lcd.printf("Scan For\n%s\n",peerSSIDPrefix);
  int8_t scanResults = WiFi.scanNetworks();
  
  // reset on each scan 
  memset(&peer, 0, sizeof(peer));

  if (writeLogToSerial)
    USB_SERIAL.println("");

  if (scanResults == 0) 
  {   
    if (writeLogToSerial)
      USB_SERIAL.println("No WiFi devices in AP Mode found");

    peer.channel = ESPNOW_NO_PEER_CHANNEL_FLAG;
  } 
  else 
  {
    if (writeLogToSerial)
    {
      USB_SERIAL.print("Found "); USB_SERIAL.print(scanResults); USB_SERIAL.println(" devices ");
    }
    
    for (int i = 0; i < scanResults; ++i) 
    {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);

      if (writeLogToSerial && ESPNOW_PRINTSCANRESULTS) 
      {
        USB_SERIAL.print(i + 1);
        USB_SERIAL.print(": ");
        USB_SERIAL.print(SSID);
        USB_SERIAL.print(" (");
        USB_SERIAL.print(RSSI);
        USB_SERIAL.print(")");
        USB_SERIAL.println("");
      }
      
      delay(10);
      
      // Check if the current device starts with the peerSSIDPrefix
      if (SSID.indexOf(peerSSIDPrefix) == 0) 
      {
        if (writeLogToSerial)
        {
          // SSID of interest
          USB_SERIAL.println("Found a peer.");
          USB_SERIAL.print(i + 1); USB_SERIAL.print(": "); USB_SERIAL.print(SSID); USB_SERIAL.print(" ["); USB_SERIAL.print(BSSIDstr); USB_SERIAL.print("]"); USB_SERIAL.print(" ("); USB_SERIAL.print(RSSI); USB_SERIAL.print(")"); USB_SERIAL.println("");
        }
                
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
      if (writeLogToSerial)
        USB_SERIAL.println("Peer Found, processing..");
    } 
    else 
    {
      M5.Lcd.println("Peer Not Found");
      if (writeLogToSerial)
        USB_SERIAL.println("Peer Not Found, trying again.");
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

    if (writeLogToSerial)
      USB_SERIAL.print("Peer Status: ");

    // check if the peer exists
    bool exists = esp_now_is_peer_exist(peer.peer_addr);

    if (exists)
    {
      // Peer already paired.
      if (writeLogToSerial)
        USB_SERIAL.println("Already Paired");

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
        if (writeLogToSerial)
          USB_SERIAL.println("Pair success");
        M5.Lcd.println("Pair success");
        result = true;
      }
      else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT)
      {
        // How did we get so far!!
        if (writeLogToSerial)
          USB_SERIAL.println("ESPNOW Not Init");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_ARG)
      {
        if (writeLogToSerial)
            USB_SERIAL.println("Invalid Argument");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_FULL)
      {
        if (writeLogToSerial)
            USB_SERIAL.println("Peer list full");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_NO_MEM)
      {
        if (writeLogToSerial)
          USB_SERIAL.println("Out of memory");
        result = false;
      }
      else if (addStatus == ESP_ERR_ESPNOW_EXIST)
      {
        if (writeLogToSerial)
          USB_SERIAL.println("Peer Exists");
        result = true;
      }
      else
      {
        if (writeLogToSerial)
          USB_SERIAL.println("Not sure what happened");
        result = false;
      }
    }
  }
  else
  {
    // No peer found to process
    if (writeLogToSerial)
      USB_SERIAL.println("No Peer found to process");

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

    if (writeLogToSerial)
    {
      USB_SERIAL.print("Peer Delete Status: ");
      if (delStatus == ESP_OK)
      {
        // Delete success
        USB_SERIAL.println("ESPNowDeletePeer::Success");
      }
      else if (delStatus == ESP_ERR_ESPNOW_NOT_INIT)
      {
        // How did we get so far!!
        USB_SERIAL.println("ESPNowDeletePeer::ESPNOW Not Init");
      }
      else if (delStatus == ESP_ERR_ESPNOW_ARG)
      {
        USB_SERIAL.println("ESPNowDeletePeer::Invalid Argument");
      }
      else if (delStatus == ESP_ERR_ESPNOW_NOT_FOUND)
      {
        USB_SERIAL.println("ESPNowDeletePeer::Peer not found.");
      }
      else
      {
        USB_SERIAL.println("Not sure what happened");
      }
    }
  }
}
