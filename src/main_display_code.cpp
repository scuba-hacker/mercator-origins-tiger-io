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
  display_mode = DISPLAY_CURRENT_TARGET;
}

void resetMap()
{
  mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);
  display_mode = DISPLAY_MAP;
}

void resetClock()
{
  M5.Lcd.setRotation(0);
  M5.Lcd.fillScreen(BLACK);
  display_mode = DISPLAY_CLOCK;
}

bool cycleDisplays(bool refreshCurrentDisplay, e_display_modes setDisplayTo)
{
    bool changeMade = true;

    if (setDisplayTo != DISPLAY_UNDEFINED)
    {
      display_mode = setDisplayTo;
      refreshCurrentDisplay = true;
    }

    dumpHeapUsage("cycleDisplays(): ");
    if (refreshCurrentDisplay)
    {
      if (display_mode == DISPLAY_CLOCK)
        resetClock();
      else if (display_mode == DISPLAY_CURRENT_TARGET)
        resetCurrentTarget();
      else if (display_mode == DISPLAY_MAP)
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
      if (display_mode == DISPLAY_CLOCK) // clock mode, next is show current target
        resetCurrentTarget();
      else if (display_mode == DISPLAY_CURRENT_TARGET)     // show current target, next is map
      {
        if (mapScreen.get())    // OTA enabling has to delete the map screen
          resetMap();
        else
          resetClock();   // OTA enabled, go back to clock
      }
      else if (display_mode == DISPLAY_MAP)     // show map, next is clock
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
  if ( display_mode == DISPLAY_MAP)                   { drawMapDisplay();}
  if ( display_mode == DISPLAY_CURRENT_TARGET)        { drawCurrentTargetDisplay();}
  if ( display_mode == DISPLAY_POD_CONTROLS_ENABLED)  { drawPodControlsEnabledDisplay();}
  if ( display_mode == DISPLAY_CLOCK)                 { drawClockDisplay();}
}

void drawMapDisplay()
{
  // do nothing
}

void drawPodControlsEnabledDisplay()
{
   M5.Lcd.fillScreen(TFT_BLACK);
   M5.Lcd.setCursor(0,0);
   M5.Lcd.setTextSize(3);
   M5.Lcd.println("Pod\n\Control\nEnabled\n");
   // synchronous delay ok here - this is a development tool only - in case gets uploaded that makes M5 buttons primary
   // then switch to clock
   delay(5000);
   M5.Lcd.fillScreen(TFT_BLACK);

   display_mode = DISPLAY_CLOCK;
   drawDisplay();
}

uint16_t drawMultiString(const char* source, const uint16_t xpos, uint16_t ypos, const uint16_t lineHeight)
{
  char line[32];
  const int maxLines = 10;
  int lines = 0;

  uint16_t originalYpos = ypos;

  char* draw = line;
  line[0]='\0';

  const char* next=source; 
  while (*next && lines < maxLines)
  {
    while (*next && *next != '\n')
      *draw++ = *next++;

    if (*next == '\n')
      next++;

    if (draw      != line &&    // must not be empty string
        *(draw-1) == '\n')      // if previous char a newline, then substitutes a null terminator in place of the newline
        *(draw-1) = '\0';
    else
      *draw = '\0';             // otherwise appends null terminator.

    if (*line != '\0')
    {
      M5.Lcd.drawString(line,xpos,ypos);
      ypos += lineHeight;
    }
    draw = line;
    lines++;
  }

  return ypos - originalYpos;
}

void drawCurrentTargetDisplay()
{
  if (refreshTargetShown)
  {
    M5.Lcd.fillScreen(TFT_BLACK);
    refreshTargetShown = false;
  }
  bool targetLive = false;

  const uint8_t textSize=1;
  const uint8_t textFont=4;

  uint8_t oldFont = M5.Lcd.textfont;
  uint8_t oldSize = M5.Lcd.textsize;

  M5.Lcd.setTextSize(textSize);
  M5.Lcd.setTextFont(textFont);
  uint16_t ypos = 0, centre = M5.Lcd.width() / 2;

  M5.Lcd.setTextDatum(TC_DATUM);

  uint16_t lineHeight = 28;

  if (ESPNowActive && isPairedWithMako)
  {    
    if (targetValid)
    {
      M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
      M5.Lcd.drawString("Towards",centre,ypos);
      ypos+=lineHeight;
    }

    M5.Lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
    ypos += drawMultiString(currentTarget, centre, ypos, lineHeight);
  }
  else
  {
    const char* noTarget = "No\nTarget\n \nESP-Now\n";
    const char* noPair = "No\nPair";
    const char* isOff = "Is\nOff";
   
    M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
    ypos += drawMultiString(noTarget, centre, ypos, lineHeight);

    if (ESPNowActive)
      ypos += drawMultiString(noPair, centre, ypos, lineHeight);
    else
      ypos += drawMultiString(isOff, centre, ypos, lineHeight);
  }

  ypos = TFT_HEIGHT;
  M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5.Lcd.setTextDatum(BC_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextFont(6);
  getTime(currentTime);
  ypos+=M5.Lcd.drawString(currentTime,centre,ypos);
  
  M5.Lcd.setTextDatum(BL_DATUM);
  M5.Lcd.setTextSize(oldSize);
  M5.Lcd.setTextFont(oldFont);
}

uint16_t drawSmoothDigits(int h1, int h2, int i1, int i2, int s1, int s2)
{
  int16_t xpos = 0, ypos = 0;

  char digits[4];

  const uint8_t hoursMinsTextSize=2;
  const uint8_t hoursMinsTextFont=6;

  clockSprite->fillSprite(TFT_BLACK);
  clockSprite->setTextSize(hoursMinsTextSize);
  clockSprite->setTextColor(TFT_ORANGE);

  snprintf(digits,sizeof(digits),"%02d:",h1*10+h2);
  clockSprite->drawString(digits, xpos,ypos,hoursMinsTextFont);
  ypos += clockSprite->fontHeight(hoursMinsTextFont)-20;

  snprintf(digits,sizeof(digits),"%02d",i1*10+i2);
  clockSprite->drawString(digits, xpos,ypos,hoursMinsTextFont);
  ypos += clockSprite->fontHeight(hoursMinsTextFont)-28;

  if (s1 != -1 && s2 != -1)
  {
    const uint8_t secondsTextSize=2;
    const uint8_t secondsTextFont=2;

    clockSprite->setTextSize(secondsTextSize);
    clockSprite->setTextDatum(TL_DATUM);
    xpos = 100;
    snprintf(digits,sizeof(digits),"%02d",s1*10+s2);
    clockSprite->drawString(digits, xpos, ypos,secondsTextFont);
  }

  clockSprite->pushSprite(0,0);
  
  ypos = clockSprite->height()+30;

  return ypos;
}

uint16_t drawBlockDigits(int h1, int h2, int i1, int i2, int s1, int s2)
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
  return M5.Lcd.getCursorY() + M5.Lcd.fontHeight()*4;
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

  uint16_t ypos = drawSmoothDigits(h1, h2, i1, i2, s1, s2);

  M5.Lcd.setTextSize(2);
  
  char espLabel[32];

  if (otaActive)
  {
    M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextDatum(TC_DATUM);
    ypos+=M5.Lcd.drawString("OTA On",M5.Lcd.width()/2,ypos,4);
    ypos+=M5.Lcd.drawString(WiFi.localIP().toString().c_str(),M5.Lcd.width()/2,ypos,2);
  }
  else if (ESPNowActive)
  {
    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);    
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextDatum(BC_DATUM);
    ypos+=M5.Lcd.fontHeight(2)*2;

    const bool overrideESPLabel = false;
    if (overrideESPLabel)
    {
      multi_heap_info_t info;
      heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
      snprintf(espLabel,sizeof(espLabel),"fr: %i",info.total_free_bytes);
    }
    else
    {
      if (isPairedWithMako)
      {
        if (!pingReceivedFromMako)
          snprintf(espLabel,sizeof(espLabel),"ESP+ %i",attemptSendPingResponseToMako);
        else if (pingReceivedFromMako % 2)
          snprintf(espLabel,sizeof(espLabel),"ESP/ %i",attemptSendPingResponseToMako);
        else
          snprintf(espLabel,sizeof(espLabel),"ESP\\ %i",attemptSendPingResponseToMako);
      }
      else
      {
        M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
        snprintf(espLabel,sizeof(espLabel),"ESP-      ");
      }
    }
   
    M5.Lcd.drawString(espLabel,M5.Lcd.width()/2,ypos,4);
  }
  M5.Lcd.setTextDatum(TL_DATUM);

  /*
  // Update AXP temperature every 1 second asynchronously
  if (millis() - lastAXPTempUpdateTime >= AXP_TEMP_UPDATE_INTERVAL)
  {
    lastAXPTempUpdateTime = millis();
    cachedAXPTemperature = M5.Axp.GetTempInAXP192();
  }
  
  // Position temperature to the left of seconds, aligned with first digit of minutes
  M5.Lcd.setCursor(5, 130);  // x=5 (left of seconds at x=50), y=120 (same as seconds)
  M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
  M5.Lcd.printf("%.0fC", cachedAXPTemperature);
  */
}

void displayReedActivationIndicators()
{
  const int minimumActivationTimeBeforeIndication = 500;
  int pressedPrimaryButtonX, pressedPrimaryButtonY, pressedSecondButtonX, pressedSecondButtonY;

  pressedPrimaryButtonX = 110;
  pressedPrimaryButtonY = 210; 

  pressedSecondButtonX = 5;
  pressedSecondButtonY = 210;
    
  // Update button indicators at the same rate as display (100ms) to prevent overwriting
  if (millis() - lastButtonIndicatorUpdateTime >= getDisplayUpdateInterval() && !leakAlarmInInitialFlash && !leakAlarmCurrentlyShowing)
  {
    lastButtonIndicatorUpdateTime = millis();
    
    // Primary button indicator
    if (primaryButtonIsPressed && millis()-primaryButtonPressedTime > minimumActivationTimeBeforeIndication)
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
    if (secondButtonIsPressed && millis()-secondButtonPressedTime > minimumActivationTimeBeforeIndication)
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