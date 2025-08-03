#include <Arduino.h>

#include <M5StickCPlus.h>
#include <MapScreen_M5.h>

#include <WebSerial.h>
#include "SerialConfig.h"

//#define USE_WEBSERIAL

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
#include "soc/rtc_wdt.h"
#include "esp_task_wdt.h"
#include <time.h>

// rename the git file "mercator_secrets_template.c" to the filename below, filling in your wifi credentials etc.
#include "mercator_secrets.c"

bool writeLogToSerial=false;
bool testPreCannedLatLong=false;       // test that animates the diver sprite through slow movements across the lake.
bool goProButtonsPrimaryControl = false;

bool enableOTAServerAtStartup=false; // OTA updates - don't set true without disabling mapscreen, insufficient heap

const bool enableESPNow = !enableOTAServerAtStartup; // cannot have OTA server on regular wifi and espnow concurrently running

const String ssid_not_connected = "-";
String ssid_connected;

const char* buildTimestamp = __DATE__ " " __TIME__;


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

const uint8_t REED_GOPRO_TOP_GPIO=25;
const uint8_t REED_GOPRO_SIDE_GPIO=0;
const uint8_t UNUSED_GPIO_36_PIN=36;
const uint8_t M5_POWER_SWITCH_PIN=255;
const uint32_t MERCATOR_DEBOUNCE_MS=100;
const uint8_t PENETRATOR_LEAK_DETECTOR_PIN=26;

Button ReedSwitchGoProTop = Button(REED_GOPRO_TOP_GPIO, true, MERCATOR_DEBOUNCE_MS);    // from utility/Button.h for M5 Stick C Plus
Button ReedSwitchGoProSide = Button(REED_GOPRO_SIDE_GPIO, true, MERCATOR_DEBOUNCE_MS); // from utility/Button.h for M5 Stick C Plus
Button LeakDetectorSwitch = Button(PENETRATOR_LEAK_DETECTOR_PIN, true, MERCATOR_DEBOUNCE_MS); // from utility/Button.h for M5 Stick C Plus
uint16_t sideCount = 0, topCount = 0;

bool isTopReedClosed() { // Direct GPIO Read Bypass button press code
  return digitalRead(REED_GOPRO_TOP_GPIO) == false;
}

bool isSideReedClosed() { // Direct GPIO Read Bypass button press code
  return digitalRead(REED_GOPRO_SIDE_GPIO) == false;
}

bool isLeakDetected() { // Direct GPIO Read Bypass button press code
  return digitalRead(PENETRATOR_LEAK_DETECTOR_PIN) == false;
}

bool topReedActiveAtStartup = false;
bool sideReedActiveAtStartup = false;

  
bool recoveryScreenShown = false;

const uint16_t mode_label_y_offset = 170;

AsyncWebServer asyncWebServer(80);      // OTA updates
bool otaActive=false;   // OTA updates toggle
uint32_t restartAfterGoodOTAUpdateAt = millis() + 3000;
uint32_t restartForGoodOTAScheduled = false;

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
int   daylightOffset_sec = 0;   // DST offset - 3600 in the summer

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
void drawClockDisplay();
void drawCurrentTargetTempDisplay();
void drawDisplay();
void drawMapDisplay();
void getTime(char* time);
void drawDigits(int h1, int h2, int i1, int i2, int s1, int s2);
void drawDigitText(int h1, int h2, int i1, int i2, int s1, int s2);
void resetCurrentTarget();
void resetMap();
void resetClock();
void fadeToBlackAndShutdown();
const char* scanForKnownNetwork();
bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly = false);
void updateButtonsAndBuzzer();
void readAndTestGoProReedSwitches();
bool leakAlarmActive = false;
void checkForLeak(const char* msg);
void checkUSBPowerAndAutoShutdown();
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

uint8_t redLEDStatus = HIGH;  // HIGH == off, LOW == on

void toggleRedLED() {
  redLEDStatus = (redLEDStatus == HIGH ? LOW : HIGH ); digitalWrite(RED_LED_GPIO, redLEDStatus);
}

void setRedLEDOn() {
  redLEDStatus = LOW; digitalWrite(RED_LED_GPIO, redLEDStatus);
}

void setRedLEDOff() {
  redLEDStatus = HIGH; digitalWrite(RED_LED_GPIO, redLEDStatus);
}

