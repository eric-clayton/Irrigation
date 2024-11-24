#include <CAN.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>

#define CURRENTPUMP 0
#define PRESSURE 1
#define WATERLEVEL 2
#define FLOW 3

// Rain pump connected to digital pin 9
const byte RAIN_PUMP_PIN = 9;
// Well pump connected to digital pin 4       
const byte WELL_PUMP_PIN = 4; 
// 20 PSI
const byte LOW_PRESSURE = 20;
// 40 PSI  
const byte HIGH_PRESSURE = 40;
// Measured as distance from water to the top of tank. Measured in decimeters
const byte DISTANCE_TO_BOTTOM_TANK = 21;
const byte MAX_WATER_DISTANCE = 18;

// if the water level is low or the flow was 0 do not retry for 3 hr 
const unsigned long RAIN_REST_TIME = 10800000;
// 30 min will require a manual reset. 
const unsigned long OVERRUN_TIME = 1800000; 
// 24 hr
const unsigned long FILTER_FLUSH_REST_TIME  = 86400000;
// 5 seconds to flush
const unsigned long FILTER_FLUSH_TIME = 5000;
const unsigned long FLOW_TIME = 30000;
const unsigned long PRIME_TIME = 30000;
const unsigned long DISPLAY_TIME = 2000;
const byte MAX_MESSAGES = 5;
// set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_PCF8574 lcd(0x27);  
//LCD connections: SCL > A5,  SDA > A4
//CAN BUS connections: CS > D10, SO > D12, SI > D11, SCK > D13,  INT > D2

unsigned long pumpStart;
unsigned long rainRestStart;
unsigned long flushRestStart = 0;
unsigned long filterFlushStart;
unsigned long flowStart;
unsigned long primeStart;
unsigned long messageStart;

bool isOverrun = false;
bool isPrimeFail = false;
bool isFlowZero = false;
bool primeFailedWell = false;

bool isRainRestTimerActive = false;
bool isWaterLevelTimerActive = false;
bool isPressureTimerActive = false;
bool isFlushRestTimerActive = true;
bool isFlushingTimerActive = false;
bool isFlowTimerActive = false;
bool isPrimeTimerActive = false;
bool isMessageTimerActive = false;

bool shouldFlowZeroDisplay = false;

// Flow is zero but water level is above minimum
bool primeFailedRain = false;
byte distanceToWaterPrimeFailed = 255;
// Save the current pump pin 255 is undefined pin and pumps should be off
byte currentPump = 255;
byte pressure;
byte distanceToWater;
byte flow;

short prevPressure = 255;
byte messageIndex = MAX_MESSAGES;

void setup() {
  // put your setup code here, to run once:
  //Serial.begin(9600);
  lcd.begin(16, 2); // initialize the lcd
  delay(1000);
  lcd.setBacklight(255);
  lcd.home(); 
  lcd.noBlink();
  lcd.noCursor();
  lcd.clear();
  lcd.print("Starting...");
  lcd.setCursor(0, 1);
  lcd.print("Intializing CAN");
  delay(2000);
  lcd.clear();
  // start the CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    lcd.clear();
    lcd.print("Com CAN failed!");
    while (1);
  }
  lcd.print("CAN initialized");
  delay(2000);
  printToLcd("Intialize", "Sensors..." );
  // sets the digital pin 9 as output
  pinMode(RAIN_PUMP_PIN, OUTPUT);
   // sets the digital pin 4 as output  
  pinMode(WELL_PUMP_PIN, OUTPUT);
  // Turn off pumps 
  digitalWrite(RAIN_PUMP_PIN, HIGH);
  digitalWrite(WELL_PUMP_PIN, HIGH);
  prevPressure = getPressure();
  byte distanceToWater = getDistanceToWater();
  delay(1000);
  printToLcd("Setup complete", "Starting program");
  delay(1000);
}

