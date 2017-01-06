/*
SenseoWifi.cpp - base file for the SenseoWifi project.
Created by Thomas Dietrich, 2016-03-05.
Released under some license.
*/

#include <Homie.h>

#include "SenseoLed.h"
#include "SenseoSM.h"
#include "SenseoControl.h"
#include "Cup.h"
#include "pins.h"
#include "constants.h"
#include "testIO.cpp"

SenseoLed mySenseoLed(ocSenseLedPin);
SenseoSM mySenseoSM;
SenseoControl myControl(ocPressPowerPin, ocPressLeftPin, ocPressRightPin);
Cup myCup(cupDetectorPin);

HomieNode senseoNode("machine", "machine");
HomieSetting<bool> CupDetectorAvailableSetting("available", "Enable cup detection (TCRT5000)");
HomieSetting<bool> BuzzerSetting("buzzer", "Enable buzzer feedback (no water, cup finished, ...)");
HomieSetting<bool> DebuggingSetting("debugging", "Enable debugging output over MQTT");

// will get called by the LED changed interrupt
void ledChangedHandler() {
  mySenseoLed.pinStateToggled();
  Homie.getLogger() << "LED pulse duration: " << mySenseoLed.getLastPulseDuration() << endl;
}

//
bool powerHandler(const HomieRange& range, const String& value) {
  Homie.getLogger() << "MQTT topic '/power' message: " << value << endl;
  if (value != "ON" && value !="OFF" && value != "RESET") {
    Homie.getLogger() << "--> malformed message content. Allowed: [ON,OFF]" << endl;
    return false;
  }
  
  if (value == "ON" && mySenseoSM.getState() == SENSEO_OFF) {
    Homie.getLogger() << "Powering on" << endl;
    myControl.pressPowerButton();
  }
  else if (value == "OFF" && mySenseoSM.getState() != SENSEO_OFF) {
    Homie.getLogger() << "Powering off" << endl;
    myControl.pressPowerButton();
  }
  else if (value == "RESET") {
    if (BuzzerSetting.get()) {
      tone(beeperPin, 1024, 250);
      tone(beeperPin, 2048, 250);
      tone(beeperPin, 1024, 500);
    }
    Homie.reset();
  }
  else {
    // nothing to do here, machine already in right state
    Homie.getLogger() << "Machine in correct power state" << endl;
    senseoNode.setProperty("power").send(value);
  }
  return true;
}

//
bool brewHandler(const HomieRange& range, const String& value) {
  Homie.getLogger() << "MQTT topic '/brew' message: " << value << endl;
  if (value != "1cup" && value !="2cup") {
    Homie.getLogger() << "--> malformed message content. Allowed: [1cup,2cup]" << endl;
    senseoNode.setProperty("brew").send("false");
    return false;
  }
  
  if (mySenseoSM.getState() != SENSEO_READY) {
    Homie.getLogger() << "--> wrong state" << endl;
    senseoNode.setProperty("brew").send("false");
    return false;
  }
  
  if (CupDetectorAvailableSetting.get()) {
    if (myCup.isNotAvailable() || myCup.isFull()) {
      Homie.getLogger() << "--> no or full cup detected" << endl;
      senseoNode.setProperty("brew").send("false");
      return false;
    }
  }
  
  if (value == "1cup") myControl.pressLeftButton();
  if (value == "2cup") myControl.pressRightButton();
  return true;
}

//
void senseoStateEntryAction() {
  switch (mySenseoSM.getState()) {
    case SENSEO_OFF: {
      senseoNode.setProperty("power").send("OFF");
      break;
    }
    case SENSEO_HEATING: {
      break;
    }
    case SENSEO_READY: {
      if (BuzzerSetting.get()) tone(beeperPin, 1024, 500);
      break;
    }
    case SENSEO_BREWING: {
      senseoNode.setProperty("brew").send("true");
      break;
    }
    case SENSEO_NOWATER: {
      if (BuzzerSetting.get()) tone(beeperPin, 2048, 1000);
      senseoNode.setProperty("outOfWater").send("true");
      break;
    }
  }
}

//
void senseoStateExitAction() {
  switch (mySenseoSM.getStatePrev()) {
    case SENSEO_OFF: {
      senseoNode.setProperty("power").send("ON");
      break;
    }
    case SENSEO_HEATING: {
      break;
    }
    case SENSEO_READY: {
      break;
    }
    case SENSEO_BREWING: {
      if (CupDetectorAvailableSetting.get()) myCup.fillUp();
      senseoNode.setProperty("brew").send("false");
      senseoNode.setProperty("cupFull").send("true");
      // 0---------------------|-----+-----|-----+-----|-------100
      int tolerance = (BrewingTime2Cup - BrewingTime1Cup) / 2;
      if (abs(mySenseoSM.getSecondsInLastState() - BrewingTime1Cup) < tolerance) {
        senseoNode.setProperty("brewedSize").send("1");
      }
      else if (abs(mySenseoSM.getSecondsInLastState() - BrewingTime2Cup) < tolerance) {
        senseoNode.setProperty("brewedSize").send("2");
      }
      else {
        senseoNode.setProperty("brewedSize").send("0");
        senseoNode.setProperty("debug").send(String("unexpected time in SENSEO_BREWING state.") + String(mySenseoSM.getSecondsInLastState()));
      }
      break;
    }
    case SENSEO_NOWATER: {
      senseoNode.setProperty("outOfWater").send("false");
      break;
    }
  }
}

