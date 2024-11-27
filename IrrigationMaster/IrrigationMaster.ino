#include <CAN.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>

#define CURRENTPUMP 0
#define PRESSURE 1
#define WATERLEVEL 2
#define FLOW 3
#define PRIME_RAIN_FAIL 4
#define PRIME_RAIN_FAIL_WATERLEVEL 5
#define PRIME_WELL_FAILED 6
#define RAIN_TIMER 7
#define FILTER_TIMER 8



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
const unsigned long FILTER_REST_TIME  = 86400000;
// 5 seconds to flush
const unsigned long FILTER_TIME = 5000;
const unsigned long FLOW_TIME = 30000;
const unsigned long PRIME_TIME = 30000;
const unsigned long DISPLAY_TIME = 2000;
const byte MAX_MESSAGES = 9;
// set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_PCF8574 lcd(0x27);  
//LCD connections: SCL > A5,  SDA > A4
//CAN BUS connections: CS > D10, SO > D12, SI > D11, SCK > D13,  INT > D2

// Errors
bool isOverrun = false;
bool isPrimeFail = false;
bool isFlowZero = false;
bool primeFailedWell = false;
// Flow is zero but water level is above minimum
bool primeFailedRain = false;
bool displayShutDown = false;
byte distanceToWaterPrimeFailed = 255;
// Save the current pump pin 255 is undefined pin and pumps should be off
byte currentPump = 255;
byte pressure;
byte distanceToWater;
byte flow;

short prevPressure = 255;
byte messageIndex = MAX_MESSAGES;

class Timer {
  unsigned long duration;
  unsigned long startTime;
  bool endMessageFlag = false;
  bool startMessageFlag = false;
  bool active;
  void stopTimer() {
    endMessageFlag = true;
    active = false;
  }
  public:
  Timer(bool active, unsigned long duration) {
    if (active) {
      startTimer();
    }
    this->active = active;
    this->duration = duration;
  }
  bool isActive() {
    return active;
  }
  bool getEndMessageFlag() {
    return endMessageFlag;
  }
  void resetEndMessageFlag() {
    this->endMessageFlag = false;
  }
  bool getStartMessageFlag() {
    return startMessageFlag;
  }
  void resetStartMessageFlag() {
    this->startMessageFlag = false;
  }
  void startTimer() {
    startMessageFlag = true;
    active = true;
    startTime = millis();
  }

  void cancelTimer() {
    active = false;
  }
  double timeRemainingHr() {
    return ((double)timeRemaining() / 1000.0 / 60.0 / 60.0);
  }
  unsigned long timeRemaining() {
    unsigned long timeTotal = timeElapsed();
    if (timeTotal >= duration) {
      return 0;
    }
    else {
      return duration - timeTotal ;
    }
  }
  unsigned long timeElapsed() {
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
  void update() {
    if (active)
      if (hasTimerRunOut()) 
        stopTimer();
  }
  bool updateConditional(bool shouldTimerBeOn) {
    if (shouldTimerBeOn) {
      if (active) {
        if (hasTimerRunOut()) {
          stopTimer();
          return true;
        }
      }
      else {
        startTimer();
        return false;
      }
    }
    else {
      if (active) {
        cancelTimer();
      }
      return false;
    }
  }
  bool updateContinuous() {
      if (active) {
        if (hasTimerRunOut()) {
          stopTimer();
          startTimer();
          return true;
        }
      }
      else {
        startTimer();
        return false;
      }
  }
  // returns if timer has run over duration
  bool hasTimerRunOut() {
    if(!active) {
      return false;
    }
    if (timeElapsed() >= duration) {
      return true;
    }
    return false;
  }

};
// Setup timers
Timer primeTimer(false, PRIME_TIME);
Timer rainRestTimer(false, RAIN_REST_TIME);
Timer overrunTimer(false, OVERRUN_TIME);
Timer messageTimer(true, DISPLAY_TIME);
Timer oneMessageTimer(true, DISPLAY_TIME);
Timer flowTimer(false, FLOW_TIME);
Timer filterActiveTimer(false, FILTER_TIME);
Timer filterRestTimer(false, FILTER_REST_TIME);
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
  distanceToWater = getDistanceToWater();
  delay(1000);
  printToLcd("Setup complete", "Starting program");
  delay(1000);
}

