#ifdef BUILD_INCLUDE_MAIN_ESPNOW_MESSAGE_CODE

////////////////////////////////////////////////////////////////////////
///////////////////////////////// ESPNow Message Functions
////////////////////////////////////////////////////////////////////////

void processIncomingESPNowMessages()
{
 if (espNOW_msgsReceivedQueue && !otaActive && ESPNowActive)
  {
    if (xQueueReceive(espNOW_msgsReceivedQueue,&(rxQueueItemBuffer),(TickType_t)0))
    {
      switch(rxQueueItemBuffer[0])
      {
        case 'c':   // current target
        {
          ESPNowMessagesReceived++;
          if (strcmp(rxQueueItemBuffer+1,currentTarget) != 0)
          {
            strncpy(previousTarget,currentTarget,sizeof(previousTarget));
            strncpy(currentTarget,rxQueueItemBuffer+1,sizeof(currentTarget));
            refreshTargetShown = true;
            targetValid = true;
          }
          break;
        }

        case 'P':     // Ping message
        {
          // send back a ping response to Mako - make generic later
          publishToMakoPingResponseMessage();
          break;
        }
        case 'X':   // location, heading and current Target info.
        {
             // debugging bad target update in X msg for tiger and
             // why tiger is processing oceanic's msg
//          return;
          // format: targetCode[7],lat,long,heading,targetText
          const int targetCodeOffset = 1;
          const int latitudeOffset = 8;
          const int longitudeOffset = 16;
          const int headingOffset = 24;
          const int depthOffset = 32;
          const int courseOffset = 36;
          const int x_message_flags_offset = 40;
          const int currentTargetOffset = 44;
                    
          char targetCode[7];

          double old_latitude = latitude;
          double old_longitude = longitude;
          double old_heading = heading;

          strncpy(targetCode,rxQueueItemBuffer + targetCodeOffset,sizeof(targetCode));
          targetCode[sizeof(targetCode) - 1] = '\0';  // Guarantee null termination
          memcpy(&latitude,  rxQueueItemBuffer + latitudeOffset,  sizeof(double));
          memcpy(&longitude, rxQueueItemBuffer + longitudeOffset, sizeof(double));
          memcpy(&heading,   rxQueueItemBuffer + headingOffset, sizeof(double));
          memcpy(&depth,     rxQueueItemBuffer + depthOffset, sizeof(float));
          memcpy(&course,    rxQueueItemBuffer + courseOffset, sizeof(float));
          memcpy(&x_message_flags,   rxQueueItemBuffer + x_message_flags_offset, sizeof(uint32_t));

          if (*currentTarget == '\0' ||
              strcmp(rxQueueItemBuffer+currentTargetOffset,currentTarget) != 0)
          {
            strncpy(previousTarget,currentTarget,sizeof(previousTarget));
            strncpy(currentTarget,rxQueueItemBuffer+currentTargetOffset,sizeof(currentTarget));
            refreshTargetShown = true;
            targetValid = true;
          }

          locationHasFix = x_message_flags & X_MESSAGE_FIX_FLAG;

          if (locationHasFix)
            fixMessagesReceived++;
          else
            noFixMessagesReceived++;

          USB_SERIAL_PRINTF("targetCode: %s\n",targetCode);
          USB_SERIAL_PRINTF("latitude: %f\n",latitude);
          USB_SERIAL_PRINTF("longitude: %f\n",longitude);
          USB_SERIAL_PRINTF("heading: %f\n",heading);
          USB_SERIAL_PRINTF("depth: %f\n",depth);
          USB_SERIAL_PRINTF("Fix Msgs: %d\n",fixMessagesReceived);
          USB_SERIAL_PRINTF("No Fix Msgs: %d\n",noFixMessagesReceived);

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

          if (display_mode == DISPLAY_MAP) // map on screen
            mapScreen->drawDiverOnBestFeaturesMapAtCurrentZoom(latitude, longitude, heading);
          else if (display_mode == DISPLAY_CURRENT_TARGET && refreshTargetShown)
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

void publishToMakoPingResponseMessage()
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    attemptSendPingResponseToMako++;
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"p%i", attemptSendPingResponseToMako);
    USB_SERIAL_PRINTF("Sending ESP p msg to Mako... Ping Response Message: %s\n",mako_espnow_buffer);
    pingReceivedFromMako++;
    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
    toSerialESPNowSendDataResult(ESPNowSendResult);

    if (ESPNowSendResult == ESP_OK)
    {
      toggleRedLED();
    }
    else
    {
      toggleRedLED();
      delay(100);
      toggleRedLED();
      delay(100);
      toggleRedLED();
      delay(100);
      toggleRedLED();
    }
  }
  else
  {
    failAttemptSendPingResponseToMako++;
  }
}

void publishToMakoTestMessage(const char* testMessage)
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"T%s",testMessage);
    USB_SERIAL_PRINTLN("Sending ESP T msg to Mako... Test Message");
    USB_SERIAL_PRINTLN(mako_espnow_buffer);

    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
  }
}

void publishToMakoForceGoProButtonsPrimaryControl()
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"F");
    USB_SERIAL_PRINTLN("Sending ESP F msg to Mako... Force use go pro buttons to primary controls (disable M5 Buttons if set to primary)");
    USB_SERIAL_PRINTLN(mako_espnow_buffer);
    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
  }
  else
  {
    USB_SERIAL_PRINTLN("ESPNow inactive - not sending ESP F msg to Mako...");
  }
}

void publishToMakoReedActivation(const bool topReed, const uint32_t ms)
{
  if (isPairedWithMako && ESPNow_mako_peer.channel == ESPNOW_CHANNEL)
  {
    snprintf(mako_espnow_buffer,sizeof(mako_espnow_buffer),"R%c%lu       ",(topReed ? 'T' : 'B'),ms);
    USB_SERIAL_PRINTLN("Sending ESP R msg to Mako... Reed Activation");
    USB_SERIAL_PRINTLN(mako_espnow_buffer);
    ESPNowSendResult = esp_now_send(ESPNow_mako_peer.peer_addr, (uint8_t*)mako_espnow_buffer, strlen(mako_espnow_buffer)+1);
    toSerialESPNowSendDataResult(ESPNowSendResult);
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
      USB_SERIAL_PRINTLN("Sending ESP L msg to Mako... Leak Detected");
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