//
void setupHandler() {
  if (BuzzerSetting.get()) tone(beeperPin, 2048, 500);
  /**
  * Send status data once.
  */
  // senseoNode.setProperty("ledState").send(mySenseoLed.getStateAsString());
  // senseoNode.setProperty("opState").send(mySenseoSM.getStateAsString());
  // if (CupDetectorAvailableSetting.get()) {
  //   senseoNode.setProperty("cupAvailable").send(myCup.isAvailable() ? "true" : "false");
  //   senseoNode.setProperty("cupFull").send(myCup.isFull() ? "true" : "false");
  // }
  
  attachInterrupt(digitalPinToInterrupt(ocSenseLedPin), ledChangedHandler, CHANGE);
  
  Homie.getLogger() << endl << "☕☕☕☕ Enjoy your SenseoWifi ☕☕☕☕" << endl << endl;
}

//
void loopHandler() {
  /**
  * Check and update the cup availability, based on the cup detector signal.
  * (no cup, cup available, cup full)
  */
  if (CupDetectorAvailableSetting.get()) {
    myCup.updateState();
    if (myCup.isAvailableChanged()) {
      Homie.getLogger() << "Cup state changed. Available: " << (myCup.isAvailable() ? "yes" : "no") << endl;
      senseoNode.setProperty("cupAvailable").send(myCup.isAvailable() ? "true" : "false");
    }
    if (myCup.isFullChanged()) {
      Homie.getLogger() << "Cup state changed. Full: " << (myCup.isFull() ? "yes" : "no") << endl;
      senseoNode.setProperty("cupFull").send(myCup.isFull() ? "true" : "false");
    }
  }
  
  /**
  * Update the low level LED state machine based on the measured LED timings.
  * (off, slow blinking, fast blinking, on)
  */
  mySenseoLed.updateState();
  if (mySenseoLed.hasChanged()) {
    Homie.getLogger() << "LED state machine, new LED state: " << mySenseoLed.getStateAsString() << endl;
    senseoNode.setProperty("ledState").send(mySenseoLed.getStateAsString());
  }
  
  /**
  * Update the higher level Senseo state machine based on the LED state.
  * (off, heating, ready, brewing, no water)
  */
  mySenseoSM.updateState(mySenseoLed.getState());
  if (mySenseoSM.stateHasChanged()) {
    Homie.getLogger() << "(time in last state: " << mySenseoSM.getSecondsInLastState() << "s)" << endl;
    Homie.getLogger() << "Senseo state machine, new Senseo state: " << mySenseoSM.getStateAsString() << endl;
    senseoNode.setProperty("opState").send(mySenseoSM.getStateAsString());
    
    senseoStateEntryAction();
    senseoStateExitAction();
  }
  
  /**
  * Non-blocking Low-High-Low transition.
  * Check for a simulated button press - release after > 100ms
  */
  myControl.releaseIfPressed();
}

//
void setup() {
  Serial.begin(115200);
  
  /**
  * pin initializations
  */
  pinMode(ocPressLeftPin, OUTPUT);
  pinMode(ocPressRightPin, OUTPUT);
  pinMode(ocPressPowerPin, OUTPUT);
  pinMode(ocSenseLedPin, INPUT_PULLUP);
  
  digitalWrite(ocPressPowerPin, LOW);
  digitalWrite(ocPressLeftPin, LOW);
  digitalWrite(ocPressRightPin, LOW);
  
  pinMode(beeperPin, OUTPUT);
  pinMode(resetButtonPin, INPUT_PULLUP);
  
  if (CupDetectorAvailableSetting.get()) {
    pinMode(cupDetectorPin, INPUT_PULLUP);
    pinMode(cupDetectorAnalogPin, INPUT);
    /** needed class routine after pin initialization */
    myCup.initDebouncer();
  }
  
  /**
  * Testing routine. Activate only in development environemt.
  * Test the circuit and Senseo connections, loops indefinitely.
  *
  * Wifi will NOT BE AVAILABLE, no OTA!
  */
  if (false) testIO();
  
  
  /**
  * Homie specific settings
  */
  Homie_setFirmware("senseo-wifi-wemos", "0.9.3");
  Homie_setBrand("SenseoWifi");
  //Homie.disableResetTrigger();
  Homie.disableLedFeedback();
  Homie.setResetTrigger(resetButtonPin, LOW, 5000);
  Homie.setSetupFunction(setupHandler);
  Homie.setLoopFunction(loopHandler);
  
  /**
  * Homie: Options, see at the top of this file.
  */
  CupDetectorAvailableSetting.setDefaultValue(true);
  BuzzerSetting.setDefaultValue(true);
  DebuggingSetting.setDefaultValue(false);
  
  /**
  * Homie: Advertise custom SenseoWifi MQTT topics
  */
  senseoNode.advertise("debug");
  senseoNode.advertise("ledState");
  senseoNode.advertise("opState");
  senseoNode.advertise("power").settable(powerHandler);
  senseoNode.advertise("brew").settable(brewHandler);
  senseoNode.advertise("brewedSize");
  senseoNode.advertise("outOfWater");
  if (CupDetectorAvailableSetting.get()) senseoNode.advertise("cupAvailable");
  if (CupDetectorAvailableSetting.get()) senseoNode.advertise("cupFull");
  
  if (BuzzerSetting.get()) tone(beeperPin, 1024, 2000);
  
  Homie.setup();
}

//
void loop() {
  Homie.loop();
}
