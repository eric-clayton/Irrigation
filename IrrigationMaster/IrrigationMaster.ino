#include <CAN.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>

#define CURRENTPUMP 0
#define PRESSURE 1
#define WATERLEVEL 2
#define DELTA_P 3
#define PRIME_RAIN_FAIL 4
#define PRIME_RAIN_FAIL_WATERLEVEL 5
#define PRIME_WELL_FAILED 6
#define PRIME_WELL_FAIL_PRESSURE 7
#define FILTER_TIMER 8
#define RAIN_TIMER 9
#define PRIME_TIMER 10

const byte MAX_MESSAGES = 11;


// Rain pump connected to digital pin 9
const byte RAIN_PUMP_PIN = 9;
// Well pump connected to digital pin 4       
const byte WELL_PUMP_PIN = 4; 
const byte SPRINKLER_PIN = 5;
// 20 PSI
const byte LOW_PRESSURE = 20;
// 40 PSI  
const byte HIGH_PRESSURE = 40;

const byte MIN_WATER_LEVEL_PERCENTAGE = 15;
const byte NORMAL_WATER_LEVEL_PERCENTAGE = 50;
const byte MAX_WATER_LEVEL_PERCENTAGE = 100;
// if the water level is low or the delta p was low do not retry for 3 hr 
const unsigned long RAIN_REST_TIME =  24 * 3600000;
// 30 min will require a manual reset. 
const unsigned long OVERRUN_TIME = 1800000; 
// 24 hr
const unsigned long FILTER_REST_TIME  = 86400000;
// 5 seconds to flush
const unsigned long FILTER_TIME = 5000;
const unsigned long DELTA_P_TIME = 100000;
const unsigned long PRIME_TIME = 80000;
const unsigned long DISPLAY_TIME = 2000;

// set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_PCF8574 lcd(0x27);  
//LCD connections: SCL > A5,  SDA > A4
//CAN BUS connections: CS > D10, SO > D12, SI > D11, SCK > D13,  INT > D2

// Errors
bool isDeltaPLow = false;
bool primeFailedWell = false;
byte pressurePrimeFailed = 255;
// Delta p is low but water level is above minimum
bool primeFailedRain = false;
byte waterLevelPrimeFailed = 255;

bool stopPumpMessageFlag = false;
bool startPumpMessageFlag = false;
bool lowDeltaPMessageFlag = false; 
bool startFilterMessageFlag = false;
bool stopFilterMessageFlag = false;

// Save the current pump pin 255 is undefined pin and pumps should be off
byte currentPump = 255;
byte shutDownPump = 255;
byte pressure;
byte waterLevelPercentage;
short deltaP;

short prevPressure = 255;
byte messageIndex = 0;

