void initRTC() {
  Wire.begin(21, 22);

  if (!rtc.begin()) {
    Serial.println("RTC NOT FOUND");
    return;
  }

  if (rtc.lostPower()) {
    Serial.println("RTC LOST POWER");
  }
}

void initDFPlayer() {
  DFSerial.begin(9600, SERIAL_8N1, 25, 33);

  if (!dfPlayer.begin(DFSerial)) {
    Serial.println("DFPLAYER ERROR");
    return;
  }

  int vol = atoi(deviceConfig.volumeLevel);  // convert string ke int

  if (strlen(deviceConfig.volumeLevel) == 0) {
    vol = 23;  // default
  }

  // safety clamp (optional tapi disarankan)
  if (vol < 0) vol = 0;
  if (vol > 30) vol = 30;

  dfPlayer.volume(vol);  // 0-30


  Serial.println("DFPLAYER READY");
  Serial.print("Level Volume : ");
  Serial.println(vol);
}