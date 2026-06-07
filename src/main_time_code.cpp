#ifdef BUILD_INCLUDE_MAIN_TIME_CODE

void saveLastTimezoneOffset(long offset) {
  persistedPreferences.putLong("tz_offset", offset);
}

long loadLastTimezoneOffset() {
  latestTimezoneOffset = persistedPreferences.getLong("tz_offset", 0);
  return latestTimezoneOffset;
}

void  initialiseRTCfromNTP()
{
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0,0);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setRotation(1);

  // NTP connects as a STA to infrastructure WiFi, which moves the radio home
  // channel to the router's channel. ESP-NOW peers must be rebuilt afterward
  // so their registered channel matches the restored ESP-NOW home channel.
  const bool restartESPNowAfterNTP = enableESPNow && ESPNowActive && espNOW_msgsReceivedQueue;
  if (restartESPNowAfterNTP)
  {
    BUFFER_LOG_PRINTLN("Stopping ESP-NOW for NTP WiFi connection");
    TeardownESPNow();
  }

  const bool wifiOnly = true;

  const int maxWifiScanAttempts = 8;
  if (WiFi.status() == WL_CONNECTED || connectToWiFiAndInitOTA(wifiOnly,maxWifiScanAttempts,"Get NTP Time..."))
  {
    M5.Lcd.println("NTP Wifi OK");
    BUFFER_LOG_PRINTLN("NTP Wifi OK");
    delay(250);

    if (hardcodeUKTimezone)
    {
      useLondonTimezoneOffset(detectedTimezoneOffset);
      // Second param is total timezone offset (raw_offset + dst_offset)
      // Third param is 0 (we don't use separate DST offset)
      updateRTCFromNTP("initialiseRTCfromNTP",detectedTimezoneOffset,0);
      delay(1000);
    }
    else
    {
      // Detect timezone from IP if not already set
      if (!timezoneSetFromIP) 
      {
        if (detectTimezoneFromIP(detectedTimezoneOffset))
          BUFFER_LOG_PRINTF("initialiseFromNTP: calling updateRTCFromNTP detectedTimezoneOffset=%ld\n",detectedTimezoneOffset);
        else
          BUFFER_LOG_PRINTLN("initialiseFromNTP: timezone not detected from IP");

        updateRTCFromNTP("initialiseRTCfromNTP",detectedTimezoneOffset,0);
      }
    }

    M5.Lcd.println("NTP Updated");
    BUFFER_LOG_PRINTLN("NTP Updated");
    delay(250);
  }
  else
  {
    M5.Lcd.println("NTP Wifi NOT OK");
    BUFFER_LOG_PRINTLN("NTP Wifi NOT OK");
    delay(1000);
  }

  // Fully drop the infrastructure STA connection before restoring ESP-NOW.
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(100);

  if (restartESPNowAfterNTP)
  {
    BUFFER_LOG_PRINTLN("Restarting ESP-NOW after NTP WiFi connection");
    configAndStartUpESPNow();
  }
}

bool useLondonTimezoneOffset(long& timezoneOffset)
{
  // accommodates for British Summer Time
  bool result = false;

  timezoneOffset = 0;

  if (WiFi.status() != WL_CONNECTED) 
  {
    timezoneOffset = loadLastTimezoneOffset();
    BUFFER_LOG_PRINTF("Wifi Not Connected - getting last timezoneoffset from flash: %d\n",timezoneOffset);
    return true;
  }

  WiFiClient client;
  HTTPClient httpLondonTZOffset;
  String payload;

  M5.Lcd.println("BST:");
  BUFFER_LOG_PRINTLN("BST:");

  char url[200];
  snprintf(url, sizeof(url), "http://api.timezonedb.com/v2.1/get-time-zone?key=%s&format=json&by=zone&zone=Europe/London", timezonedb_api_key);
  httpLondonTZOffset.begin(client, url);
  httpLondonTZOffset.setTimeout(10000); // 10 second timeout
  const int maxAttempts = 5;
  int attempts = maxAttempts;
  const uint32_t timeBetweenAttempts = 2000;

  while (attempts)
  {
    BUFFER_LOG_PRINTF("api.timezonedb.com: Get timezone offset, attempt %d\n",maxAttempts-attempts+1);
    int httpCode = httpLondonTZOffset.GET();

    if (httpCode == HTTP_CODE_OK)
    {
      payload = httpLondonTZOffset.getString();
      BUFFER_LOG_PRINTLN(payload.c_str());

      if (payload.length() > 0)
      {
        StaticJsonDocument<128> filter;
        filter["gmtOffset"] = true;

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
        if (!err)
        {
          timezoneOffset = (long)(doc["gmtOffset"] | 0);    // seconds (includes DST)
          M5.Lcd.printf("offset:%ld\n",timezoneOffset);
          BUFFER_LOG_PRINTF("offset:%ld ",timezoneOffset);

          saveLastTimezoneOffset(timezoneOffset);
          result = true;
          break;
        }
        else
        {
          M5.Lcd.printf("JSON Err: %s",err.c_str());
          BUFFER_LOG_PRINTF("JSON Err: %s",err.c_str());
        }
      }
      else
      {
        M5.Lcd.println("Empty JSON");
        BUFFER_LOG_PRINTLN("Empty JSON");
      }
    }
    else
    {
      M5.Lcd.printf("HTTP %d ",httpCode);
      BUFFER_LOG_PRINTF("HTTP %d ",httpCode);
    }
    attempts--;
    delay(timeBetweenAttempts);
  }

  httpLondonTZOffset.end();

  if (!result)
  {
    loadLastTimezoneOffset();
    result = true;
  }

  return result;
}