void initRedLed() {
  pinMode(RED_LED_GPIO, OUTPUT); setRedLEDOff();
}

void dumpHeapUsage(const char* msg)
{  
  if (writeLogToSerial)
  {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
    USB_SERIAL_PRINTF("\n%s : free heap bytes: %i  largest free heap block: %i min free ever: %i\n",  msg, info.total_free_bytes, info.largest_free_block, info.minimum_free_bytes);
  }
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
    M5.Lcd.println("  Mako OTA\n    Ready\n");
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

bool haltAllProcessingDuringOTAUpload = false;
bool forceLoopInitialOTAEnablement = false;

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

  mapScreen.reset();      // delete mapscreen to save heapspace prior to OTA

  WebSerial.closeAll();   // close all websocket connetions for WebSerial
}
void setup()
{
  M5.begin();
    
  dumpHeapUsage("Setup(): start");
  currentTarget[0]='\0';
  previousTarget[0]='\0';

  initRedLed();
  
#ifndef USE_WEBSERIAL
  USB_SERIAL.begin(115200);
#endif

  ssid_connected = ssid_not_connected;

  uint32_t start = millis();
  while(millis() < start + 2000)
  {
    if (isTopReedClosed())
    {
      enableOTAServerAtStartup = true;
      topReedActiveAtStartup = true;
      break;
    }

    if (isSideReedClosed())
    {
      // Cannot use this as side GPIO = 0 , a strapping pin
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

  if (msgsReceivedQueue == nullptr)
    USB_SERIAL_PRINTLN("Failed to create queue");
  else
    USB_SERIAL_PRINTLN("Created msg queue");

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


void checkForLeak(const char* msg)
{
  if (isLeakDetected())
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
        USB_SERIAL_PRINTLN("cycleDisplays Error: invalid mode_");
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
        USB_SERIAL_PRINTLN("cycleDisplays Error: invalid mode_");
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

    USB_SERIAL_PRINTLN("Cycle To Next Display");

    cycleDisplays();
  }

  // press second button for 10 seconds reboot
  if (p_secondButton->wasReleasefor(10000))
  { 
    USB_SERIAL_PRINTLN("Reboot");

     esp_restart();
  }
  // press second button for 5 seconds to attempt WiFi connect and enable OTA
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
    USB_SERIAL_PRINTLN("Enable OTA Mode");
  }
  // press second button for 1 second to toggle all features on the map
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
    USB_SERIAL_PRINTLN("Toggle show all map features");
  }
  // tap second button for 0.1 second to change zoom level of map
  else if (p_secondButton->wasReleasefor(100))
  {
    activationTime = lastSecondButtonPressLasted;
    reedSwitchTop = false;

    if (mode_ == 6) // map mode - cycle zoom
    {
      mapScreen->cycleZoom(); changeMade = true;
      mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);

      USB_SERIAL_PRINTLN("Cycle zoom level on map");
    }
  }
  
  // Notify Mako of the reed switch being activated
  if (activationTime > 0)
  {
    USB_SERIAL_PRINTLN("Reed Activated...");

    publishToMakoReedActivation(reedSwitchTop, activationTime);
  }
  
  return changeMade;
}

