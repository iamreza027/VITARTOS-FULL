void CheckOverspeed()
{
  int speed = CAN_getSpeed();

  if (speed > 30)
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

  if (gear == 0 && speed > 5)
  {
    if (neutralStartTime == 0)
      neutralStartTime = millis();

    if (millis() - neutralStartTime >= 3000)
    {
      if (!costingNeutralActive)
      {
        costingNeutralActive = true;

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