#include <LiquidCrystal_I2C.h>

#define DEBOUNCE_DELAY 50

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte fuelCutPin      = 5; 
const byte shiftUpPin      = 6;  // L298N 모터 드라이버 1 IN2
const byte shiftDownPin    = 7;  // L298N 모터 드라이버 2 IN1
const byte l298n2_IN2 = 9;       // L298N 모터 드라이버 2 IN2

const byte shiftUpButton      = 2;
const byte shiftUpIdleButton  = 3;
const byte shiftDownButtonPin = 4;  // 버튼 핀 이름 변경

const byte wingMotorPinA = 11;
const byte wingMotorPinB = 12;
const byte wingMotorEN = 13;

int gearSequence[5] = {1, 0, 2, 3, 4};
int gearIdx = 1;

unsigned int fuelCutTime    = 0; 
unsigned int cylinderTime   = 0;
unsigned int cylinderDelay  = 0;
unsigned int updateLCDTime = 500;

unsigned long fuelCutStartTime       = 0;
unsigned long cylinderStartTime      = 0;
unsigned long cylinderDelayStartTime = 0;
unsigned long cylinderDownStartTime  = 0;
unsigned long updateLCDStartTime = 0;

bool fuelCutTimeOn   = false;
bool cylinderOn      = false;
bool cylinderDelayOn = false;
bool cylinderOnDown  = false;

bool prevShiftUpBtnState     = false;
bool prevShiftUpIdleBtnState = false;
bool prevShiftDownBtnState   = false;
bool currentShiftUpBtnState    = false;
bool currentShiftUpIdleBtnState= false;
bool currentShiftDownBtnState  = false;

bool ButtonDebounce(byte pin) {
  static bool buttonState[20];
  static bool lastReading[20];
  static unsigned long lastDebounceTime[20]; 

  bool reading = digitalRead(pin);

  if (reading != lastReading[pin]) {
    lastDebounceTime[pin] = millis();
    lastReading[pin] = reading;
  }

  if ((millis() - lastDebounceTime[pin]) > DEBOUNCE_DELAY) {
    buttonState[pin] = reading;
  }

  return buttonState[pin];
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FuelCutTime:");
  lcd.print(fuelCutTime);

  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(cylinderTime);
  lcd.setCursor(8,1);
  lcd.print("D:");
  lcd.print(cylinderDelay);
}

int speedValue = 0;
int speedThreshold = 60;
unsigned long wingMoveStartTime = 0;
const unsigned long wingMoveDuration = 1000;
bool wingIsUp = false;
bool wingMoving = false;
bool wingDirectionUp = false;

void controlRearWing(int spd) {
  if (wingMoving) return;

  if (spd >= speedThreshold && !wingIsUp) {
    digitalWrite(wingMotorPinA, HIGH);
    digitalWrite(wingMotorPinB, LOW);
    wingMoveStartTime = millis();
    wingMoving = true;
    wingDirectionUp = true;
    Serial.println("Rear Wing: UP start");
  } 
  else if (spd < speedThreshold && wingIsUp) {
    digitalWrite(wingMotorPinA, LOW);
    digitalWrite(wingMotorPinB, HIGH);
    wingMoveStartTime = millis();
    wingMoving = true;
    wingDirectionUp = false;
    Serial.println("Rear Wing: DOWN start");
  }
}

void updateWingMotor() {
  if (wingMoving && millis() - wingMoveStartTime >= wingMoveDuration) {
    digitalWrite(wingMotorPinA, LOW);
    digitalWrite(wingMotorPinB, LOW);
    wingMoving = false;
    wingIsUp = wingDirectionUp;
    Serial.println(wingIsUp ? "Rear Wing: UP complete" : "Rear Wing: DOWN complete");
  }
}

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

