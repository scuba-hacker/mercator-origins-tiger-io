#include <Arduino.h>

#include <M5StickCPlus.h>
#include <MapScreen_M5.h>

#include <WebSerial.h>
#include "SerialConfig.h"

#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>
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

bool writeLogToSerial=true;
bool testPreCannedLatLong=false;       // test that animates the diver sprite through slow movements across the lake.
bool testGPSTimezone=true;           // test GPS timezone detection with simulated coordinates
bool goProButtonsPrimaryControl = false;

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

const uint8_t REED_GOPRO_TOP_GPIO=25;
const uint8_t REED_GOPRO_SIDE_GPIO=0;
const uint8_t UNUSED_GPIO_36_PIN=36;
const uint8_t M5_POWER_SWITCH_PIN=255;
const uint32_t MERCATOR_DEBOUNCE_MS=100;
const uint8_t LEAK_DETECTOR_GPIO=26;

const uint8_t M5_BUTTON_A_PIN = BUTTON_A_PIN;
const uint8_t M5_BUTTON_B_PIN = BUTTON_B_PIN;

Button ReedSwitchGoProTop = Button(REED_GOPRO_TOP_GPIO, true, MERCATOR_DEBOUNCE_MS);    // from utility/Button.h for M5 Stick C Plus
Button ReedSwitchGoProSide = Button(REED_GOPRO_SIDE_GPIO, true, MERCATOR_DEBOUNCE_MS); // from utility/Button.h for M5 Stick C Plus
Button LeakDetectorSwitch = Button(LEAK_DETECTOR_GPIO, true, MERCATOR_DEBOUNCE_MS); // from utility/Button.h for M5 Stick C Plus
uint16_t sideCount = 0, topCount = 0;

bool isTopReedClosed() { // Direct GPIO Read Bypass button press code
  return digitalRead(REED_GOPRO_TOP_GPIO) == false;
}

bool isSideReedClosed() { // Direct GPIO Read Bypass button press code
  return digitalRead(REED_GOPRO_SIDE_GPIO) == false;
}

bool isButtonAPressed() { // Direct GPIO Read Bypass button press code
  return digitalRead(M5_BUTTON_A_PIN) == false;
}

bool isButtonBPressed() { // Direct GPIO Read Bypass button press code
  return digitalRead(M5_BUTTON_B_PIN) == false;
}

bool isLeakDetected() { // Direct GPIO Read Bypass button press code
  return digitalRead(LEAK_DETECTOR_GPIO) == false;
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
//const long  gmtOffset_sec = 0;        // timezone offset
//int   daylightOffset_sec = 0;   // DST offset - 3600 in the summer

// Timezone correction variables
bool timezoneSetFromIP = false;
bool timezoneVerifiedFromGPS = false;
long detectedTimezoneOffset = 0;

RTC_TimeTypeDef RTC_TimeStruct;
RTC_DateTypeDef RTC_DateStruct;

const char* leakAlarmMsg = "\nWATER\n\nLEAK\n\nALARM";

int mode_ = 3; // clock

const int defaultBrightness = 100;

char rxQueueItemBuffer[256];
const uint8_t queueLength=4;

char currentTarget[128];
char previousTarget[128];
bool refreshTargetShown = false;

const float minimumUSBVoltage=2.0;
long USBVoltageDropTime=0;
long milliSecondsToWaitForShutDown=100;

bool systemStartupAndCheckForOTADemand();
bool cutShortLoopOnOTADemand();
void initialiseRTCfromNTP();
bool detectTimezoneFromIP(long& timezoneOffset);
bool detectTimezoneFromGPS(double lat, double lon);
bool updateRTCFromNTP(const char* context,long timezoneOffset, int dstOffset);
bool cycleDisplays(bool refreshCurrentDisplay = false, int setDisplayTo = 0);
bool checkReedSwitches();
void shutdownIfUSBPowerOff();
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
const char* scanForKnownNetwork();
bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly = false);
void updateButtonsAndBuzzer();
void displayReedActivationIndicators();
void readAndTestGoProReedSwitches();
bool leakAlarmActive = false;
uint32_t leakAlarmStartTime = 0;
uint32_t leakAlarmLastShowTime = 0;
uint8_t leakAlarmShowCount = 0;
bool leakAlarmCurrentlyShowing = false;
uint32_t leakAlarmShowStartTime = 0;
bool simulatedLeakActive = false;

// Initial alarm flash sequence variables
bool leakAlarmInInitialFlash = false;
uint8_t leakAlarmFlashCycle = 0;
uint32_t leakAlarmFlashTime = 0;
bool leakAlarmFlashOn = false;

// Display update timing
uint32_t lastDisplayUpdateTime = 0;
const uint32_t DISPLAY_UPDATE_INTERVAL = 100;

// AXP temperature update timing
uint32_t lastAXPTempUpdateTime = 0;
const uint32_t AXP_TEMP_UPDATE_INTERVAL = 10000;
float cachedAXPTemperature = 0.0;