void loop() {
  displayMessage();

  printToLcd("Current pump: ", "" );
  printPump(currentPump);
  delay(1000);

  pressure = getPressure();
  printToLcd("Pressure: ", pressure);
  delay(1000);

  byte distanceToWater = distanceToWater = getDistanceToWater();
  printToLcd("Water level: ", (DISTANCE_TO_BOTTOM_TANK - distanceToWater));
  delay(1000);

  // if pump on
  if (currentPump != 255) {
    if (pressure >= HIGH_PRESSURE || (currentPump == RAIN_PUMP_PIN && distanceToWater >= DISTANCE_TO_BOTTOM_TANK)) {
      shutDownPumps();
    }
  }
  // Pump is off. If pressure is low turn on pump  
  else if (pressure <= LOW_PRESSURE) {
    if (!isRainRestTimerActive) {
      // if water level is too low set rain rest to true
      if (distanceToWater >= MAX_WATER_DISTANCE) {
        isRainRestTimerActive = true;
        rainRestStart = millis();
        if (!primeFailedWell) {
          currentPump = WELL_PUMP_PIN;
        } 
      }
      else {
        currentPump = RAIN_PUMP_PIN;
      }
    }
    else if (!primeFailedWell) {
      currentPump = WELL_PUMP_PIN;
    } 
    if(currentPump != 255) {
      // if the filter is flushing turn it off before turning on the pump
      if(isFlushingTimerActive) {
        isFlushRestTimerActive = filterFlush(true); // override set to true to immediately turn off filter flush
        flushRestStart = millis();
      }
      printToLcd("Start", "");
      printPump(currentPump);
      delay(1000);
      digitalWrite(currentPump, LOW);
      pumpStart = millis();
      
    }
  }
  // Pump is off. If filter flush is not resting start flushing
  else {
    // if rest is over start flush
    if(!isFlushRestTimerActive) {
      isFlushRestTimerActive = filterFlush(false); // override is off so we dont end the flush early
      if(isFlushRestTimerActive) {
        flushRestStart = millis();  // flush rest timer has started because flush has ended
      }
    }
  }
  // failsafes if the pumps are still on
  if (currentPump != 255 || areAnyPumpsOn()) {
    if (pressure <= LOW_PRESSURE) {
      isPrimeFail = hasPrimeFailed();
    }
    else {
      isPrimeTimerActive = false;
    }
    if (isPrimeFail) {
      if (currentPump == RAIN_PUMP_PIN) {
        primeFailedRain = isPrimeFail;
        distanceToWaterPrimeFailed = distanceToWater; 
        isRainRestTimerActive = true;
        rainRestStart = millis();
      }
      else if (currentPump == WELL_PUMP_PIN) {
        primeFailedWell = true;
      }
      shutDownPumps();
    }
    isFlowZero = isNoFlow();
    if (isFlowZero) {    
      shutDownPumps();    
    }
    isOverrun = hasTimeRunOut(pumpStart, OVERRUN_TIME);
    if (isOverrun) {
      // num of hours times length of hour in ms (24 hours)
      const unsigned long REST_TIME = (3.6e+6 * 24);
      printToLcd("Overran", "");
      printPump(currentPump);
      shutDownPumps();
      // shutdown for rest time
      delay(REST_TIME);
    }

  }

  if(primeFailedRain) {
    printToLcd("Prime fail", "rain pump");
    delay(2000);
    byte waterLevel = (DISTANCE_TO_BOTTOM_TANK - distanceToWaterPrimeFailed);
    printToLcd("at water lvl ", waterLevel);
    delay(4000);
  }
  if(primeFailedWell) {
    printToLcd("Prime fail", "well pump");
    delay(4000);
  }
  if (isRainRestTimerActive) {
    printToLcd("Rain pump rest", "t: ");
    double timeRemain = ((double)timeRemaining(rainRestStart, RAIN_REST_TIME) / 1000.0 / 60.0 / 60.0);
    lcd.print(timeRemain);
    lcd.print(" hr");
    delay(1500);
    isRainRestTimerActive = !hasTimeRunOut(rainRestStart, RAIN_REST_TIME);
  }
  if (isFlushRestTimerActive) {
    printToLcd("Filter rest", "t: ");
    double timeRemain = ((double)timeRemaining(flushRestStart, FILTER_FLUSH_REST_TIME) / 1000.0 / 60.0 / 60.0);
    lcd.print(timeRemain);
    lcd.print(" hr");
    delay(1500);
    isFlushRestTimerActive = !hasTimeRunOut(flushRestStart, FILTER_FLUSH_REST_TIME);
  }
}
byte getPressure(){
  unsigned long getPressureStart = millis();
  const unsigned long GET_PRESSURE_TIME = 6000;
  const int PRESSURE_ID = 0x2;
  byte pressure = 255;
  do {
    pressure = CANreceive(PRESSURE_ID);

    if(pressure != 255) {
      return pressure;
    }
    else if (hasTimeRunOut(getPressureStart, GET_PRESSURE_TIME)) {
      shutDownPumps();
      printToLcd("Read timeout", "pressure");
      while(1);
    }
  } while (pressure == 255);
}
byte getDistanceToWater() {
  unsigned long getDistanceStart = millis();
  const unsigned long GET_DISTANCE_TIME = 6000;
  const int DISTANCE_TO_WATER_ID = 0x3;
  byte distanceToWater = 255;
  do {
    distanceToWater = CANreceive(DISTANCE_TO_WATER_ID);

    if (distanceToWater != 255) {
      return distanceToWater;
    }
    else if (hasTimeRunOut(getDistanceStart, GET_DISTANCE_TIME)) {
      shutDownPumps();
      printToLcd("Read timeout", "Water level");
      while(1);
    }
  } while (distanceToWater == 255);
}
unsigned long timeRemaining(unsigned long startTime, unsigned long duration) {
  unsigned long timeTotal = timeElapsed(startTime);
  if (timeTotal >= duration) {
    return 0;
  }
  else {
    return duration - timeTotal ;
  }
}
unsigned long timeElapsed(unsigned long startTime) {
  // unsigned long max value;
  const unsigned long MAX_ULONG = 0xFFFFFFFF;
  unsigned long currTime = millis();
  // Arduino timer restarted in between capturing the elapsed time
  if (currTime < startTime) {
    int timeElapsedBeforeReset = MAX_ULONG - startTime; // Time between start time and the max value of a ulong
    return timeElapsedBeforeReset + currTime; // currTime is time elapsed after the clock reset to 0. Adding the time before reset and after gives total time elapsed
  }
  else {
    return currTime - startTime;
  }
}
// returns if clock has finished
bool hasTimeRunOut(unsigned long startTime, unsigned long duration) {
  if(timeElapsed(startTime) >= duration) {
    return true;
  }
  return false;
}
bool timer(bool &timerActive, unsigned long &startTime, const unsigned long duration, bool &shouldDisplayMsg) {
  if (timerActive) {
    if(hasTimeRunOut(startTime, duration)) {
      timerActive = false;
      shouldDisplayMsg = true;
      return true;
    }
  }
  else {
    timerActive = true;
    startTime = true;
    return false;
  }
}
bool timer(bool &timerActive, unsigned long &startTime, const unsigned long duration) {
  if (timerActive) {
    if(hasTimeRunOut(startTime, duration)) {
      timerActive = false;
      return true;
    }
  }
  else {
    timerActive = true;
    startTime = true;
    return false;
  }
}
bool hasPrimeFailed() {
  if (isPrimeTimerActive) {
    return hasTimeRunOut(primeStart, PRIME_TIME);
  }
  else {
    isPrimeTimerActive = true;
    primeStart = millis();
    return false;
  }
}
bool isNoFlow() {
  byte currPressure = getPressure();
  byte flow = currPressure - prevPressure;
  prevPressure = currPressure;
  
  printToLcd("Flow: ", "");
  lcd.print(flow);
  delay(2000);

  if (flow <= 1) {
    return timer(isFlowTimerActive, flowStart, FLOW_TIME, shouldFlowZeroDisplay);
  } else {
    // Reset the flow timer if there is flow
    isFlowTimerActive = false;
    return false;
  }
}

