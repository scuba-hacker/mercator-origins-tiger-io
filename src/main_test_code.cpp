#ifdef BUILD_INCLUDE_MAIN_TEST_CODE

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


void testGPSTimezoneDetection()
{
  static int testCase = 0;
  static uint32_t lastTestTime = 0;
  
  // Run test every 30 seconds when flag is enabled
  if (!testGPSTimezone || (millis() - lastTestTime < 30000)) {
    return;
  }
  
  lastTestTime = millis();
  
  // Reset GPS verification flag to allow testing
  timezoneVerifiedFromGPS = false;
  
  double testLat, testLon;
  const char* testLocation;
  
  switch (testCase % 2) {
    case 0:
      // London, UK coordinates
      testLat = 51.5074;
      testLon = -0.1278;
      testLocation = "London";
      break;
    case 1:
      // New York, USA coordinates  
      testLat = 40.7128;
      testLon = -74.0060;
      testLocation = "New York";
      break;
  }
  
  USB_SERIAL_PRINTF("=== GPS Timezone Test Case %d: %s ===\n", testCase + 1, testLocation);
  USB_SERIAL_PRINTF("Testing GPS coordinates: lat=%.4f, lon=%.4f\n", testLat, testLon);
  
  if (detectTimezoneFromGPS(testLat, testLon)) {
    USB_SERIAL_PRINTF("Test %s: Timezone detection successful\n", testLocation);
  } else {
    USB_SERIAL_PRINTF("Test %s: Timezone detection failed\n", testLocation);
  }
  
  testCase++;
}


#endif