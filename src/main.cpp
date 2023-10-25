#include <SPI.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <TimeLib.h>
#include "secrets.h"

#define BLYNK_TEMPLATE_ID SECRET_TEMPLATE_ID
#define BLYNK_TEMPLATE_NAME "Arduino IoT 33"
#define BLYNK_AUTH_TOKEN SECRET_AUTH

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

#define PUMP_PIN 12
#define VALVE_PIN 14
#define WATER_LEVEL_SENSOR A0

#define PUMP_CONTROL V0
#define VALVE_CONTROL V5
#define PUMP_DURATION V1
#define PUMP_INTERVAL V2
#define CYCLE_GAUGE_PIN V3
#define WATER_LEVEL_GAUGE_PIN V6
#define LABEL_PIN V4

#define WATER_LEVEL_THRESHOLD_UPPER 95
#define WATER_LEVEL_THRESHOLD_LOWER 5

unsigned long pumpStartTime;
unsigned long pumpDuration;
bool pumpState;
bool valveState;
bool isReservoirOk;
int waterLevel;
int cycleTimes[8];
int nextCycleHour;
long timeNow;

BlynkTimer timer;

void stopPump()
{
  Blynk.virtualWrite(CYCLE_GAUGE_PIN, 0);
  if (pumpState) {
    Blynk.virtualWrite(LABEL_PIN, "Pump is idle");
    Blynk.virtualWrite(CYCLE_GAUGE_PIN, 0);
    pumpState = false;
    digitalWrite(PUMP_PIN, LOW);
    Blynk.virtualWrite(PUMP_CONTROL, 0);
  }
  Blynk.setProperty(PUMP_DURATION, "isDisabled", "false");
  Blynk.setProperty(PUMP_INTERVAL, "isDisabled", "false");
}

void startPump()
{
  if (!pumpState && waterLevel >= WATER_LEVEL_THRESHOLD_LOWER) {
    Blynk.virtualWrite(LABEL_PIN, "Pump cycle start");
    pumpStartTime = millis();
    pumpState = true;
    Blynk.virtualWrite(PUMP_CONTROL, 1);
    digitalWrite(PUMP_PIN, HIGH);
  } else {
    Blynk.virtualWrite(PUMP_CONTROL, 0);
  }
  Blynk.setProperty(PUMP_DURATION, "isDisabled", "true");
  Blynk.setProperty(PUMP_INTERVAL, "isDisabled", "true");
}

void runPump()
{
  unsigned long pumpEndTime = pumpStartTime + pumpDuration;
  while (pumpState && millis() < pumpEndTime && waterLevel >= WATER_LEVEL_THRESHOLD_LOWER) {
    unsigned long elapsedTime = millis() - pumpStartTime;
    int percent = (elapsedTime * 100) / pumpDuration;
    Blynk.virtualWrite(CYCLE_GAUGE_PIN, percent);
  }
  stopPump();
}

void openReservoirValve()
{
  if (valveState) { // true means valve is closed
    valveState = false;
    digitalWrite(VALVE_PIN, LOW);
    Blynk.virtualWrite(VALVE_CONTROL, 0);
  }
}

void closeReservoirValve()
{
  if (!valveState) { // false means valve is open
    valveState = true;
    digitalWrite(VALVE_PIN, HIGH);
    Blynk.virtualWrite(VALVE_CONTROL, 1);
  }
}

void checkReservoirLevel()
{
  int sensorValue = analogRead(WATER_LEVEL_SENSOR);
  waterLevel = map(sensorValue, 400, 1024, 100, 0);
  Blynk.virtualWrite(WATER_LEVEL_GAUGE_PIN, waterLevel);

  if (waterLevel >= WATER_LEVEL_THRESHOLD_UPPER) {
      if (isReservoirOk) {
        isReservoirOk = false;
        Blynk.virtualWrite(LABEL_PIN, "Reservoir full");
      }
      closeReservoirValve();
  } else if (waterLevel <= WATER_LEVEL_THRESHOLD_LOWER) {
      if (isReservoirOk) {
        isReservoirOk = false;
        Blynk.virtualWrite(LABEL_PIN, "Reservoir empty");
      }
      openReservoirValve();
  } else {
      if (!isReservoirOk) {
        isReservoirOk = true;
        Blynk.virtualWrite(LABEL_PIN, "Reservoir OK");
      }
      openReservoirValve();
  }
}

