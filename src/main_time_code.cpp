#ifdef BUILD_INCLUDE_MAIN_TIME_CODE

void  initialiseRTCfromNTP()
{
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0,0);

  const bool wifiOnly = true;

  const int maxWifiScanAttempts = 2;  
  if (WiFi.status() == WL_CONNECTED || connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts,"Get NTP\nTime...\n"))
  {
    M5.Lcd.println("Wifi OK");
    
    // Detect timezone from IP if not already set
    if (!timezoneSetFromIP) 
    {
      if (detectTimezoneFromIP(detectedTimezoneOffset))
        USB_SERIAL_PRINTF("initialiseFromNTP: calling updateRTCFromNTP detectedTimezoneOffset=%ld\n",detectedTimezoneOffset);
      else
        USB_SERIAL_PRINTLN("initialiseFromNTP: timezone not detected from IP");

      updateRTCFromNTP("initialiseRTCfromNTP",detectedTimezoneOffset,0);
    }
  }

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(0);

  resetClock();
}

bool detectTimezoneFromIP(long& timezoneOffset)
{
  bool result = false;

  if (WiFi.status() != WL_CONNECTED) {
    timezoneOffset = 0;
    return result;
  }

  M5.Lcd.println("Get\nTimezone...\n");
  
  // Use ip-api.com for IP-based geolocation (free, no API key needed)
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, "http://ip-api.com/json/?fields=timezone,offset");
  http.setTimeout(10000); // 10 second timeout
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    USB_SERIAL_PRINTF("IP Timezone API response: %s\n", payload.c_str());
    
    // Parse JSON response manually (simple parsing)
    int timezoneStart = payload.indexOf("\"timezone\":\"") + 12;
    int timezoneEnd = payload.indexOf("\"", timezoneStart);
    String timezone = payload.substring(timezoneStart, timezoneEnd);
    
    int offsetStart = payload.indexOf("\"offset\":") + 9;
    int offsetEnd = payload.indexOf(",", offsetStart);
    if (offsetEnd == -1) offsetEnd = payload.indexOf("}", offsetStart);
    int offsetSeconds = payload.substring(offsetStart, offsetEnd).toInt();
      
    USB_SERIAL_PRINTF("Detected timezone: %s, offset: %d seconds (includes DST)\n", timezone.c_str(), offsetSeconds);
    
    // The offset from ip-api already includes DST, so use it directly
    timezoneOffset = offsetSeconds;
    timezoneSetFromIP = true;
    
    M5.Lcd.printf("TZ: %s\n", timezone.c_str());
    M5.Lcd.printf("UTC%+d hours\n", offsetSeconds/3600);
    result = true;
  } else {
    USB_SERIAL_PRINTF("IP Timezone API failed: %d\n", httpCode);
    M5.Lcd.println("TZ detect failed");
  }
  
  http.end();
  return result;
}

// This isn't practical at present as WiFi connection closed after initial NTP server retrieval.
// Update from GPS location will need to have a timezone and dst offset looked-up by Lemon, sent to Mako and forwarded to Tiger.
// additional timezone offset seconds needs to be sent in GPS message that is sent from Lemon to Mako.
// then this needs sending over ESPNow to Tiger.
// Lemon will then make all the NTP, IP-API and timezone db calls so Tiger has no need to connect to NTP on startup.
bool detectTimezoneFromGPS(double lat, double lon)
{
  if (WiFi.status() != WL_CONNECTED || timezoneVerifiedFromGPS) {
    return false;
  }

  USB_SERIAL_PRINTF("GPS Timezone check: lat=%.6f, lon=%.6f\n", lat, lon);
  
  // Use TimeZoneDB API (free tier available)
  WiFiClient client;
  HTTPClient http;
  
  char url[200];
  snprintf(url, sizeof(url), 
    "http://api.timezonedb.com/v2.1/get-time-zone?key=%s&format=json&by=position&lat=%.6f&lng=%.6f", 
    timezonedb_api_key, lat, lon);
  
  http.begin(client, url);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    USB_SERIAL_PRINTF("GPS Timezone API response: %s\n", payload.c_str());
    
    // Parse offset from JSON
    int offsetStart = payload.indexOf("\"gmtOffset\":") + 12;
    int offsetEnd = payload.indexOf(",", offsetStart);
    if (offsetEnd == -1) offsetEnd = payload.indexOf("}", offsetStart);
    int offsetSeconds = payload.substring(offsetStart, offsetEnd).toInt();
    
    // Check if timezone changed from IP detection
    if (abs(offsetSeconds - detectedTimezoneOffset) > 1800) { // 30 minutes difference
      USB_SERIAL_PRINTF("GPS timezone differs from IP: %d vs %d\n", offsetSeconds, detectedTimezoneOffset);
      detectedTimezoneOffset = offsetSeconds;
      // NEEDS EXTRA CODE HERE TO APPLY THE DIFFERENT OFFSET
    }
    
    timezoneVerifiedFromGPS = true;
    http.end();
    return true;
  } else {
    USB_SERIAL_PRINTF("GPS Timezone API failed: %d\n", httpCode);
  }
  
  http.end();
  return false;
}

bool updateRTCFromNTP(const char* context,long timezoneOffset, int dstOffset)
{
  bool ntpSuccess = false;
  
  const uint8_t max_NTP_connect_attempts=10;

  for (uint8_t i=0; i<max_NTP_connect_attempts; i++)
  {
    struct tm timeinfo;
    configTime(timezoneOffset, dstOffset, ntpServer);
    
    if (getLocalTime(&timeinfo)) {
      USB_SERIAL_PRINTF("%s: RTC being updated with corrected time\n", context);

      // Update RTC with corrected time
      RTC_TimeTypeDef TimeStruct;
      TimeStruct.Hours = timeinfo.tm_hour;
      TimeStruct.Minutes = timeinfo.tm_min;
      TimeStruct.Seconds = timeinfo.tm_sec;
      M5.Rtc.SetTime(&TimeStruct);
      
      RTC_DateTypeDef DateStruct;
      DateStruct.Month = timeinfo.tm_mon + 1;
      DateStruct.Date = timeinfo.tm_mday;
      DateStruct.Year = timeinfo.tm_year + 1900;
      DateStruct.WeekDay = timeinfo.tm_wday;
      M5.Rtc.SetDate(&DateStruct);
      
      USB_SERIAL_PRINTF("%s: RTC updated with timezone-corrected time\n", context);
      ntpSuccess = true;
      break;
    }
    else
    {
      USB_SERIAL_PRINTLN("No time available (yet)");
      // Let RTC continue with existing settings
      M5.Lcd.println("Wait for NTP Time\n");
      delay(500);
    }
  }

  if (ntpSuccess)
  {
    USB_SERIAL_PRINTLN("NTP time received");
    M5.Lcd.printf("RTC set (UTC%+d)\n", ((int)(timezoneOffset + int(dstOffset))/3600));
    delay(300);
  }
  else
  {      
    // Let RTC continue with existing settings
    M5.Lcd.println("No NTP Server\n");
  }

  return ntpSuccess;
}

void getTime(char* time)
{    // Clock mode - Hours, mins, secs with optional date
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  int h = int(RTC_TimeStruct.Hours);
  int m = int(RTC_TimeStruct.Minutes);
  snprintf(time,sizeof(currentTime),"%02d:%02d",h,m);
}

#endif