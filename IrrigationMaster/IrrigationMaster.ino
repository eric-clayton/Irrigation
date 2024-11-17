#include <CAN.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>

LiquidCrystal_PCF8574 lcd(0x27);  // set the LCD address to 0x27 for a 16 chars and 2 line display
//LCD connections: SCL > A5,  SDA > A4
//CAN BUS connections: CS > D10, SO > D12, SI > D11, SCK > D13,  INT > D2

const byte RpumpPin = 9;        // Rain pump connected to digital pin 9
const byte WpumpPin = 4;        // Well pump connected to digital pin 4

const byte LP = 20;  // 30 PSI
const byte HP = 40;  // 40 PSI

unsigned long TimeMark[9];
unsigned long TimeElaps[9];

const unsigned long primRst = 10800000; // 3 hr (1h = 3,600,000ms)
const unsigned long primtime = 30000; 
const unsigned long overruntime = 1800000; // 30m (30m = 1,800,000ms) will require a manual reset.
const unsigned long FilterFushIntval  = 86400000; // 24 hr
bool pumpHOLD;
bool RpumpHOLD;
bool WpumpHOLD;
bool overrun;
byte vl;

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
  lcd.print("Starting");
  delay(2000);
  lcd.clear();
  // start the CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    lcd.clear();
    lcd.print("Com CAN failed!");
    while (1);
  }
  pinMode(RpumpPin, OUTPUT);  // sets the digital pin 9 as output
  pinMode(WpumpPin, OUTPUT);  // sets the digital pin 4 as output
  digitalWrite(RpumpPin, HIGH);
  digitalWrite(WpumpPin, HIGH);
  }

void loop() {
   // put your main code here, to run repeatedly:
  byte Perrlev;
  byte pump = RpumpPin;
  byte tn = 4;
  char *pumpname[] = {"Rain Pump", "Well Pump" };
  int i = 0; 
  if (vl) {
    pump = WpumpPin;
  //  i++;
    tn = 5;
  }

  if (!pumpHOLD && !overrun) {
    do {
        Perrlev = MaintainPresure(pump, pumpname[vl]);
        lcd.clear();
        lcd.print(pumpname[vl]);
        lcd.setCursor(0, 1);
        if (Perrlev == 2) lcd.print("Prim Fail");
        if (Perrlev == 3) {
          lcd.print("Overrun");
          overrun = true;
        }
        delay(5000);
        RpumpHOLD = true;
      } while (Perrlev > 4);
  }

  if (timer(tn, primRst, "Monostable")) pumpHOLD = false; //Stop trying Rain pump for pumpRst time




if (vl) {
  lcd.clear();
  lcd.print("ALL PUMPS FAILED");
  lcd.setCursor(0, 1);
  if (Perrlev == 2) lcd.print("Prim Fail");
  if (Perrlev == 3) lcd.print("Overrun");
  delay(1000);
  while (1); //Halt Loop
} 


vl++;
}



byte* GetDisplayPress(char pumpname[]){
  byte waterPress = recive(0x2);

  if (waterPress == 255) {
    lcd.clear();
    lcd.print("ERROR No Data");
    lcd.setCursor(0, 1);
    lcd.print("from Remote End");
    delay(1000);
  }else {
    lcd.clear();
    lcd.print(pumpname);
    lcd.setCursor(0, 1);
    lcd.print("Pressure ");
    lcd.print(waterPress);
    lcd.print(" PSI");
    delay(1000);
  }
    return waterPress;
  }
short GetFlow(char pumpname[]) {
  byte prevPressure = 255;
  byte nextPressure = 255;
  byte MAX_TRIES = 5;
  byte currTry = 0;
  while(prevPressure == 255) {
    if(currTry >= MAX_TRIES) {
      return 255;
    }
    prevPressure = GetDisplayPress(pumpname);
    currTry++;
  }
  currTry = 0;
  while(nextPressure == 255 && currTry < MAX_TRIES) {
    if(currTry >= MAX_TRIES) {
      return 255;
    }
    nextPressure = GetDisplayPress(pumpname);
    currTry++;
  }
  short flow = nextPressure - prevPressure;
  return flow;
}
byte MaintainPresure(byte pump, char pumpname[]){
  //byte Pump;
  //if (pumpname == "Rain Pump") Pump = RpumpPin;
  //if (pumpname == "Well Pump") Pump = WpumpPin;
  byte Perrorlev; //0 = pres low, 1 = pres hi, 2 = prim fail, 3 = exced runtme, 4 = leak
  delay(1000);

  do {
    byte Readp = GetDisplayPress(pumpname);
    delay(100);
    if (Readp <= LP) {
      digitalWrite(pump, LOW);   // running pump
      if (timer(2, primtime, "Monostable")){
        // Prime pump time delay
        Perrorlev = 2;
        digitalWrite(pump, HIGH);   // stop pump
      }
      else {
        Perrorlev = 0;
      }
    }
    // Max pressure
    else if (Readp >= HP){
      timer(2, 0, "resetMSTimer");
      timer(3, 0, "resetMSTimer");
      digitalWrite(pump, HIGH);   // stop pump
      Perrorlev = 1;
      if (timer(7, FilterFushIntval, "Monostable")) filterflush();
    }
    // Pressure normal
    else {
      if (timer(3, overruntime, "Monostable")) Perrorlev = 3;
      timer(2, 0, "resetMSTimer");
      }
    } while (Perrorlev < 2);
  return Perrorlev;
}

void filterflush() {
  lcd.clear();
  lcd.print("Flushing Filter");
  CANsend(0x7, 1);
  delay(5000);
  CANsend(0x7, 0);
}

bool timer(byte x, unsigned long ms, char mode[13]){
  byte sc;
  if (mode == "Astable") {sc = 0;}
  if (mode == "Monostable") {sc = 1;}
  if (mode == "resetMSTimer") {sc = 2;}
  if (TimeMark[x] == 0) TimeMark[x] = millis();
  TimeElaps[x] = millis() - TimeMark[x];
    switch (sc){
      case 0:  //Astable
        if (TimeElaps[x] < ms){ //time is up//timeout = true;
          return true;
        }
        else if(TimeElaps[x] < ms*2){
          return false;
        }
         else{ 
          TimeMark[x] = millis();
        }
        break;
      case 1:  //Monostable
        if (TimeElaps[x] >= ms){ //time is up//timeout = true;
          TimeElaps[x] = 0;
          TimeMark[x] = 0;//reset
          return true;
        }
        else
        {
          return false;
        }
      break;
      case 2:  //resetMSTimer
        TimeMark[x] = 0;
        TimeElaps[x] = 0;
        return false;
        break;   
    }   
} 
  
void CANsend(char addr, byte val){
  CAN.beginPacket(addr);
  CAN.write(val);
  CAN.endPacket();
  delay(100);
}

byte recive(char addr) {
  //int packetSize = CAN.parsePacket();
//return 255;
   if (CAN.parsePacket()) {    
     while (CAN.available()) {
      if (CAN.packetId() == addr){ 
         // lcd.clear();
         // lcd.print("CANReading somthing");
       delay(100);
       return CAN.read();
       
      }
    } 
  }else {return 255;}
 

  }




