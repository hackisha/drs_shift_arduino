#include <LiquidCrystal_I2C.h>

#define DEBOUNCE_DELAY 50

LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Button Class ---
class Button {
  byte pin;
  bool state, lastReading;
  unsigned long lastDebounceTime;

public:
  Button(byte p) : pin(p), state(false), lastReading(HIGH), lastDebounceTime(0) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
  }

  void update() {
    bool reading = digitalRead(pin);
    if (reading != lastReading) {
      lastDebounceTime = millis();
      lastReading = reading;
    }
    if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
      state = reading;
    }
  }

  bool isPressed() {
    return state == LOW;
  }

  bool wasPressed(bool &prevState) {
    bool result = (prevState == HIGH && state == LOW);
    prevState = state;
    return result;
  }
};

// --- RearWingController Class ---
class RearWingController {
  byte pinA, pinB;
  unsigned long moveDuration;
  int speedThreshold;
  bool isUp, moving, directionUp;
  unsigned long moveStartTime;

public:
  RearWingController(byte a, byte b, int threshold = 60, unsigned long duration = 1000)
    : pinA(a), pinB(b), speedThreshold(threshold), moveDuration(duration),
      isUp(false), moving(false), directionUp(false) {}

  void begin() {
    pinMode(pinA, OUTPUT);
    pinMode(pinB, OUTPUT);
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
  }

  void update(int speed) {
    if (moving) return;

    if (speed >= speedThreshold && !isUp) {
      digitalWrite(pinA, HIGH);
      digitalWrite(pinB, LOW);
      directionUp = true;
      moving = true;
      moveStartTime = millis();
      Serial.println("Rear Wing: UP start");
    } else if (speed < speedThreshold && isUp) {
      digitalWrite(pinA, LOW);
      digitalWrite(pinB, HIGH);
      directionUp = false;
      moving = true;
      moveStartTime = millis();
      Serial.println("Rear Wing: DOWN start");
    }
  }

  void tick() {
    if (moving && millis() - moveStartTime >= moveDuration) {
      digitalWrite(pinA, LOW);
      digitalWrite(pinB, LOW);
      isUp = directionUp;
      moving = false;
      Serial.println(isUp ? "Rear Wing: UP complete" : "Rear Wing: DOWN complete");
    }
  }
};

// --- ShiftController Class ---
class ShiftController {
  byte fuelCutPin, shiftUpPin, shiftDownPin, l298n2_IN2;
  int gearSequence[5] = {1, 0, 2, 3, 4};
  int gearIdx = 1;
  unsigned int &fuelCutTime, &cylinderTime, &cylinderDelay;

  unsigned long fuelCutStartTime, cylinderStartTime, cylinderDelayStartTime, cylinderDownStartTime;
  bool fuelCutTimeOn, cylinderOn, cylinderDelayOn, cylinderOnDown;

public:
  ShiftController(byte f, byte u, byte d, byte i,
                  unsigned int &fTime, unsigned int &cTime, unsigned int &cDelay)
    : fuelCutPin(f), shiftUpPin(u), shiftDownPin(d), l298n2_IN2(i),
      fuelCutTime(fTime), cylinderTime(cTime), cylinderDelay(cDelay),
      fuelCutTimeOn(false), cylinderOn(false), cylinderDelayOn(false), cylinderOnDown(false) {}

  void begin() {
    pinMode(fuelCutPin, OUTPUT);
    pinMode(shiftUpPin, OUTPUT);
    pinMode(shiftDownPin, OUTPUT);
    pinMode(l298n2_IN2, OUTPUT);
    digitalWrite(fuelCutPin, HIGH);
  }

  void handleShiftUp() {
    digitalWrite(fuelCutPin, LOW);
    Serial.println("Fuel cut: OFF (차단)");
    fuelCutTimeOn = true;
    fuelCutStartTime = millis();

    stopCylinders();
    cylinderDelayOn = true;
    cylinderDelayStartTime = millis();
  }

  void handleIdleShiftUp() {
    shiftCylinderUp();
    cylinderOn = true;
    cylinderStartTime = millis();
    Serial.println("ShiftUpIdle Cylinder ON");
  }

  void handleShiftDown() {
    digitalWrite(fuelCutPin, LOW);
    Serial.println("Fuel cut: OFF (차단)");
    fuelCutTimeOn = true;
    fuelCutStartTime = millis();

    shiftCylinderDown();
    cylinderOnDown = true;
    cylinderDownStartTime = millis();
    Serial.println("Shift Down Cylinder ON");
  }

