#ifdef BUILD_INCLUDE_MAIN_BUTTON_PRESS_CODE


void setPrimaryControls(const bool useReedSwitches)
{
  if (useReedSwitches)
  {
    p_primaryButton = &ReedSwitchGoProTop;
    p_secondButton = &ReedSwitchGoProSide;
  }
  else
  {
    p_primaryButton = &M5.BtnA;
    p_secondButton = &M5.BtnB;
  }
}

bool checkForDualButtonPresses()
{
  static bool action500msReached = false;
  static bool action3000msReached = false;
  static bool action8000msReached = false;
  static bool anyActionTriggered = false;
  static uint32_t lastActionTime = 0;
  
  bool triggered = false;
  
  // Prevent any action within 10 seconds of the last action
  if (millis() - lastActionTime < 10000)
  {
    return false;
  }
  
  // Check if both buttons are currently pressed
  bool bothPressed = p_primaryButton->isPressed() && p_primaryButton->isPressed();
  
  if (!bothPressed)
  {
    // Buttons released - check which action to trigger based on how long they were held
    if (!anyActionTriggered && (action500msReached || action3000msReached || action8000msReached))
    {
      anyActionTriggered = true;  // Set this first to prevent re-entry
      lastActionTime = millis();  // Record time of action execution
      
      if (action8000msReached)
      {
        // 8 second hold completed
        triggered = true;
      }
      else if (action3000msReached)
      {
        // 3 second hold completed
        triggered = true;
      }
      else if (action500msReached)
      {
        // 0.5 second hold completed - OTA mode
        static uint32_t lastOTAToggle = 0;
        if (millis() - lastOTAToggle > 5000)  // Prevent rapid OTA toggles
        {
          lastOTAToggle = millis();
          // enable OTA
          const bool wifiOnly = false;
          M5.Lcd.fillScreen(TFT_BLACK);
          const int maxWifiScanAttempts = 3;
          connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts,"Enable\nOTA Mode\n");

          delay (2000);

          const bool refreshCurrentScreen=true;
          cycleDisplays(refreshCurrentScreen);
          triggered = true;
        }
      }
    }
    
    // Only reset flags if both buttons are completely released
    if (p_primaryButton->isReleased() && p_secondButton->isReleased())
    {
      action500msReached = false;
      action3000msReached = false;
      action8000msReached = false;
      anyActionTriggered = false;
    }
    
    return triggered;
  }
  
  // Buttons are pressed - track which thresholds have been reached
  // Only set the highest threshold reached to ensure mutual exclusivity
  if (p_primaryButton->pressedFor(8000) && p_secondButton->pressedFor(8000))
  {
    action8000msReached = true;
    action3000msReached = false;  // Clear lower thresholds
    action500msReached = false;
  }
  else if (p_primaryButton->pressedFor(3000) && p_secondButton->pressedFor(3000))
  {
    action3000msReached = true;
    action500msReached = false;   // Clear lower threshold
  }
  else if (p_primaryButton->pressedFor(500) && p_secondButton->pressedFor(500))
  {
    action500msReached = true;
  }

  return false; // No action triggered while buttons are still pressed
}

