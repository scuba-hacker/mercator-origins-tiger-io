#ifdef BUILD_INCLUDE_MAIN_DISPLAY_CODE

// External reference to cached AXP temperature
extern float cachedAXPTemperature;

// External reference to leak alarm state
extern bool leakAlarmCurrentlyShowing;


void checkForLeak(const char* msg)
{
  // Handle asynchronous leak alarm flash sequence with dual-tone pattern
  if (leakAlarmInInitialFlash && millis() - leakAlarmFlashTime >= 100) // Flash every 100ms for dual tone timing
  {
    leakAlarmFlashTime = millis();
    
    if (leakAlarmFlashCycle < MAX_ALARM_BURST_CYCLES) // Still flashing
    {
      if (leakAlarmFlashOn) // Currently showing red, switch to orange
      {
        M5.Lcd.fillScreen(TFT_ORANGE);
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(5, 10);
        M5.Lcd.setTextColor(TFT_YELLOW, TFT_ORANGE);
        M5.Lcd.print(leakAlarmMsg);
        M5.Beep.setBeep(LEAK_ALARM_TONE_2_FREQ, LEAK_ALARM_TONE_2_DURATION);
        M5.Beep.beep();
        leakAlarmFlashOn = false;
      }
      else // Currently showing orange, switch to red and increment cycle
      {
        M5.Lcd.fillScreen(TFT_RED);
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(5, 10);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
        M5.Lcd.print(leakAlarmMsg);
        M5.Beep.setBeep(LEAK_ALARM_TONE_1_FREQ, LEAK_ALARM_TONE_1_DURATION);
        M5.Beep.beep();
        leakAlarmFlashOn = true;
        leakAlarmFlashCycle++;
      }
    }
    else
    {
      // Flash sequence complete - hide alarm and set timing for next sequence
      leakAlarmInInitialFlash = false;
      leakAlarmCurrentlyShowing = false;
      M5.Lcd.fillScreen(TFT_BLACK);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Beep.mute();
      
      // Set timing for next alarm sequence (2 minutes from now)
      leakAlarmLastShowTime = millis();
      leakAlarmShowCount = 0;
    }
  }

  // Check for new leak detection (real or simulated)
  if ((isLeakDetected() || simulatedLeakActive) && !leakAlarmActive)
  {
    // First time leak detected - activate alarm permanently
    leakAlarmActive = true;
    leakAlarmStartTime = millis();
    leakAlarmCurrentlyShowing = true;
    leakAlarmShowStartTime = millis();
    leakAlarmLastShowTime = millis();
    leakAlarmShowCount = 0;
    
    initialiseLeakAlarm(msg);
    publishToMakoLeakDetected();
    
    USB_SERIAL_PRINTLN("LEAK DETECTED - ALARM ACTIVATED PERMANENTLY");
  }
  
  // Handle ongoing alarm behavior (only if alarm is active)
  if (leakAlarmActive)
  {
    // Simple timing: 10-flash sequence every 2 minutes
    uint32_t timeSinceLastShow = millis() - leakAlarmLastShowTime;
    
    // Time to start a new 10-flash sequence?
    if (timeSinceLastShow >= DURATION_BETWEEN_ALARM_BURSTS && !leakAlarmCurrentlyShowing && !leakAlarmInInitialFlash) // 2 minutes
    {
      leakAlarmCurrentlyShowing = true;
      leakAlarmShowStartTime = millis();
      initialiseLeakAlarm(msg);
    }
  }
}

void initialiseLeakAlarm(const char* msg)
{
  leakAlarmInInitialFlash = true;
  leakAlarmFlashCycle = 0;
  leakAlarmFlashTime = millis();
  leakAlarmFlashOn = true;
}