// override is to turn off fliter flush immediately /
// Return true if filter flush is done
bool filterFlush(bool override) {
  // Send id for pressure
  const int FILTER_ID = 0x7;
  if(!isFlushingTimerActive) {
    if (override) {
      return true;
    }
    lcd.clear();
    lcd.print("Flushing Filter");
    delay(500);
    CANsend(FILTER_ID, 1);
    filterFlushStart = millis();
    isFlushingTimerActive = true;
  }
  else if(hasTimeRunOut(filterFlushStart, FILTER_FLUSH_TIME) || override) {
    isFlushingTimerActive = false;
    CANsend(FILTER_ID, 0);
    lcd.clear();
    lcd.print("Done Flushing");
    delay(500);
  }
  return !isFlushingTimerActive;
}
void CANsend(int id, byte val){
  CAN.beginPacket(id);
  CAN.write(val);
  CAN.endPacket();
}

byte CANreceive(int id) {
  if (CAN.parsePacket()) {    
    if (CAN.available()) {
      if (CAN.packetId() == (long)id){ 
        return CAN.read();
      }
    } 
  }
  return 255;
}
// return false if errror occurs trying to shut down the specified pin
void shutDownPumps() {
  printToLcd("Shut down", "");
  printPump(currentPump);
  digitalWrite(WELL_PUMP_PIN, HIGH);
  digitalWrite(RAIN_PUMP_PIN, HIGH);
  currentPump = 255;
  isFlowTimerActive = false;
  isPrimeTimerActive = false;
  isPrimeFail = false;
  delay(1000);
}
void printPump(byte pumpPin) {
  if(pumpPin == RAIN_PUMP_PIN) {
    lcd.print("Rain Pump");
  }
  else if (pumpPin == WELL_PUMP_PIN) {
    lcd.print("Well Pump");
  }
  else {
    lcd.print("None");
  }
}
void printToLcd(const char* line1, const char* line2) {
  lcd.clear();
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
void printToLcd(const char* line1, byte val) {
  lcd.clear();
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(val);
}
bool areAnyPumpsOn(){
  bool wellPumpOn = digitalRead(WELL_PUMP_PIN) == LOW;
  bool rainPumpOn = digitalRead(RAIN_PUMP_PIN) == LOW;
  return wellPumpOn || rainPumpOn;
}
void displayMessages() {
  
  if (shouldDisplayFlowZero) {
    printToLcd("Flow is Zero", "shut down..");
    shouldDisplayFlowZero = false;
    isMessageTimerActive = false;
  }
  if (timer(isMessageTimerActive, messageStart, DISPLAY_TIME)) {
    messageIndex++;
    if (messageIndex > MAX_MESSAGES) {
      messageIndex = 0;
    }
    
    bool skipCurrentMessage = false;
    do {
      switch(messageIndex) {
        case CURRENTPUMP:
          printToLcd("Current pump: ", "");
          printPump(currentPump);
          break;
        case PRESSURE:
          printToLcd("Pressure: ", pressure);
          break;
        case WATERLEVEL:
          printToLcd(Pressure)
      }
    } while (skipCurrentMessage)
  }
}
// bool timer(byte x, unsigned long ms, char mode[13]){
//   byte sc;
//   if (mode == "Astable") {sc = 0;}
//   if (mode == "Monostable") {sc = 1;}
//   if (mode == "resetMSTimer") {sc = 2;}
//   if (TimeMark[x] == 0) TimeMark[x] = millis();
//   TimeElaps[x] = millis() - TimeMark[x];
//     switch (sc){
//       case 0:  //Astable
//         if (TimeElaps[x] < ms){ //time is up//timeout = true;
//           return true;
//         }
//         else if(TimeElaps[x] < ms*2){
//           return false;
//         }
//          else{ 
//           TimeMark[x] = millis();
//         }
//         break;
//       case 1:  //Monostable
//         if (TimeElaps[x] >= ms){ //time is up//timeout = true;
//           TimeElaps[x] = 0;
//           TimeMark[x] = 0;//reset
//           return true;
//         }
//         else
//         {
//           return false;
//         }
//       break;
//       case 2:  //resetMSTimer
//         TimeMark[x] = 0;
//         TimeElaps[x] = 0;
//         return false;
//         break;   
//     }   
// } 
  
// byte MaintainPressure(byte pump, char pumpname[]){
//   byte Perrorlev; //0 = pres low, 1 = pres hi, 2 = prim fail, 3 = exced runtme, 4 = leak

//   do {
//     byte Readp = getPressure();
//     delay(100);
//     if (Readp <= LOW_PRESSURE) {
//       digitalWrite(pump, LOW);   // running pump
//       if (timer(2, PRIME_TIME, "Monostable")){
//         // Prime pump time delay
//         Perrorlev = 2;
//         digitalWrite(pump, HIGH);   // stop pump
//       }
//       else {
//         Perrorlev = 0;
//       }
//     }
//     // Max pressure
//     else if (Readp >= HIGH_PRESSURE){
//       timer(2, 0, "resetMSTimer");
//       timer(3, 0, "resetMSTimer");
//       digitalWrite(pump, HIGH);   // stop pump
//       Perrorlev = 1;
//       if (timer(7, FILTER_FLUSH_TIME, "Monostable")) filterflush();
//     }
//     // Pressure normal
//     else {
//       if (timer(3, OVERRUN_TIME, "Monostable")) Perrorlev = 3;
//       timer(2, 0, "resetMSTimer");
//     }
//     } while (Perrorlev < 2);
//   return Perrorlev;
// }
// return true if filter is done flushing