bool checkReedSwitches()
{  
  bool changeMade = false;

  bool reedSwitchTop;
  uint32_t activationTime=0;
    
  updateButtonsAndBuzzer();

  if (checkForDualButtonPresses())
    return true;

  displayReedActivationIndicators();

  // Check for 20-second press to simulate leak (TEST MODE)  
  const uint32_t UPPER_REED_SIMULATE_LEAK_ACTIVATION = 20000;             // Any display - TOP-RIGHT - 20s simulate leak
  const uint32_t UPPER_REED_ESPNOW_ON_ACTIVATION = 5000;                  // Any display - TOP-RIGHT - 5s  enable ESP Now if off
  const uint32_t UPPER_REED_CYCLE_DISPLAY_ACTIVATION = 100;               // Any display - TOP-RIGHT - tap to cycle the display

  const uint32_t LOWER_REED_CANCEL_SIMULATE_LEAK_ACTIVATION = 15000;      // Any display - BOT-LEFT - 15s cancel leak simulation
  const uint32_t LOWER_REED_REBOOT_ACTIVATION = 10000;                    // Any display - BOT-LEFT - 10s reboot
  const uint32_t LOWER_REED_CONNECT_OTA_ACTIVATION = 5000;                // Any display - BOT-LEFT - 5s  enable OTA server

  const uint32_t LOWER_REED_TOGGLE_MAP_FEATURES_ACTIVATION = 1000;        // Map only    - BOT-LEFT - Toggle show all features
  const uint32_t LOWER_REED_CYCLE_MAP_ZOOM_LEVEL_ACTIVATION = 100;        // Map only    - BOT-LEFT - Cycle map zoom level

  if (!reedSwitchesPrimaryControl && (isTopReedClosed() || isSideReedClosed()))
  {
    delay(500);                                       // small delay to let the reed open again
    forceReedSwitchesPrimaryControl();                // get out of Jail, make reed switches primary control and not M5 buttons
    publishToMakoForceGoProButtonsPrimaryControl();   // also tell Mako to use Go Pro Buttons and not M5 buttons - in case he is in Jail!
    
    changeMade = true;
    return changeMade;
  }

  // Check for 20-second press to simulate leak (TEST MODE)
  if (p_primaryButton->wasReleasefor(UPPER_REED_SIMULATE_LEAK_ACTIVATION) && !simulatedLeakActive)
  {
    simulatedLeakActive = true;
    USB_SERIAL_PRINTLN("*** LEAK SIMULATION ACTIVATED ***");
    changeMade = true;
  }
  // press second button for 5 seconds turn on ESP Now if it is currently off
  else if (p_primaryButton->wasReleasefor(UPPER_REED_ESPNOW_ON_ACTIVATION))
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
  else if (p_primaryButton->wasReleasefor(UPPER_REED_CYCLE_DISPLAY_ACTIVATION) && !primaryButtonIndicatorNeedsClearing) // show next display
  {
    activationTime = lastPrimaryButtonPressLasted;
    reedSwitchTop = true;
    changeMade = true;

    USB_SERIAL_PRINTLN("Cycle To Next Display");
    cycleDisplays();
  }

  // press second button for 15 seconds to reset leak simulation (TEST MODE)
  if (p_secondButton->wasReleasefor(LOWER_REED_CANCEL_SIMULATE_LEAK_ACTIVATION) && (simulatedLeakActive || leakAlarmActive))
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
  else if (p_secondButton->wasReleasefor(LOWER_REED_REBOOT_ACTIVATION))
  { 
    USB_SERIAL_PRINTLN("Reboot");
    esp_restart();
  }
  // press second button for 5 seconds to attempt WiFi connect and enable OTA
  else if (p_secondButton->wasReleasefor(LOWER_REED_CONNECT_OTA_ACTIVATION))
  { 
    activationTime = lastSecondButtonPressLasted;
    reedSwitchTop = false;

    TeardownESPNow();
    isPairedWithMako = false;

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
  else if (p_secondButton->wasReleasefor(LOWER_REED_TOGGLE_MAP_FEATURES_ACTIVATION))
  {
    activationTime = lastSecondButtonPressLasted;
    reedSwitchTop = false;

    if (display_mode == DISPLAY_MAP)    // toggle showing all features on the map
    {
      mapScreen->toggleDrawAllFeatures();
      mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);
      changeMade = true;
    }
    USB_SERIAL_PRINTLN("Toggle show all map features");
  }
  // tap second button for 0.1 second to change zoom level of map
  else if (p_secondButton->wasReleasefor(LOWER_REED_CYCLE_MAP_ZOOM_LEVEL_ACTIVATION))
  {
    activationTime = lastSecondButtonPressLasted;
    reedSwitchTop = false;

    if (display_mode == DISPLAY_MAP) // map mode - cycle zoom
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

void forceReedSwitchesPrimaryControl()
{
  // get out of jail...
  // If the code is uploaded to Tiger in the pod with reedSwitchesPrimaryControl set to true (ie from being tested on the bench outside the pod, or
  // another M5 stick as a test device), then the reeds don't work and there is no way to force an OTA to get this corrected.
  // This is a special case where closing either reed switch when in ButtonsPrimary mode switches to the reed switches as primary
  // so that the code can be re-uploaded with the primaries set back to the reeds.
  // Without this you have to open the GoPro case and physically upload code to Tiger using USB-C. Not nice as the pod needs dismantling to do this!
  reedSwitchesPrimaryControl = true;
  setPrimaryControls(reedSwitchesPrimaryControl); 
  display_mode = DISPLAY_POD_CONTROLS_ENABLED;
}
  



#endif