void processIncomingESPNowMessages()
{
 if (msgsReceivedQueue && !otaActive)
  {
    if (xQueueReceive(msgsReceivedQueue,&(rxQueueItemBuffer),(TickType_t)0))
    {
      if (!isPairedWithMako)    // only pair with Mako once first message received from Mako.
        pairWithMako();

      switch(rxQueueItemBuffer[0])
      {
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

          USB_SERIAL_PRINTF("targetCode: %s\n",targetCode);
          USB_SERIAL_PRINTF("latitude: %f\n",latitude);
          USB_SERIAL_PRINTF("longitude: %f\n",longitude);
          USB_SERIAL_PRINTF("heading: %f\n",heading);

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
      otaActive = connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts);
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

    // After 5 seconds of recovery screen, allow restart if any button is pressed 
    if (recoveryScreenShown && (millis() - recoveryScreenStartTime > 5000)) 
    {            
      // check if either button is pressed once 5 seconds has passed since ota screen was shown
      
      if (isTopReedClosed() || isSideReedClosed()) {
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

/////////////// EVENT LOOP
void loop()
{
  //////// PROTECTED - DO NOT ADD CODE BEFORE OR WITHIN THE OTA DEMAND CHECK BELOW  - RISK OF OTA FAILURE
  if (cutShortLoopOnOTADemand())
    return;
  ///////////////////////////////////////////////////////////////////////////////////////

  // check for incoming messages
  processIncomingESPNowMessages();

  drawDisplay();
  
  for (int m=0;m<10;m++)
  {
    delay(50);
    checkForLeak(leakAlarmMsg);

    if (checkReedSwitches()) // If a change occurred break out of wait loop to make change asap.
    {
      break;
    }
  }
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


////////////////////////////////////////////////////////////////////////
///////////////////////////////// WiFi/Network/OTA Functions
////////////////////////////////////////////////////////////////////////

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
        USB_SERIAL_PRINTLN("No time available (yet)");
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
      USB_SERIAL_PRINTLN("NTP time received");
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

void getTime(char* time)
{    // Clock mode - Hours, mins, secs with optional date
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  int h = int(RTC_TimeStruct.Hours);
  int m = int(RTC_TimeStruct.Minutes);
  snprintf(time,sizeof(currentTime),"%02d:%02d",h,m);
}

////////////////////////////////////////////////////////////////////////
///////////////////////////////// ESPNow Message Functions
////////////////////////////////////////////////////////////////////////

void publishToMakoTestMessage(const char* testMessage)
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"T%s",testMessage);
    USB_SERIAL_PRINTLN("Sending ESP T msg to Mako...");
    USB_SERIAL_PRINTLN(mako_espnow_buffer);

    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
  }
}

void publishToMakoReedActivation(const bool topReed, const uint32_t ms)
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"R%c%lu       ",(topReed ? 'T' : 'B'),ms);
    USB_SERIAL_PRINTLN("Sending ESP R msg to Mako...");
    USB_SERIAL_PRINTLN(mako_espnow_buffer);
    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
  }
  else
  {
    USB_SERIAL_PRINTLN("ESPNow inactive - not sending ESP R msg to Mako...");
  }
}

void publishToMakoLeakDetected()
{
  static uint32_t  nextLeakMessagePublishTime = 0;
  const uint32_t   leakMessageDutyCycle = 3000;

  if (millis() > nextLeakMessagePublishTime)
  {
    nextLeakMessagePublishTime = millis() + leakMessageDutyCycle;

    if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
    {
      snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"L");
      USB_SERIAL_PRINTLN("Sending ESP L msg to Mako...");
      USB_SERIAL_PRINTLN(mako_espnow_buffer);
      ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
    }
    else
    {
      USB_SERIAL_PRINTLN("ESPNow inactive - not sending ESP L msg to Mako...");
    }
  }
}

////////////////////////////////////////////////////////////////////////
///////////////////////////////// ESPNow Functions
////////////////////////////////////////////////////////////////////////

void InitESPNow()
{
  WiFi.disconnect();
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
}


void configAndStartUpESPNow()
{  
  //Set device in AP mode to begin with
  WiFi.mode(WIFI_AP);
  
  // configure device AP mode
  configESPNowDeviceAP();
  
  // This is the mac address of this peer in AP Mode
  USB_SERIAL_PRINT("AP MAC: "); 
  USB_SERIAL_PRINTLN(WiFi.softAPmacAddress());
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

  if (!result)
  {
    USB_SERIAL_PRINTLN("AP Config failed.");
  }
  else
  {
    USB_SERIAL_PRINTF("AP Config Success. Broadcasting with AP: %s\n",String(SSID).c_str());
    USB_SERIAL_PRINTF("WiFi Channel: %d\n",WiFi.channel());
  }
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
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  USB_SERIAL_PRINTF("Last Packet Recv from: %s\n",macStr);
  USB_SERIAL_PRINTF("Last Packet Recv 1st Byte: '%c'\n",*data);
  USB_SERIAL_PRINTF("Last Packet Recv Length: %d\n",data_len);
  USB_SERIAL_PRINTLN((char*)data);

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

////////////// TEST FUNCTIONS
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

#define BUILD_INCLUDE_MAIN_DISPLAY_CODE
#include "main_display_code.cpp"