bool isRainRest = true;
class Timer {
  unsigned long duration;
  unsigned long startTime;
  bool active = false;
  void (*onEnd)() = nullptr;
  void (*onStart)() = nullptr;
  void (*onUpdate)() = nullptr;
  public:
  Timer(unsigned long duration) {
    this->duration = duration;
  }
  unsigned long getStartTime() {
    return startTime;
  }
  bool isActive() {
    return active;
  }
  void start() {
    if (active) {
      return;
    }
    active = true;
    if (onStart) {
      onStart();
    }
    startTime = millis();
  }
  void end() {
    active = false;
    if (onEnd) {
      onEnd();
    }
  }
  void cancel() {
    active = false;
  }
  double timeRemainingHr() {
    return ((double)timeRemaining() / 1000.0 / 60.0 / 60.0);
  }
  double timeRemainingSec() {
    return ((double)timeRemaining() / 1000.0);
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
    if (!active) {
      return 0;
    }
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
    if (!active) {
      return;
    }
    if (timeElapsed() >= duration) {
      end();
    }
    else if (onUpdate)
    {
      onUpdate();
    }  
  }
  attachOnEnd(void (*callback)()) {
    onEnd = callback;
  }
  attachOnStart(void (*callback)()) {
    onStart = callback;
  }
  attachOnUpdate(void (*callback)())
  {
    onUpdate = callback;
  }
};
// Setup timers
Timer primeTimer(PRIME_TIME);
Timer rainRestTimer(RAIN_REST_TIME);
Timer overrunTimer(OVERRUN_TIME);
Timer messageTimer(DISPLAY_TIME);
Timer interruptMessageTimer(DISPLAY_TIME);
Timer deltaPTimer(DELTA_P_TIME);
Timer filterActiveTimer(FILTER_TIME);
Timer filterRestTimer(FILTER_REST_TIME);
void setup() {
  //Serial.begin(9600);
  // Attach timer functions
  overrunTimer.attachOnEnd(pumpOverran);
  primeTimer.attachOnEnd(primeFailed);
  deltaPTimer.attachOnEnd(lowDeltaP);
  filterActiveTimer.attachOnStart(activateFilterFlush);
  filterActiveTimer.attachOnUpdate(activateFilterFlush);
  filterActiveTimer.attachOnEnd(deactivateFilterFlush);
  rainRestTimer.attachOnStart([&isRainRest]() {isRainRest = true;});
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
  waterLevelPercentage = getWaterLevelPercentage();
  delay(1000);
  printToLcd("Setup complete", "Starting program");
  delay(1000);
}
void loop() {
  displayInterruptMessage();
  if (!interruptMessageTimer.isActive()) {
    displayMessage();
  }
  pressure = getPressure();
  waterLevelPercentage = getWaterLevelPercentage();
  if (!rainRestTimer.isActive()) {
    if (waterLevelPercentage < MIN_WATER_LEVEL_PERCENTAGE || waterLevelPercentage >= MAX_WATER_LEVEL_PERCENTAGE) {
      isRainRest = true;
      if (currentPump == RAIN_PUMP_PIN) {
        shutDownPumps();
      }
    }
    else if (waterLevelPercentage >= NORMAL_WATER_LEVEL_PERCENTAGE) {
      isRainRest = false;
    }
  }

  // if pump on
  if (currentPump != 255) {
    if (pressure > LOW_PRESSURE) {
      primeTimer.cancel();
      if (!filterRestTimer.isActive()) {
        filterActiveTimer.start();
      }
    }
    else {
      primeTimer.start();
    }
    if (pressure >= HIGH_PRESSURE) {
      shutDownPumps();
    }
  }
  // Pump is off. If pressure is low turn on pump  
  else if (pressure <= LOW_PRESSURE) {
    if (!isRainRest) {
      currentPump = RAIN_PUMP_PIN;
    }
    else if (!primeFailedWell) {
      currentPump = WELL_PUMP_PIN;
    }
    if (currentPump != 255) {
      startPump();
    }
  }
  // failsafes if the pumps are still on
  if (currentPump != 255 || areAnyPumpsOn()) {
    byte currPressure = pressure;
    deltaP = currPressure - prevPressure;
    prevPressure = currPressure;
    if (deltaP <= 0) {
      deltaPTimer.start();
    }
    else {
      deltaPTimer.cancel();
    }
    if (digitalRead(SPRINKLER_PIN) == HIGH)
    {
      overrunTimer.start();
    }
  }
  updateTimers();
}
void lowDeltaP() { 
  lowDeltaPMessageFlag = true;
  shutDownPumps();    
}
void primeFailed() {
  if (currentPump == RAIN_PUMP_PIN) {
    primeFailedRain = true;
    waterLevelPrimeFailed = waterLevelPercentage; 
    rainRestTimer.start();
  }
  else if (currentPump == WELL_PUMP_PIN) {
    primeFailedWell = true;
    pressurePrimeFailed  = pressure;
  }
  shutDownPumps();
}
void pumpOverran() {
  // if the sprinklers are on cancel shutdown
  if (digitalRead(SPRINKLER_PIN) == LOW)
  {
    overrunTimer.cancel();
    return;
  }
  // num of hours times length of hour in ms (24 hours)
  const unsigned long REST_TIME = (3.6e+6 * 12);
  printToLcd("Overran", "");
  printPump(currentPump);
  shutDownPumps();
  // shutdown for rest time
  delay(REST_TIME);
}
void updateTimers() {
  primeTimer.update();
  overrunTimer.update();
  messageTimer.update();
  interruptMessageTimer.update();
  deltaPTimer.update();
  filterActiveTimer.update();
  rainRestTimer.update();
  filterRestTimer.update();
}
byte getPressure() {
  const int PRESSURE_ID = 0x2;
  byte waterPressure = 255;
  waterPressure = CANreceive(PRESSURE_ID);
    
  if (waterPressure != 255) {
    return waterPressure;
  }
  else {
    return waitForReading(PRESSURE_ID, getPressureFailed);
  }
}