  void tick() {
    unsigned long now = millis();

    if (fuelCutTimeOn && now - fuelCutStartTime >= fuelCutTime) {
      digitalWrite(fuelCutPin, HIGH);
      fuelCutTimeOn = false;
      Serial.println("Fuel cut: ON (복원)");
    }

    if (cylinderDelayOn && now - cylinderDelayStartTime >= cylinderDelay) {
      shiftCylinderUp();
      cylinderOn = true;
      cylinderStartTime = now;
      cylinderDelayOn = false;
    }

    if (cylinderOn && now - cylinderStartTime >= cylinderTime) {
      stopCylinders();
      cylinderOn = false;
      Serial.println("Cylinder Up Off");
      if (gearIdx < 4) gearIdx++;
    }

    if (cylinderOnDown && now - cylinderDownStartTime >= cylinderTime) {
      stopCylinders();
      cylinderOnDown = false;
      Serial.println("Shift Down Cylinder OFF");
      if (gearIdx > 0) gearIdx--;
    }
  }

private:
  void shiftCylinderUp() {
    digitalWrite(shiftUpPin, HIGH);
    Serial.println("Shift Cylinder UP");
  }

  void shiftCylinderDown() {
    digitalWrite(shiftDownPin, HIGH);
    digitalWrite(l298n2_IN2, LOW);
    Serial.println("Shift Cylinder DOWN");
  }

  void stopCylinders() {
    digitalWrite(shiftUpPin, LOW);
    digitalWrite(shiftDownPin, LOW);
    digitalWrite(l298n2_IN2, LOW);
    Serial.println("Cylinders STOPPED");
  }
};

// --- 전역 인스턴스 및 핀 정의 ---
const byte fuelCutPin = 5;
const byte shiftUpPin = 6;
const byte shiftDownPin = 7;
const byte l298n2_IN2 = 9;

const byte shiftUpButtonPin = 2;
const byte shiftUpIdleButtonPin = 3;
const byte shiftDownButtonPin = 4;

const byte wingMotorPinA = 11;
const byte wingMotorPinB = 12;
const byte wingMotorEN = 13;

unsigned int fuelCutTime, cylinderTime, cylinderDelay;
unsigned int updateLCDTime = 500;
unsigned long updateLCDStartTime = 0;

Button shiftUpButton(shiftUpButtonPin);
Button shiftUpIdleButton(shiftUpIdleButtonPin);
Button shiftDownButton(shiftDownButtonPin);

RearWingController rearWing(wingMotorPinA, wingMotorPinB);
ShiftController shiftController(fuelCutPin, shiftUpPin, shiftDownPin, l298n2_IN2, fuelCutTime, cylinderTime, cylinderDelay);

int speedValue = 0;
bool prevUp = false, prevIdle = false, prevDown = false;

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FuelCutTime:");
  lcd.print(fuelCutTime);

  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(cylinderTime);
  lcd.setCursor(8, 1);
  lcd.print("D:");
  lcd.print(cylinderDelay);
}

void setup() {
  Serial.begin(9600);
  lcd.begin();
  lcd.backlight();

  pinMode(wingMotorEN, OUTPUT);
  digitalWrite(wingMotorEN, HIGH);

  shiftUpButton.begin();
  shiftUpIdleButton.begin();
  shiftDownButton.begin();

  rearWing.begin();
  shiftController.begin();

  Serial.println("Enter speed value in Serial Monitor.");
}

void loop() {
  unsigned long now = millis();

  // 시리얼 속도 입력
  if (Serial.available() > 0) {
    speedValue = Serial.parseInt();
    Serial.print("Input Speed: ");
    Serial.println(speedValue);
    rearWing.update(speedValue);
  }

  // LCD 주기적 업데이트
  fuelCutTime = analogRead(A0);
  cylinderTime = analogRead(A1);
  cylinderDelay = analogRead(A2);

  if (now - updateLCDStartTime >= updateLCDTime) {
    updateLCD();
    updateLCDStartTime = now;
  }

  // 버튼 업데이트
  shiftUpButton.update();
  shiftUpIdleButton.update();
  shiftDownButton.update();

  if (shiftUpButton.wasPressed(prevUp)) {
    shiftController.handleShiftUp();
  }
  if (shiftUpIdleButton.wasPressed(prevIdle)) {
    shiftController.handleIdleShiftUp();
  }
  if (shiftDownButton.wasPressed(prevDown)) {
    shiftController.handleShiftDown();
  }

  // 시간 기반 동작
  rearWing.tick();
  shiftController.tick();
}
