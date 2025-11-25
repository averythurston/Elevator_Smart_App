// =======================
// SMART ELEVATOR PROTOTYPE
// (Merged moveCabin + Stats + JSON)
// =======================

// Libraries
#include <Servo.h>
#include <Wire.h>
#include <LCD_I2C.h>
#include <Pushbutton.h>
#include <LiquidCrystal_I2C.h>

// ---------------- Pin Definitions ----------------
const int SERVO_PIN  = 7;

const int REED_PIN   = 10;
const int REED_PIN1  = 11;
const int REED_PIN2  = 9;
const int REED_PIN3  = 12;

const int LED_PIN3   = 13;
const int LED_PIN2   = 8;
const int LED_PIN1   = 2;
const int LED_PIN    = 1;

const int buttonPin  = 6;
int       buttonState  = HIGH;
const int buttonPin1 = 5;
int       buttonState1 = HIGH;
const int buttonPin2 = 4;
int       buttonState2 = HIGH;
const int buttonPin3 = 3;
int       buttonState3 = HIGH;

// Last known floor vs target floor
int lastFloor    = 0;
int targetFloor  = 0;   // for JSON + app
int direction    = 0;   // not heavily used, but kept

// ---------------- Servo / Buttons ----------------
Servo myservo;
int   pos = 0;

Pushbutton ButtonT(buttonPin);
Pushbutton Button1(buttonPin1);
Pushbutton Button2(buttonPin2);
Pushbutton Button3(buttonPin3);

// [DEFINITIONS]
#define VelUP   65
#define VelDW   65
#define STOPPED 90

#define STOP_PULSE     1500
#define BACKWARD_PULSE 1300
#define FORWARD_PULSE  1700

// Forward declarations
int  moveCabin(byte floor);
byte checkFloor();
void ledLight(int PIN_Reed, int PIN_Light, int floor);

// [VARIABLE DECLARATION]
byte sensorFloor[] = { REED_PIN, REED_PIN1, REED_PIN2, REED_PIN3 };
int  delayFloor[]  = { 250, 300, 350, 400 };
byte currentFloor;
byte buttonPressed;

// LCD Display
LCD_I2C lcd(0x27, 16, 2);

// ------------------------------------------------------------
//                  NEW: STATISTICS ENGINE
// ------------------------------------------------------------

unsigned long bootTime = 0;

// Counters
unsigned long totalTrips            = 0;
unsigned long stopCount             = 0;
unsigned long doorCycles            = 0;
unsigned long travelDistanceFloors  = 0;

unsigned long totalTripTime         = 0;
unsigned long totalWaitTime         = 0;
unsigned long tripCount             = 0;
unsigned long waitCount             = 0;

unsigned long moveStart             = 0;
unsigned long buttonPressTime       = 0;

// Helper: safe average
unsigned long safeAvg(unsigned long sum, unsigned long count) {
  return (count == 0) ? 0 : (sum / count);
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
  // Motor
  myservo.attach(SERVO_PIN);

  // LCD
  lcd.begin();
  lcd.display();
  lcd.backlight();

  // Serial
  Serial.begin(9600);
  Serial.flush();

  // Reed switches
  pinMode(REED_PIN,  INPUT_PULLUP);
  pinMode(REED_PIN1, INPUT_PULLUP);
  pinMode(REED_PIN2, INPUT_PULLUP);
  pinMode(REED_PIN3, INPUT_PULLUP);

  // LEDs
  pinMode(LED_PIN,  OUTPUT);
  pinMode(LED_PIN1, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);
  pinMode(LED_PIN3, OUTPUT);

  // Buttons
  pinMode(buttonPin,  INPUT_PULLUP);
  pinMode(buttonPin1, INPUT_PULLUP);
  pinMode(buttonPin2, INPUT_PULLUP);
  pinMode(buttonPin3, INPUT_PULLUP);

  currentFloor = checkFloor();  // Find starting floor
  lastFloor    = currentFloor;
  targetFloor  = currentFloor;

  lcd.print("STOPPED at ");
  lcd.print(currentFloor);
  Serial.println("End of Setup");

  bootTime = millis();
}

