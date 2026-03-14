void CheckOverspeed()
{
  int speed = CAN_getSpeed();

  bool overspeed = speed > 30;

  // update kondisi audio
  requestAudio(AUDIO_OVERSPEED, overspeed);

  if (overspeed)
  {
    if (!overspeedActive)
    {
      overspeedActive = true;

      playAudio(AUDIO_OVERSPEED);

      EventItem item;

      strcpy(item.event,"V6");
      strcpy(item.kodeST,"1");
      item.valueSensor = speed;

      fillEventTime(&item);

      xQueueSend(eventQueue,&item,0);

      Serial.println("OVERSPEED DETECTED");
    }
  }
  else
  {
    overspeedActive = false;
  }
}

void CheckCostingNetral()
{
  int speed = CAN_getSpeed();
  int gear  = canData.gear;

  bool coastingCondition = (gear == 0 && speed > 5);

  // update kondisi audio
  requestAudio(AUDIO_COASTING, coastingCondition);

  if (coastingCondition)
  {
    if (neutralStartTime == 0)
      neutralStartTime = millis();

    if (millis() - neutralStartTime >= 3000)
    {
      if (!costingNeutralActive)
      {
        costingNeutralActive = true;

        playAudio(AUDIO_COASTING);

        EventItem item;

        strcpy(item.event,"V10");
        strcpy(item.kodeST,"1");
        item.valueSensor = speed;

        fillEventTime(&item);

        xQueueSend(eventQueue,&item,0);

        Serial.println("COSTING NETRAL");
      }
    }
  }
  else
  {
    neutralStartTime = 0;
    costingNeutralActive = false;
  }
}