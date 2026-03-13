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