// Button indicator update timing (synchronized with display)
uint32_t lastButtonIndicatorUpdateTime = 0;

const uint32_t DURATION_BETWEEN_ALARM_BURSTS = 2 * 60 * 1000; // 2 minutes
const uint32_t LEAK_ALARM_TONE_1_FREQ     = 1200;
const uint32_t LEAK_ALARM_TONE_1_DURATION = 100;
const uint32_t LEAK_ALARM_TONE_2_FREQ     = 1500;
const uint32_t LEAK_ALARM_TONE_2_DURATION = 100;
const uint8_t MAX_ALARM_BURST_CYCLES = 30;        // (entire burst alarm is 30 x (100 + 100) = 6 seconds)

void checkForLeak(const char* msg);
void initialiseLeakAlarm(const char* msg);
void hideLeakAlarm();
void checkUSBPowerAndAutoShutdown();
void processIncomingESPNowMessages();
void InitESPNow();
void configAndStartUpESPNow();
void configESPNowDeviceAP();
void OnESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnESPNowDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len);
bool ESPNowScanForPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, const bool suppressPeerFoundMsg = true);
bool pairWithMako();
bool pairWithPeer(esp_now_peer_info_t& peer, const char* peerSSIDPrefix, int maxAttempts);
bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts, const char* message);
bool ESPNowManagePeer(esp_now_peer_info_t& peer);
void ESPNowDeletePeer(esp_now_peer_info_t& peer);
bool TeardownESPNow();
void handleOTAShutdown();

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

bool haltAllProcessingDuringOTAUpload = false;
bool forceLoopInitialOTAEnablement = false;
const char* buildTimestamp = __DATE__ " " __TIME__;

// OTA shutdown variables
bool otaShutdownRequested = false;
uint32_t otaShutdownStartTime = 0;