// ------------------------------------------------------------
// Main Loop
// ------------------------------------------------------------
void loop() {
  // -------- Receive commands from Python server (GOTO:x) --------
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');

    if (cmd.startsWith("GOTO:")) {
      int f = cmd.substring(5).toInt();
      if (f < 0) f = 0;
      if (f > 3) f = 3;
      buttonPressTime = millis();   // measure wait time until motion actually starts
      moveCabin((byte)f);
    }
  }

  // -------- Manual button commands --------
  if (ButtonT.isPressed()) {
    buttonPressTime = millis();
    moveCabin(0);
  } else if (Button1.isPressed()) {
    buttonPressTime = millis();
    moveCabin(1);
  } else if (Button2.isPressed()) {
    buttonPressTime = millis();
    moveCabin(2);
  } else if (Button3.isPressed()) {
    buttonPressTime = millis();
    moveCabin(3);
  }

  // -------- Send JSON state every 200ms --------
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 200) {
    lastSend = millis();

    // Door status (1=open if sensorFloor is active at currentFloor)
    int doorOpen = (digitalRead(sensorFloor[currentFloor]) == LOW ? 1 : 0);

    // Direction for JSON: based on last floor change (simple approx)
    int dir = 0;
    if (currentFloor < lastFloor)      dir = +1;   // (sign convention kept as you had)
    else if (currentFloor > lastFloor) dir = -1;

    // Elevator state text
    const char* st;
    if (dir == 0 && doorOpen == 1)      st = "DoorOpen";
    else if (dir == 0 && doorOpen == 0) st = "Idle";
    else                                st = "Moving";

    unsigned long uptimeMs   = millis() - bootTime;
    unsigned long avgTripMs  = safeAvg(totalTripTime, tripCount);
    unsigned long avgWaitMs  = safeAvg(totalWaitTime, waitCount);

    // SEND JSON (single line)
    Serial.print("{\"floor\":");
    Serial.print(currentFloor);

    Serial.print(",\"target\":");
    Serial.print(targetFloor);

    Serial.print(",\"dir\":");
    Serial.print(dir);

    Serial.print(",\"door\":");
    Serial.print(doorOpen);

    Serial.print(",\"state\":\"");
    Serial.print(st);
    Serial.print("\"");

    // Stats fields
    Serial.print(",\"totalTrips\":");
    Serial.print(totalTrips);

    Serial.print(",\"stopCount\":");
    Serial.print(stopCount);

    Serial.print(",\"doorCycles\":");
    Serial.print(doorCycles);

    Serial.print(",\"avgTripMs\":");
    Serial.print(avgTripMs);

    Serial.print(",\"avgWaitMs\":");
    Serial.print(avgWaitMs);

    Serial.print(",\"travelDistanceFloors\":");
    Serial.print(travelDistanceFloors);

    Serial.print(",\"uptimeMs\":");
    Serial.print(uptimeMs);

    Serial.println("}");

    lastFloor = currentFloor;
  }
} // [END OF LOOP]

