#include <Arduino.h>
#include <CAN.h>
//CAN BUS connections: CS > D10, SO > D12, SI > D11, SCK > D13,  INT > D2

const int WellPresPin = A7;   // Well pressure sensor analog pin A7
const int ComError_Pin = 5;   // CAN comuication bord failure connected to digital pin 5
const byte FilterRelayPin = 6;

void setup() {
  // put your setup code here, to run once:
  //Serial.begin(9600);
  //delay(2000);
  //Serial.println("serial started");
  
  pinMode(ComError_Pin, OUTPUT);      // sets the digital pin 5 as output
  pinMode(FilterRelayPin, OUTPUT);  // sets the digital pin 4 as output
  digitalWrite(FilterRelayPin, HIGH);

  // start the CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    //Serial.println("Starting CAN failed!");
    while (1) { //stops program
      digitalWrite(ComError_Pin, LOW);
      delay(700);
      digitalWrite(ComError_Pin, HIGH);
      delay(700);
    } 
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  digitalWrite(ComError_Pin, HIGH); //program started
   
  CANsend(0x2, Pressure(WellPresPin));
  //Serial.println(recive(0x7));
  
  if (recive(0x7) == 1) {
    digitalWrite(FilterRelayPin, LOW);
  }
  else {
    digitalWrite(FilterRelayPin, HIGH);
  }

}

byte Pressure(int Sensor){
  int PSen;
  int pst = 0;
  byte var = 0;
//Serial.println(analogRead(Sensor));
  while(var < 20){
    PSen = analogRead(Sensor);   // read the pump pressure input pin
    var++;
    pst = pst + PSen;
  }
  
  PSen = pst / 20;
  byte Readp = map(PSen, 100, 880, 0, 100);
  return Readp;
}
  
void CANsend (char addr, byte val){
  CAN.beginPacket(addr);
  CAN.write(val);
  CAN.endPacket();
  delay(100);
}

byte recive(char addr) {
  int packetSize = CAN.parsePacket();
  if (packetSize) {    
    while (CAN.available()) {
      if (CAN.packetId() == addr){ 
       return CAN.read();
       delay(1000);
      }
    }
  }
}
