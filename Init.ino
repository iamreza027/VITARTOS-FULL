void initRTC(){
  Wire.begin(21, 22);

  if (!rtc.begin())
  {
    Serial.println("RTC NOT FOUND");
    return;
  }

  if (rtc.lostPower())
  {
    Serial.println("RTC LOST POWER");
  }
}

void initDFPlayer(){
  DFSerial.begin(9600, SERIAL_8N1, 25, 33);

  if (!dfPlayer.begin(DFSerial))
  {
    Serial.println("DFPLAYER ERROR");
    return;
  }

  dfPlayer.volume(25);  // 0-30

  Serial.println("DFPLAYER READY");
}