void setup() {
  pinMode(fuelCutPin, OUTPUT);
  digitalWrite(fuelCutPin, HIGH); // 평소에는 전류 흐름 유지

  pinMode(shiftUpPin, OUTPUT);
  pinMode(shiftDownPin, OUTPUT);
  pinMode(l298n2_IN2, OUTPUT);

  pinMode(wingMotorPinA, OUTPUT);
  pinMode(wingMotorPinB, OUTPUT);
  digitalWrite(wingMotorPinA, LOW);
  digitalWrite(wingMotorPinB, LOW);
  digitalWrite(wingMotorEN, HIGH);

  pinMode(shiftUpButton,      INPUT_PULLUP);
  pinMode(shiftUpIdleButton,  INPUT_PULLUP);
  pinMode(shiftDownButtonPin, INPUT_PULLUP);

  lcd.begin();
  lcd.backlight();
  Serial.begin(9600);

  prevShiftUpBtnState     = ButtonDebounce(shiftUpButton);
  prevShiftUpIdleBtnState = ButtonDebounce(shiftUpIdleButton);
  prevShiftDownBtnState   = ButtonDebounce(shiftDownButtonPin);
  Serial.println("Enter speed value in Serial Monitor.");
}

void loop() {
  unsigned long currentMillis = millis();

  if (Serial.available() > 0) {
    speedValue = Serial.parseInt(); 
    Serial.print("Input Speed: ");
    Serial.println(speedValue);
    controlRearWing(speedValue);
  }

  updateWingMotor();

  fuelCutTime   = analogRead(A0);
  cylinderTime  = analogRead(A1);
  cylinderDelay = analogRead(A2);

  if (currentMillis - updateLCDStartTime >= updateLCDTime){
    updateLCD();
    Serial.println("LCD updated");
    updateLCDStartTime = currentMillis;
  }

  currentShiftUpBtnState     = ButtonDebounce(shiftUpButton);
  currentShiftUpIdleBtnState = ButtonDebounce(shiftUpIdleButton);
  currentShiftDownBtnState   = ButtonDebounce(shiftDownButtonPin);

  if (prevShiftUpBtnState == HIGH && currentShiftUpBtnState == LOW && gearIdx<4) {
    digitalWrite(fuelCutPin, LOW); // 연료 컷 시작 → 전류 차단
    Serial.println("Fuel cut: OFF (차단)");
    fuelCutTimeOn = true;
    fuelCutStartTime = currentMillis;

    stopCylinders();
    cylinderDelayOn = true;
    cylinderDelayStartTime = currentMillis;
  }

  if (fuelCutTimeOn && currentMillis - fuelCutStartTime >= fuelCutTime) {
    digitalWrite(fuelCutPin, HIGH); // 연료 컷 해제 → 전류 재공급
    fuelCutTimeOn = false;
    Serial.println("Fuel cut: ON (복원)");
  }

  if (cylinderDelayOn && currentMillis - cylinderDelayStartTime >= cylinderDelay) {
    shiftCylinderUp();
    cylinderOn = true;
    cylinderStartTime = currentMillis;
    cylinderDelayOn = false;
  }

  if (cylinderOn && currentMillis - cylinderStartTime >= cylinderTime) {
    stopCylinders();
    cylinderOn = false;
    Serial.println("Cylinder Up Off");
    if (gearIdx < 4) gearIdx++;
  }

  if (prevShiftUpIdleBtnState == HIGH && currentShiftUpIdleBtnState == LOW && gearIdx<4) {
    shiftCylinderUp();
    cylinderOn = true;
    cylinderStartTime = currentMillis;
    Serial.println("ShiftUpIdle Cylinder ON");
  }

  if (cylinderOn && currentMillis - cylinderStartTime >= cylinderTime) {
    stopCylinders();
    cylinderOn = false;
    Serial.println("Cylinder Idle Off");
    if (gearIdx < 4) gearIdx++;
  }

  if (prevShiftDownBtnState == HIGH && currentShiftDownBtnState == LOW && gearIdx>0) {
    digitalWrite(fuelCutPin, LOW); // 연료 컷 시작 → 전류 차단
    Serial.println("Fuel cut: OFF (차단)");
    fuelCutTimeOn = true;
    fuelCutStartTime = currentMillis;

    shiftCylinderDown();
    cylinderOnDown = true;
    cylinderDownStartTime = currentMillis;
    Serial.println("Shift Down Cylinder ON");
  }

  if (cylinderOnDown && currentMillis - cylinderDownStartTime >= cylinderTime) {
    stopCylinders();
    cylinderOnDown = false;
    Serial.println("Shift Down Cylinder OFF");
    if (gearIdx > 0) gearIdx--;
  }

  prevShiftUpBtnState     = currentShiftUpBtnState;
  prevShiftUpIdleBtnState = currentShiftUpIdleBtnState;
  prevShiftDownBtnState   = currentShiftDownBtnState;
}