void hideLeakAlarm()
{
  M5.Beep.mute();
  M5.Lcd.fillScreen(TFT_BLACK);  // Ensure screen is completely black
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
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

    dumpHeapUsage("cycleDisplays(): ");
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

void drawDisplay()
{
  // Don't draw normal display if leak alarm is currently showing
  if (leakAlarmCurrentlyShowing) {
    return;
  }
  
  // draw display
  if ( mode_ == 6) { drawMapDisplay();}
  if ( mode_ == 5) { drawCurrentTargetTempDisplay();}
  if ( mode_ == 3 ){ drawClockDisplay();}   // hh,mm,ss, optional dd mm
}

void drawMapDisplay()
{
  // do nothing
}

void drawCurrentTargetTempDisplay()
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

void drawDigits(int h1, int h2, int i1, int i2, int s1, int s2)
{
  drawDigitText(h1,h2,i1,i2,s1,s2);
}


void drawDigitText(int h1, int h2, int i1, int i2, int s1, int s2)
{
  M5.Lcd.setTextSize(9);
  M5.Lcd.setTextFont(0);
  M5.Lcd.setTextColor(TFT_ORANGE,TFT_BLACK);

  // Use fixed positions to avoid cursor calculation issues
  // Hours (2 digits)
  M5.Lcd.setCursor(5, 5);
  M5.Lcd.printf("%d%d", h1, h2);
  
  // Colon
  M5.Lcd.setCursor(85, 5);  // Fixed position for colon
  M5.Lcd.printf(":");
  
  // Minutes (2 digits)
  M5.Lcd.setCursor(105, 5);  // Fixed position for minutes
  M5.Lcd.printf("%d%d", i1, i2);

  // Seconds (smaller text, if provided)
  if (s1 != -1 && s2 != -1)
  {
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(50, 120);
    M5.Lcd.printf("%d%d", s1, s2);
  }
}

void drawClockDisplay()
{    // Clock mode - Hours, mins, secs with optional date
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  int h1 = int(RTC_TimeStruct.Hours / 10 );
  int h2 = int(RTC_TimeStruct.Hours - h1*10 );
  int i1 = int(RTC_TimeStruct.Minutes / 10 );
  int i2 = int(RTC_TimeStruct.Minutes - i1*10 );
  int s1 = int(RTC_TimeStruct.Seconds / 10 );
  int s2 = int(RTC_TimeStruct.Seconds - s1*10 );

  drawDigits(h1, h2, i1, i2, s1, s2);

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
  
  // Position temperature to the left of seconds, aligned with first digit of minutes
  M5.Lcd.setCursor(5, 130);  // x=5 (left of seconds at x=50), y=120 (same as seconds)
  M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
  M5.Lcd.printf("%.0fC", cachedAXPTemperature);
}

  void displayReedActivationIndicators()
  {
    int pressedPrimaryButtonX, pressedPrimaryButtonY, pressedSecondButtonX, pressedSecondButtonY;

    pressedPrimaryButtonX = 110;
    pressedPrimaryButtonY = 105; 

    pressedSecondButtonX = 5;
    pressedSecondButtonY = 210;
      
    // Update button indicators at the same rate as display (100ms) to prevent overwriting
    if (millis() - lastButtonIndicatorUpdateTime >= DISPLAY_UPDATE_INTERVAL && !leakAlarmInInitialFlash && !leakAlarmCurrentlyShowing)
    {
      lastButtonIndicatorUpdateTime = millis();
      
      // Primary button indicator
      if (primaryButtonIsPressed && millis()-primaryButtonPressedTime > 250)
      {
        int seconds = (millis()-primaryButtonPressedTime)/1000;
        int xPos = pressedPrimaryButtonX;
        
        // Move left for double digits to prevent wrapping
        if (seconds >= 10) {
          xPos -= 18; // Adjust for character width at size 3
        }
        
        M5.Lcd.setTextSize(3);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
        M5.Lcd.setCursor(xPos, pressedPrimaryButtonY);
        M5.Lcd.printf("%i", seconds);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        primaryButtonIndicatorNeedsClearing=true;
        USB_SERIAL_PRINTF("Primary button indicator: %i seconds\n", seconds);
      }
      else
      {
        if (primaryButtonIndicatorNeedsClearing)
        {
          primaryButtonIndicatorNeedsClearing = false;
          // Clear both single and double digit positions with black rectangle
          M5.Lcd.fillRect(pressedPrimaryButtonX-18, pressedPrimaryButtonY, 54, 24, TFT_BLACK);
        }
      }

      // Second button indicator
      if (secondButtonIsPressed && millis()-secondButtonPressedTime > 250)
      {
        int seconds = (millis()-secondButtonPressedTime)/1000;
        int xPos = pressedSecondButtonX;
        
        // Move right for double digits since this is on the left side
        if (seconds >= 10) {
          xPos += 0; // Keep same position since we have room on the left side
        }
        
        M5.Lcd.setTextSize(3);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
        M5.Lcd.setCursor(xPos, pressedSecondButtonY);
        M5.Lcd.printf("%i", seconds);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        secondButtonIndicatorNeedsClearing=true;
      }
      else
      {
        if (secondButtonIndicatorNeedsClearing)
        {
          secondButtonIndicatorNeedsClearing = false;
          // Clear second button indicator with black rectangle
          M5.Lcd.fillRect(pressedSecondButtonX, pressedSecondButtonY, 36, 24, TFT_BLACK);
        }
      }
    }
  }

#endif