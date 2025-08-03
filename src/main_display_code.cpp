#ifdef BUILD_INCLUDE_MAIN_DISPLAY_CODE


void drawDisplay()
{
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
    
    M5.Lcd.setCursor(12, mode_label_y_offset+10);
    M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
    M5.Lcd.printf("AXP %.1fC",M5.Axp.GetTempInAXP192());
  }
}


#endif