// Warning: Silky with Grasplet SIM has Sofia, Bulgaria external IP location
bool detectTimezoneFromIP(long& timezoneOffset)
{
  bool result = false;

  if (WiFi.status() != WL_CONNECTED) {
    timezoneOffset = 0;
    return result;
  }

  WiFiClient client;
  HTTPClient httpExtIP;
  String payload;

  M5.Lcd.println("Ext IP:");
  httpExtIP.begin(client, "http://api.ipify.org");
  httpExtIP.setTimeout(10000); // 10 second timeout
  
  int httpCode = httpExtIP.GET();

  if (httpCode == HTTP_CODE_OK) 
  {
    payload = httpExtIP.getString();
    if (payload.length() > 0)
    {
      M5.Lcd.printf("%s\n",payload.c_str());
      M5.Lcd.println("???");
    }
  }
  httpExtIP.end();

  HTTPClient httpTZ;

  M5.Lcd.println("TZ:\n");
  
  // Use ip-api.com for IP-based geolocation (free, no API key needed)  
  httpTZ.begin(client, "http://ip-api.com/json/?fields=timezone,offset");
  httpTZ.setTimeout(10000); // 10 second timeout
  
  httpCode = httpTZ.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = httpTZ.getString();
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
  
  httpTZ.end();
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

  USB_SERIAL_PRINTF("%s: configTime called with timezoneOffset=%ld, dstOffset=%d\n", context, timezoneOffset, dstOffset);
  BUFFER_LOG_PRINTF("%s: configTime called with timezoneOffset=%ld, dstOffset=%d\n", context, timezoneOffset, dstOffset);

  for (uint8_t i=0; i<max_NTP_connect_attempts; i++)
  {
    struct tm timeinfo;
    configTime(timezoneOffset, dstOffset, ntpServer);
    
    if (getLocalTime(&timeinfo)) {
      USB_SERIAL_PRINTF("%s: RTC being updated with corrected time\n", context);
      BUFFER_LOG_PRINTF("%s: RTC being updated with corrected time\n", context);

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
      
      USB_SERIAL_PRINTF("%s: RTC updated with timezone-corrected time %02d-%02d-%04d %02d:%02d:%02d\n", context, DateStruct.Date, DateStruct.Month, DateStruct.Year, TimeStruct.Hours, TimeStruct.Minutes, TimeStruct.Seconds);
      BUFFER_LOG_PRINTF("%s: RTC updated with timezone-corrected time %02d-%02d-%04d %02d:%02d:%02d\n", context, DateStruct.Date, DateStruct.Month, DateStruct.Year, TimeStruct.Hours, TimeStruct.Minutes, TimeStruct.Seconds);
      ntpSuccess = true;
      break;
    }
    else
    {
      USB_SERIAL_PRINTLN("No time available (yet)");
      BUFFER_LOG_PRINTLN("No time available (yet)");
      // Let RTC continue with existing settings
      M5.Lcd.println("Wait for NTP Time\n");
      delay(250);
    }
  }

  if (ntpSuccess)
  {
    USB_SERIAL_PRINTLN("NTP time received");
    M5.Lcd.printf("RTC set (UTC%+d)\n", ((int)(timezoneOffset + int(dstOffset))/3600));
    BUFFER_LOG_PRINTF("RTC set (UTC%+d)\n", ((int)(timezoneOffset + int(dstOffset))/3600));
    delay(200);
  }
  else
  {      
    // Let RTC continue with existing settings
    M5.Lcd.println("No NTP Server\n");
    BUFFER_LOG_PRINTLN("No NTP Server\n");
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
