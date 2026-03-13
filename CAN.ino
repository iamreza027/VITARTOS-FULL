void handleSpeed() {
  int speed = tpoBuf[0];

  if (speed > 30) {
    EventItem item;

    strcpy(item.event, "V6");
    strcpy(item.kodeST, "1");
    item.valueSensor = speed;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    sprintf(item.dateStr, "%04d/%02d/%02d",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

    sprintf(item.timeStr, "%02d:%02d:%02d",
            t->tm_hour, t->tm_min, t->tm_sec);

    xQueueSend(eventQueue, &item, 0);
  }
}