void updateCycleDisplay()
{
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", hour(timeNow), minute(timeNow));
  String message = "It's " + String(timeStr) + ". Next cycle at " + String(nextCycleHour) + ":00.";
  Blynk.virtualWrite(LABEL_PIN, message);
}

void findNextCycleHour()
{
  int currentHour = hour(timeNow);
  if (currentHour == 0) {
    nextCycleHour = cycleTimes[0];
    updateCycleDisplay();
    return;
  }

  for (int i = 0; i < 8; i++) {
    if (currentHour < cycleTimes[i]) {
      nextCycleHour = cycleTimes[i];
      updateCycleDisplay();
      return;
    }
  }

  nextCycleHour = cycleTimes[0];
  updateCycleDisplay();
}

void cycleTimer()
{
  Blynk.sendInternal("rtc", "sync");
  int currentHour = hour(timeNow);
  if (currentHour == nextCycleHour) {
    findNextCycleHour();
    startPump();
  }
}

BLYNK_WRITE(PUMP_CONTROL)
{
  int value = param.asInt();
  if (value == 1) {
    startPump();
  } else {
    stopPump();
  }
}

BLYNK_WRITE(PUMP_DURATION) {
  int value = param.asInt();
  pumpDuration = value * 1000;
}

BLYNK_WRITE(PUMP_INTERVAL) {
  int value = param.asInt();
  if (value == 3) {
    cycleTimes[0] = 3;
    cycleTimes[1] = 6;
    cycleTimes[2] = 9;
    cycleTimes[3] = 12;
    cycleTimes[4] = 15;
    cycleTimes[5] = 18;
    cycleTimes[6] = 21;
    cycleTimes[7] = 0;
  } else if (value == 6) {
    cycleTimes[0] = 6;
    cycleTimes[1] = 12;
    cycleTimes[2] = 18;
    cycleTimes[3] = 0;
    cycleTimes[4] = 6;
    cycleTimes[5] = 12;
    cycleTimes[6] = 18;
    cycleTimes[7] = 0;
  } else if (value == 9) {
    cycleTimes[0] = 9;
    cycleTimes[1] = 18;
    cycleTimes[2] = 3;
    cycleTimes[3] = 12;
    cycleTimes[4] = 21;
    cycleTimes[5] = 6;
    cycleTimes[6] = 15;
    cycleTimes[7] = 0;
  } else if (value == 12) {
    cycleTimes[0] = 6;
    cycleTimes[1] = 18;
    cycleTimes[2] = 6;
    cycleTimes[3] = 18;
    cycleTimes[4] = 6;
    cycleTimes[5] = 18;
    cycleTimes[6] = 6;
    cycleTimes[7] = 18;
  }
  findNextCycleHour();
}

BLYNK_WRITE(InternalPinRTC)
{
  timeNow = param.asLong();
}

BLYNK_CONNECTED()
{
  Blynk.syncAll();
  Blynk.virtualWrite(PUMP_PIN, 0);
  Blynk.sendInternal("rtc", "sync");
  findNextCycleHour();
}

void setup()
{
  pinMode(WATER_LEVEL_SENSOR, INPUT);
  pinMode(VALVE_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timer.setInterval(1000L, checkReservoirLevel);
  timer.setInterval(1000L, cycleTimer);
  timer.setInterval(1000L, runPump);
  Blynk.virtualWrite(PUMP_DURATION, 64);
  Blynk.virtualWrite(PUMP_INTERVAL, 3);
  Blynk.virtualWrite(VALVE_CONTROL, 0);
  pumpState = false;
  valveState = false; // false means valve is open
  cycleTimes[0] = 3;
  cycleTimes[1] = 6;
  cycleTimes[2] = 9;
  cycleTimes[3] = 12;
  cycleTimes[4] = 15;
  cycleTimes[5] = 18;
  cycleTimes[6] = 21;
  cycleTimes[7] = 0;
}

void loop()
{
  Blynk.run();
  timer.run();
}
