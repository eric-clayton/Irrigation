#include <CAN.h>
#include <NewPing.h>
//CAN BUS connections: CS > D10, SO > D12, SI > D11, SCK > D13,  INT > D2
#define MAX_DISTANCE 293
// Error Codes for CAN communication failure CAN light will blink for each interval listed
// The intialize CAN failed 1s
const short INITIALIZE_ERROR_INTERVAL = 1000;

// Send id for pressure
const int PRESSURE_ID = 0x2;
// Send id for water level
const int WATER_LEVEL_ID = 0x3;
// Receive id for pressure
const int FILTER_ID = 0x7;

// Analog pins
// Water pressure sensor analog pin A7
const byte WATER_PRESSURE_PIN_A = A7;
// Water level echo analog pin D8
const byte WATER_LEVEL_ECHO_PIN_D = 3;
// Water level trig analog pin D9
const byte WATER_LEVEL_TRIG_PIN_D = 4;

// Digital pins
// CAN communication board failure digital pin 5
const byte COM_ERROR_PIN_D = 5;
// Fliter Relay digital pin 6
const byte FILTER_RELAY_PIN_D = 6;
NewPing sonar(WATER_LEVEL_TRIG_PIN_D, WATER_LEVEL_ECHO_PIN_D, MAX_DISTANCE);
void setup() {
  Serial.begin(9600);
  // Connect can communication error as output
  pinMode(COM_ERROR_PIN_D, OUTPUT);
  // Connect filter relay as output
  pinMode(FILTER_RELAY_PIN_D, OUTPUT);
  // Sets the trig Pin as an OUTPUT
  pinMode(WATER_LEVEL_TRIG_PIN_D, OUTPUT);
  // Sets the echo Pin as an INPUT
  pinMode(WATER_LEVEL_ECHO_PIN_D, INPUT); 
  
  digitalWrite(FILTER_RELAY_PIN_D, HIGH);

  // Start the CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    // intialize CAN error;
    communicationErrorLight(INITIALIZE_ERROR_INTERVAL);
  }
  else {
    // Initialize CAN is good
    digitalWrite(COM_ERROR_PIN_D, HIGH);
  }
}

void loop() {
  CANsend(PRESSURE_ID, getPressure());
  CANsend(WATER_LEVEL_ID, getWaterLevel());
  long filterResponse = CANreceive(FILTER_ID);

  if (filterResponse == 1) {
    digitalWrite(FILTER_RELAY_PIN_D, LOW);
  }
  else {
    digitalWrite(FILTER_RELAY_PIN_D, HIGH);
  }

  delay(1000);
}
byte getPressure(){
  const byte NUM_OF_READS = 20;
  int pressureTotal = 0;
  byte counter = 0;

  while (counter < NUM_OF_READS){
    // read the pump pressure input pin
    pressureTotal += analogRead(WATER_PRESSURE_PIN_A);
    counter++;
  }
  int pressureReading = pressureTotal / NUM_OF_READS; // Average pressure reading
  // Map the voltage reading to psi
  byte pressure = map(pressureReading, 100, 880, 0, 100);
  
  return pressure;
}
byte getWaterLevel() {
  unsigned long distance = sonar.ping_cm();
  if (distance <= 5)
  {
    return 0;
  }
  return map(distance, 18, MAX_DISTANCE, 100, 0);
}

void CANsend (int id, byte val) {
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
void communicationErrorLight(unsigned long interval) {
  while(1) {
      digitalWrite(COM_ERROR_PIN_D, LOW);
      delay(interval);
      digitalWrite(COM_ERROR_PIN_D, HIGH);
      delay(interval);
  }
}