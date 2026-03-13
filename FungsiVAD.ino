void loadHistoryVAD() {
  VADStorage.begin("VAD", true);

  strncpy(HistoryVAD.HuntingGear212, VADStorage.getString("HG212", "0").c_str(), 3);
  strncpy(HistoryVAD.HuntingGear323, VADStorage.getString("HG323", "0").c_str(), 3);
  strncpy(HistoryVAD.CoastingNetral, VADStorage.getString("COAST", "0").c_str(), 3);
  strncpy(HistoryVAD.TMAbuse, VADStorage.getString("TMA", "0").c_str(), 3);
  strncpy(HistoryVAD.Spining, VADStorage.getString("SPIN", "0").c_str(), 3);
  strncpy(HistoryVAD.SpiningMundur, VADStorage.getString("SPINR", "0").c_str(), 3);
  strncpy(HistoryVAD.OverSpeedMuatan, VADStorage.getString("OSM", "0").c_str(), 3);
  strncpy(HistoryVAD.OverSpeedKosongan, VADStorage.getString("OSK", "0").c_str(), 3);
  strncpy(HistoryVAD.MundurJauh, VADStorage.getString("MJ", "0").c_str(), 3);

  VADStorage.end();

  Serial.println("VAD HISTORY LOADED");
}
void saveHistoryVAD() {
  VADStorage.begin("VAD", false);

  VADStorage.putString("HG212", HistoryVAD.HuntingGear212);
  VADStorage.putString("HG323", HistoryVAD.HuntingGear323);
  VADStorage.putString("COAST", HistoryVAD.CoastingNetral);
  VADStorage.putString("TMA", HistoryVAD.TMAbuse);
  VADStorage.putString("SPIN", HistoryVAD.Spining);
  VADStorage.putString("SPINR", HistoryVAD.SpiningMundur);
  VADStorage.putString("OSM", HistoryVAD.OverSpeedMuatan);
  VADStorage.putString("OSK", HistoryVAD.OverSpeedKosongan);
  VADStorage.putString("MJ", HistoryVAD.MundurJauh);

  VADStorage.end();

  Serial.println("VAD HISTORY SAVED");
}
String getHistoryVAD() {
  char buffer[64];

  sprintf(buffer, "%s~%s~%s~%s~%s~%s~%s~%s~%s",
          HistoryVAD.HuntingGear212,
          HistoryVAD.HuntingGear323,
          HistoryVAD.CoastingNetral,
          HistoryVAD.TMAbuse,
          HistoryVAD.Spining,
          HistoryVAD.SpiningMundur,
          HistoryVAD.OverSpeedMuatan,
          HistoryVAD.OverSpeedKosongan,
          HistoryVAD.MundurJauh);

  return String(buffer);
}

void updateHistoryVAD(char *counter) {
  int value = atoi(counter);
  value++;
  if (value >= 5) {
    Serial.println("VAD Aktif");
  }
  Serial.println(getHistoryVAD());  // Menampilkan Event VAD
  sprintf(counter, "%d", value);

  saveHistoryVAD();
}

/*Contoh Pemanggilan atau penambahan event VAD updateHistoryVAD(HistoryVAD.Spining);*/