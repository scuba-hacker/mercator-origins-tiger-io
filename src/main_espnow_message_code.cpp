#ifdef BUILD_INCLUDE_MAIN_ESPNOW_MESSAGE_CODE

////////////////////////////////////////////////////////////////////////
///////////////////////////////// ESPNow Message Functions
////////////////////////////////////////////////////////////////////////


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

          // Verify timezone from GPS coordinates (one-time check)
          // This will need to use timezone/DST seconds offset sent from Mako (sourced from Lemon)
          // not currently used
          if (!timezoneVerifiedFromGPS && timezoneSetFromIP) {
            // detectTimezoneFromGPS(latitude, longitude);
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
}

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

#endif