byte getWaterLevelPercentage() {
  const int WATER_LEVEL_PERCENTAGE_ID = 0x3;
  byte waterLevelPercentage = 255;
  waterLevelPercentage = CANreceive(WATER_LEVEL_PERCENTAGE_ID);

  if (waterLevelPercentage != 255) {
    return waterLevelPercentage;
  }
  else {
    return waitForReading(WATER_LEVEL_PERCENTAGE_ID, getWaterLevelFailed);
  }
}
byte waitForReading(const int id, void (*getReadingFailed)()) {
    const unsigned long GET_READING_TIME = 6000;
    byte value;

    Timer getReadingTimer(GET_READING_TIME);
    getReadingTimer.start();

    getReadingTimer.attachOnEnd(getReadingFailed);
    do {
        value = CANreceive(id);

        if (value != 255) {
          return value;
        }
        
        getReadingTimer.update();
    } while (value == 255);
}
void getPressureFailed() {
  shutDownPumps();
  printToLcd("Read timeout", "pressure");
  delay(5000);
}
void getWaterLevelFailed() {
  shutDownPumps();
  printToLcd("Read timeout", "water level");
  delay(5000);
}
void activateFilterFlush() {
  const int FILTER_ID = 0x7;
  startFilterMessageFlag = true;
  CANsend(FILTER_ID, 1);
}
void deactivateFilterFlush() {
  const int FILTER_ID = 0x7;
  stopFilterMessageFlag = true;
  CANsend(FILTER_ID, 0);
  filterRestTimer.start();
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
void startPump() {
  startPumpMessageFlag = true;
  digitalWrite(currentPump, LOW);
  prevPressure = pressure;
  primeTimer.start();
}
void shutDownPumps() {
  stopPumpMessageFlag = true;
  shutDownPump = currentPump;
  digitalWrite(WELL_PUMP_PIN, HIGH);
  digitalWrite(RAIN_PUMP_PIN, HIGH);
  currentPump = 255;
  deltaP = 0;
  deltaPTimer.cancel();
  overrunTimer.cancel();
  primeTimer.cancel();
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
void printToLcd(const char* line1, short val) {
  lcd.clear();
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(val);
}
void printToLcd(const char* line1, unsigned long val) {
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
void displayInterruptMessage() {
  if (interruptMessageTimer.isActive() || (!lowDeltaPMessageFlag && !stopPumpMessageFlag && !startPumpMessageFlag && !startFilterMessageFlag && !stopFilterMessageFlag)) {
    return;
  }
  else {
    if (lowDeltaPMessageFlag) {
      printToLcd("Delta P is low", "");
      lowDeltaPMessageFlag = false;
    }
    else if (stopPumpMessageFlag) {
      printToLcd("Shut down: ", "");
      printPump(shutDownPump);
      stopPumpMessageFlag = false;
    }
    else if (startPumpMessageFlag) {
      printToLcd("Start pump: ", "");
      printPump(currentPump);
      startPumpMessageFlag = false;
    }
    else if (startFilterMessageFlag) {
      printToLcd("Filter flushing", "");
      startFilterMessageFlag = false;
    }
    else if (stopFilterMessageFlag) {
      printToLcd("Filter flush", "ended");
      stopFilterMessageFlag = false;
    }
  }
  interruptMessageTimer.start();
}
void displayMessage() {
  if(messageTimer.isActive()) {
    return;
  }
  bool skipMessage;
  do {
    skipMessage = false;
    switch (messageIndex) {
      case CURRENTPUMP:
        displayCurrentPump();
        break;
      case PRESSURE:
        displayPressure();
        break;
      case WATERLEVEL:
        displayWaterLevel();
        break;
      case DELTA_P:
        displayDeltaP();
        break;
      case PRIME_RAIN_FAIL:
        if (primeFailedRain)
          displayPrimeRainFail();
        else
          skipMessage = true;
        break;
      case PRIME_RAIN_FAIL_WATERLEVEL:
        if (primeFailedRain)
          displayPrimeRainFailWaterLevel();
        else
          skipMessage = true;
        break;
      case PRIME_WELL_FAILED:
        if (primeFailedWell)
          displayPrimeWellFailed();
        else
          skipMessage = true;
        break;
      case PRIME_WELL_FAIL_PRESSURE:
        if (primeFailedWell)
          displayPrimeWellFailPressure();
        else
          skipMessage = true;
        break;
      case RAIN_TIMER:
        if (rainRestTimer.isActive())
          displayRainTimer();
        else
          skipMessage = true;
        break;
      case FILTER_TIMER:
        if (filterRestTimer.isActive())
          displayFilterTimer();
        else
          skipMessage = true;
        break;
      case PRIME_TIMER:
        if (primeTimer.isActive()){
          displayPrimeTimer();
        }
        else {
          skipMessage = true;
        }
    }
    messageIndex++;
    if (messageIndex >= MAX_MESSAGES)
      messageIndex = 0;
  } while (skipMessage);
  messageTimer.start();
}

void displayCurrentPump() {
  printToLcd("Current pump: ", "");
  printPump(currentPump);
}

void displayPressure() {
  printToLcd("Pressure: ", pressure);
}

void displayWaterLevel() {
  printToLcd("Water Tank: ", "");
  lcd.print(waterLevelPercentage);
  lcd.print("% full");
}

void displayDeltaP() {
  printToLcd("DeltaP: ", deltaP);
}

void displayPrimeRainFail() {
  printToLcd("Prime fail", "rain pump");
}

void displayPrimeRainFailWaterLevel() {
  printToLcd("at water %", waterLevelPrimeFailed);
}

void displayPrimeWellFailed() {
  printToLcd("Prime fail", "well pump");
}
void displayPrimeWellFailPressure() {
  printToLcd("at pressure", pressurePrimeFailed);
}

void displayRainTimer() {
    printToLcd("Rain pump rest", "t: ");
    lcd.print(rainRestTimer.timeRemainingHr());
    lcd.print(" hr");
}

void displayFilterTimer() {
    printToLcd("Filtr flush rest", "t: ");
    lcd.print(filterRestTimer.timeRemainingHr());
    lcd.print(" hr");
}
void displayPrimeTimer() {
    printToLcd("Prime time", "t: ");
    lcd.print(primeTimer.timeRemainingSec());
    lcd.print(" s");
}

bool updateSkipMessage(bool shouldSkip, bool &skipMessage) {
  if(shouldSkip) {
    skipMessage = true;
    return true;
  }
  return false;
}