void loop() {
  displayMessageOnce();
  if (!oneMessageTimer.isActive()) {
    displayMessage();
  }
  pressure = getPressure();

  byte distanceToWater = distanceToWater = getDistanceToWater();

  // if pump on
  if (currentPump != 255) {
    if (pressure >= HIGH_PRESSURE || (currentPump == RAIN_PUMP_PIN && distanceToWater >= DISTANCE_TO_BOTTOM_TANK)) {
      shutDownPumps();
    }
  }
  // Pump is off. If pressure is low turn on pump  
  else if (pressure <= LOW_PRESSURE) {
    if (rainRestTimer.isActive()) {
      // if water level is too low set rain rest to true
      if (distanceToWater >= MAX_WATER_DISTANCE) {
        rainRestTimer.startTimer();
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
      if (filterActiveTimer.isActive()) {
        filterFlush(true); // override set to true to immediately turn off filter flush
        filterRestTimer.startTimer();
      }
      digitalWrite(currentPump, LOW);
      overrunTimer.startTimer();
    }
  }
  // Pump is off. If filter flush is not resting start flushing
  else {
    // if rest is over start flush
    if (!filterRestTimer.isActive()) {
      if (filterFlush(false)) {
        filterRestTimer.startTimer();
      } // override is off so we dont end the flush early
    }
  }
  // failsafes if the pumps are still on
  if (currentPump != 255 || areAnyPumpsOn()) {
    isPrimeFail = primeTimer.updateConditional(pressure <= LOW_PRESSURE);
    if (isPrimeFail) {
      if (currentPump == RAIN_PUMP_PIN) {
        primeFailedRain = isPrimeFail;
        distanceToWaterPrimeFailed = distanceToWater; 
        rainRestTimer.startTimer();
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
    isOverrun = overrunTimer.updateConditional(currentPump != 255);
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
  rainRestTimer.update();
  filterRestTimer.update();
}
byte getPressure(){
  const unsigned long GET_PRESSURE_TIME = 6000;
  const int PRESSURE_ID = 0x2;
  byte pressure = 255;
  Timer pressureTimer(false, GET_PRESSURE_TIME);
  do {
    pressure = CANreceive(PRESSURE_ID);
    
    if(pressure != 255) {
      return pressure;
    }
    else if (pressureTimer.updateConditional(pressure == 255)) {
      shutDownPumps();
      printToLcd("Read timeout", "pressure");
      while(1);
    }
  } while (pressure == 255);
}
byte getDistanceToWater() {
  const unsigned long GET_DISTANCE_TIME = 6000;
  const int DISTANCE_TO_WATER_ID = 0x3;
  byte distanceToWater = 255;
  Timer distanceToWaterTimer(false, GET_DISTANCE_TIME);
  do {
    distanceToWater = CANreceive(DISTANCE_TO_WATER_ID);

    if (distanceToWater != 255) {
      return distanceToWater;
    }
    else if (distanceToWaterTimer.updateConditional(distanceToWater == 255)) {
      shutDownPumps();
      printToLcd("Read timeout", "Water level");
      while(1);
    }
  } while (distanceToWater == 255);
}

bool isNoFlow() {
  byte currPressure = getPressure();
  byte flow = currPressure - prevPressure;
  prevPressure = currPressure;
  return flowTimer.updateConditional(flow <= 1);
}

// override is to turn off fliter flush immediately /
// Return true if filter flush is done
bool filterFlush(bool override) {
  // Send id for pressure
  const int FILTER_ID = 0x7;
  if (filterActiveTimer.updateConditional(true) || override) {
    CANsend(FILTER_ID, 0);
    return true;
  }
  else {
    CANsend(FILTER_ID, 1);
    return false;
  }
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
  displayShutDown = true;

  digitalWrite(WELL_PUMP_PIN, HIGH);
  digitalWrite(RAIN_PUMP_PIN, HIGH);
  currentPump = 255;
  flowTimer.cancelTimer(); 
  primeTimer.cancelTimer();
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
void displayMessageOnce() {
  if (oneMessageTimer.hasTimerRunOut()) {
    if (flowTimer.getEndMessageFlag()) {
      printToLcd("Flow zero", "");
      flowTimer.resetEndMessageFlag();
      oneMessageTimer.startTimer();
    }
    else if (displayShutDown) {
      printToLcd("Shut down", "");
      printPump(currentPump);
      displayShutDown = false;
      oneMessageTimer.startTimer();
    }
    else if (overrunTimer.getStartMessageFlag()) {
      printToLcd("Starting ", "");
      printPump(currentPump);
      overrunTimer.resetStartMessageFlag();
      oneMessageTimer.startTimer();
    }
  }
}
void displayMessage() {
  if (messageTimer.updateContinuous()) {
    messageIndex++;
    if (messageIndex > MAX_MESSAGES) {
      messageIndex = 0;
  }
    bool skipMessage;
    do {
      skipMessage = false;
      switch(messageIndex) {
        case CURRENTPUMP:
          printToLcd("Current pump: ", "");
          printPump(currentPump);
          break;
        case PRESSURE:
          printToLcd("Pressure: ", pressure);
          break;
        case WATERLEVEL:
          printToLcd("Water level: ", "");
          lcd.print((DISTANCE_TO_BOTTOM_TANK - distanceToWater));
          break;
        case FLOW:
          printToLcd("Flow: ", flow);
          break;
        case PRIME_RAIN_FAIL:
          if (skipMsg(!primeFailedRain, skipMessage))
            break;
          printToLcd("Prime fail", "rain pump");
          break;
        case PRIME_RAIN_FAIL_WATERLEVEL:
          if (skipMsg(!primeFailedRain, skipMessage))
            break;
          byte waterLevel = (DISTANCE_TO_BOTTOM_TANK - distanceToWaterPrimeFailed);
          printToLcd("at water lvl ", waterLevel);
          break;
        case PRIME_WELL_FAILED:
          if (skipMsg(!primeFailedWell, skipMessage))
            break;
          printToLcd("Prime fail", "well pump");
          break;
        case RAIN_TIMER:
          if (skipMsg(!rainRestTimer.isActive(), skipMessage))
            break;
          printToLcd("Rain pump rest", "t: ");
          lcd.print(rainRestTimer.timeRemainingHr());
          lcd.print(" hr");
          break;
        case FILTER_TIMER:
          if (skipMsg(!filterRestTimer.isActive(), skipMessage))
            break;
          printToLcd("Filter flush rest", "t: ");
          lcd.print(filterRestTimer.timeRemainingHr());
          lcd.print(" hr");
          break;
      }
    } while (skipMessage);
  }
}
bool skipMsg(bool shouldSkip, bool &skipMessage) {
  if(shouldSkip) {
    messageIndex++; 
    skipMessage = true;
    return true;
  }
  return false;
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



