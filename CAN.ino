void startCan() {
  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK)
    Serial.println("MCP2515 OK");
  else
    Serial.println("MCP2515 FAIL");

  CAN0.setMode(MCP_LISTENONLY);
  pinMode(CAN_INT, INPUT);
}

void handleSpeed()
{
  int speed = rxBuf[0];

  canData.speed = speed;
  canData.validSpeed = true;
}

int CAN_getSpeed()
{
  if (canData.simSpeedEnable)
    return canData.simSpeed;

  return canData.speed;
}

void handleGear(){
  int gear = rxBuf[1];

  canData.gear = gear;
  canData.validGear = true;
}