void dumpHeapUsage(const char* msg)
{  
  if (writeLogToSerial)
  {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
    USB_SERIAL_PRINTF("\n%s : free heap bytes: %i  largest free heap block: %i min free ever: %i\n",  msg, info.total_free_bytes, info.largest_free_block, info.minimum_free_bytes);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////// PROTECTED - DO NOT ADD CODE IN THE ABOVE PROTECTED AREA - RISK OF OTA FAILURE
////////////////////////////////////////////////////////////////////////////////////////////////////
void setup()
{
  //////// PROTECTED - DO NOT ADD CODE BEFORE THE OTA DEMAND CHECK  - RISK OF OTA FAILURE
  if (systemStartupAndCheckForOTADemand())
    return;     // OTA Required, skip rest of setup.
  
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

  M5.Lcd.setTextSize(2);

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

bool checkReedSwitches()
{  
  bool changeMade = false;

  bool reedSwitchTop;
  uint32_t activationTime=0;
    
  updateButtonsAndBuzzer();

  displayReedActivationIndicators();

  // Check for 20-second press to simulate leak (TEST MODE)  
  const uint32_t PRIMARY_BUTTON_SIMULATE_LEAK_PRESS = 20000;            // Any display
  const uint32_t PRIMARY_BUTTON_ESPNOW_ON_PRESS = 5000;                 // Any display
  const uint32_t PRIMARY_BUTTON_CYCLE_DISPLAY_PRESS = 100;              // Any display

  const uint32_t SECOND_BUTTON_CANCEL_SIMULATE_LEAK_PRESS = 15000;      // Any display
  const uint32_t SECOND_BUTTON_REBOOT_PRESS = 10000;                    // Any display
  const uint32_t SECOND_BUTTON_CONNECT_OTA_PRESS = 5000;                // Any display except map

  const uint32_t SECOND_BUTTON_TOGGLE_MAP_FEATURES_PRESS = 1000;        // Map display only
  const uint32_t SECOND_BUTTON_CYCLE_MAP_ZOOM_LEVEL_PRESS = 100;        // Map display only 

  // Check for 20-second press to simulate leak (TEST MODE)
  if (p_primaryButton->wasReleasefor(PRIMARY_BUTTON_SIMULATE_LEAK_PRESS) && !simulatedLeakActive)
  {
    simulatedLeakActive = true;
    USB_SERIAL_PRINTLN("*** LEAK SIMULATION ACTIVATED ***");
    changeMade = true;
  }
  // press second button for 5 seconds turn on ESP Now if it is currently off
  else if (p_primaryButton->wasReleasefor(PRIMARY_BUTTON_ESPNOW_ON_PRESS))
  {
    if (!ESPNowActive)
    {
      if (!otaActive)
        configAndStartUpESPNow();
      else 
        USB_SERIAL_PRINTLN("Cannot enable ESP Now when OTA is active");
    }
    else
    {
      USB_SERIAL_PRINTLN("ESP Now already enabled");
    }
  }

  // Normal button press for display cycling (but only if not showing indicators)
  else if (p_primaryButton->wasReleasefor(PRIMARY_BUTTON_CYCLE_DISPLAY_PRESS) && !primaryButtonIndicatorNeedsClearing) // show next display
  {
    activationTime = lastPrimaryButtonPressLasted;
    reedSwitchTop = true;
    changeMade = true;

    USB_SERIAL_PRINTLN("Cycle To Next Display");
    cycleDisplays();
  }

  // press second button for 15 seconds to reset leak simulation (TEST MODE)
  if (p_secondButton->wasReleasefor(SECOND_BUTTON_CANCEL_SIMULATE_LEAK_PRESS) && (simulatedLeakActive || leakAlarmActive))
  {
    simulatedLeakActive = false;
    leakAlarmActive = false;
    leakAlarmCurrentlyShowing = false;
    M5.Beep.mute();
    hideLeakAlarm();
    USB_SERIAL_PRINTLN("*** LEAK SIMULATION RESET ***");
    changeMade = true;
  }
  // press second button for 10 seconds reboot
  else if (p_secondButton->wasReleasefor(SECOND_BUTTON_REBOOT_PRESS))
  { 
    USB_SERIAL_PRINTLN("Reboot");
    esp_restart();
  }
  // press second button for 5 seconds to attempt WiFi connect and enable OTA
  else if (p_secondButton->wasReleasefor(SECOND_BUTTON_CONNECT_OTA_PRESS))
  { 
    activationTime = lastSecondButtonPressLasted;
    reedSwitchTop = false;

    TeardownESPNow();
    isPairedWithMako = false;

    dumpHeapUsage("checkReedSwitches(): begin switch to OTA");

    // enable OTA
    const bool wifiOnly = false;
    M5.Lcd.fillScreen(TFT_BLACK);
    const int maxWifiScanAttempts = 3;
    connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts,"Enable\nOTA Mode\n");

    changeMade = true;
    const bool refreshCurrentScreen=true;
    cycleDisplays(refreshCurrentScreen);

    USB_SERIAL_PRINTLN("Enable OTA Mode");
  }
  // press second button for 1 second to toggle all features on the map
  else if (p_secondButton->wasReleasefor(SECOND_BUTTON_TOGGLE_MAP_FEATURES_PRESS))
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
  else if (p_secondButton->wasReleasefor(SECOND_BUTTON_CYCLE_MAP_ZOOM_LEVEL_PRESS))
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

/////////////// EVENT LOOP
void loop()
{
  //////// PROTECTED - DO NOT ADD CODE BEFORE OR WITHIN THE OTA DEMAND CHECK BELOW  - RISK OF OTA FAILURE
  if (cutShortLoopOnOTADemand())
    return;
  ///////////////////////////////////////////////////////////////////////////////////////

  // Handle OTA shutdown request from WebSerial command
  if (otaShutdownRequested) {
    handleOTAShutdown();
    return; // Exit loop during shutdown
  }

  processIncomingESPNowMessages();

  // Update display every 100ms asynchronously (but not during leak alarm flash sequence)
  if (millis() - lastDisplayUpdateTime >= DISPLAY_UPDATE_INTERVAL && !leakAlarmInInitialFlash)
  {
    lastDisplayUpdateTime = millis();
    drawDisplay();
  }
  
  checkForLeak(leakAlarmMsg);
  
  checkReedSwitches();

  shutdownIfUSBPowerOff();
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
      USB_SERIAL_PRINTLN("Primary button pressed - starting timer");
    }
  }
  else
  {
    if (primaryButtonIsPressed)
    {
      lastPrimaryButtonPressLasted = millis() - primaryButtonPressedTime;
      primaryButtonIsPressed=false;
      primaryButtonPressedTime=0;
      USB_SERIAL_PRINTF("Primary button released after %i ms\n", lastPrimaryButtonPressLasted);
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

/////////////// UTILITY FUNCTIONS
void shutdownIfUSBPowerOff()
{
  if (M5.Axp.GetVBusVoltage() < minimumUSBVoltage)
  {
    if (USBVoltageDropTime == 0)
      USBVoltageDropTime=millis();
    else 
    {
      if (millis() > USBVoltageDropTime + milliSecondsToWaitForShutDown)
      {
         M5.Axp.PowerOff();
      }
    }
  }
  else
  {
    if (USBVoltageDropTime != 0)
      USBVoltageDropTime = 0;
  }
}

#define BUILD_INCLUDE_MAIN_DISPLAY_CODE
#include "main_display_code.cpp"

#define BUILD_INCLUDE_MAIN_NETWORK_CODE
#include "main_network_code.cpp"

#define BUILD_INCLUDE_MAIN_TIME_CODE
#include "main_time_code.cpp"

#define BUILD_INCLUDE_MAIN_ESPNOW_MESSAGE_CODE
#include "main_espnow_message_code.cpp"

#define BUILD_INCLUDE_MAIN_ESPNOW_NETWORK_CODE
#include "main_espnow_network_code.cpp"

#define BUILD_INCLUDE_MAIN_TEST_CODE
#include "main_test_code.cpp"