// ------------------------------------------------------------
// moveCabin (MERGED + STATS)
// ------------------------------------------------------------
int moveCabin(byte floor) {
  byte velMaxUP   = VelUP;
  byte velMaxDown = VelDW;

  // Clamp floor range [0..3]
  if (floor > 3) floor = 3;

  // If we’re already there, nothing to do
  if (currentFloor == floor) {
    targetFloor = floor;
    return floor;
  }

  // WAIT TIME STAT: time from button/command to motion start
  if (buttonPressTime > 0) {
    unsigned long now = millis();
    totalWaitTime += (now - buttonPressTime);
    waitCount++;
    buttonPressTime = 0;
  }

  // Prepare motion stats
  byte oldFloor = currentFloor;
  moveStart = millis();

  // Decide direction (for stats, not JSON state)
  if (currentFloor < floor) direction = +1;
  else                      direction = -1;

  targetFloor = floor;

  // --------------------------
  //       MOVING UP
  // --------------------------
  if (currentFloor < floor) {
    myservo.write(velMaxUP);
    lcd.clear();
    lcd.print("GOING UP:  ");
    lcd.print(floor);

    pos = 0;
    bool floorReached = false;

    while (!floorReached) {
      myservo.write(pos);  // ramp speed
      delay(5);
      pos += 1;

      if (pos == velMaxUP) {
        delay(delayFloor[floor]);
        myservo.write(STOPPED);
      }

      // reed LOW = magnet close => on that floor
      if (!digitalRead(sensorFloor[floor])) {
        myservo.write(STOPPED);
        delay(delayFloor[floor]);
        currentFloor = floor;

        lcd.clear();
        lcd.print("STOPPED:  ");
        lcd.print(floor);

        floorReached = true;
      }
    }

  }
  // --------------------------
  //       MOVING DOWN
  // --------------------------
  else if (currentFloor > floor) {
    myservo.write(-velMaxDown);
    lcd.clear();
    lcd.print("GOING Down:  ");
    lcd.print(floor);

    while (digitalRead(sensorFloor[floor])) {
      for (int pos1 = 0; pos1 >= -velMaxDown; pos1--) {
        myservo.write(pos1);
        delay(15);
      }
    }

    delay(delayFloor[floor]);
    myservo.write(STOPPED);

    currentFloor = floor;
    lcd.clear();
    lcd.print("STOPPED:  ");
    lcd.print(floor);
  }

  // ---------- STATS UPDATE ----------
  totalTrips++;
  stopCount++;
  doorCycles++;  // every arrival treated as a door cycle
  travelDistanceFloors += (unsigned long)abs((int)floor - (int)oldFloor);

  unsigned long tripDuration = millis() - moveStart;
  totalTripTime += tripDuration;
  tripCount++;

  // Back to idle
  direction = 0;

  return floor;
}

// ------------------------------------------------------------
// Floor detection / LED handling
// ------------------------------------------------------------
byte checkFloor() {
  // Manage Lights
  signed int floor = 5;

  if (digitalRead(sensorFloor[0])) {
    ledLight(REED_PIN, LED_PIN, floor);
    myservo.write(STOPPED);
    return 0;
  } else if (digitalRead(sensorFloor[1])) {
    ledLight(REED_PIN1, LED_PIN1, floor);
    myservo.write(STOPPED);
    return 1;
  } else if (digitalRead(sensorFloor[2])) {
    ledLight(REED_PIN2, LED_PIN2, floor);
    myservo.write(STOPPED);
    return 2;
  } else if (digitalRead(sensorFloor[3])) {
    ledLight(REED_PIN3, LED_PIN3, floor);
    myservo.write(STOPPED);
    return 3;
  } else {

    //Check Floor via sensors
    if (!digitalRead(sensorFloor[0])) {
      delay(delayFloor[0]);
      myservo.write(STOPPED);
      return 0;
    } else if (!digitalRead(sensorFloor[1])) {
      delay(delayFloor[1]);
      myservo.write(STOPPED);
      return 1;
    } else if (!digitalRead(sensorFloor[2])) {
      delay(delayFloor[2]);
      myservo.write(STOPPED);
      return 2;
    } else if (!digitalRead(sensorFloor[3])) {
      delay(delayFloor[3]);
      myservo.write(STOPPED);
      return 3;
    } else {
      delay(10);
      floor = checkFloor();
      return -1;
    }

    ledLight(REED_PIN, LED_PIN, floor);
    ledLight(REED_PIN2, LED_PIN1, floor);
    ledLight(REED_PIN2, LED_PIN2, floor);
    ledLight(REED_PIN3, LED_PIN3, floor);
  }
  return 0;
}

void ledLight(int PIN_Reed, int PIN_Light, int floor) {
  lcd.clear();
  Serial.println("Switch closed");
  digitalWrite(PIN_Light, HIGH);  // Turn the LED on
}