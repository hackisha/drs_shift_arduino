#include <LiquidCrystal_I2C.h>

#define DEBOUNCE_DELAY 10  // 스위치 민감도 조정

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte fuelCutPin = 5; 
const byte shiftUpPin = 6;
const byte shiftDownPin = 7;
const byte l298n2_IN2 = 9;

const byte gearResetButton     = 0;
const byte shiftUpButton       = 1;
const byte shiftUpIdleButton   = 2;
const byte shiftDownButtonPin  = 3;
const byte shiftDownIdleButton = 4;

const byte wingMotorPinA = 11;
const byte wingMotorPinB = 12;
const byte wingMotorEN   = 13;

int gearSequence[5] = {1, 0, 2, 3, 4}; 
int gearIdx = 1; // 처음에 1로 세팅

unsigned int fuelCutTime    = 0; 
unsigned int cylinderTime   = 100;
unsigned int cylinderDelay  = 0;
unsigned int updateLCDTime  = 500;

unsigned long fuelCutStartTime       = 0;
unsigned long cylinderStartTime      = 0;
unsigned long cylinderDelayStartTime = 0;
unsigned long cylinderDownStartTime  = 0;
unsigned long updateLCDStartTime     = 0;

bool fuelCutTimeOn   = false;
bool cylinderOn      = false;
bool cylinderDelayOn = false;
bool cylinderOnDown  = false;

bool prevShiftUpBtnState         = false;
bool prevShiftUpIdleBtnState     = false;
bool prevShiftDownBtnState       = false;
bool prevShiftDownIdleBtnState   = false;
bool currentShiftUpBtnState      = false;
bool currentShiftUpIdleBtnState  = false;
bool currentShiftDownBtnState    = false;
bool currentShiftDownIdleBtnState = false;

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
  } 
  else if (spd < speedThreshold && wingIsUp) {
    digitalWrite(wingMotorPinA, LOW);
    digitalWrite(wingMotorPinB, HIGH);
    wingMoveStartTime = millis();
    wingMoving = true;
    wingDirectionUp = false;
  }
}

void updateWingMotor() {
  if (wingMoving && millis() - wingMoveStartTime >= wingMoveDuration) {
    digitalWrite(wingMotorPinA, LOW);
    digitalWrite(wingMotorPinB, LOW);
    wingMoving = false;
    wingIsUp = wingDirectionUp;
  }
}

void shiftCylinderUp() {
  digitalWrite(shiftUpPin, HIGH);
}

void shiftCylinderDown() {
  digitalWrite(shiftDownPin, HIGH);
  digitalWrite(l298n2_IN2, LOW);
}

void stopCylinders() {
  digitalWrite(shiftUpPin, LOW);
  digitalWrite(shiftDownPin, LOW);
  digitalWrite(l298n2_IN2, LOW);
}

void setup() {
  pinMode(fuelCutPin, OUTPUT);
  digitalWrite(fuelCutPin, LOW); // 평소에는 전류 없음

  pinMode(shiftUpPin, OUTPUT);
  pinMode(shiftDownPin, OUTPUT);
  pinMode(l298n2_IN2, OUTPUT);

  pinMode(wingMotorPinA, OUTPUT);
  pinMode(wingMotorPinB, OUTPUT);
  digitalWrite(wingMotorPinA, LOW);
  digitalWrite(wingMotorPinB, LOW);
  digitalWrite(wingMotorEN, HIGH);

  pinMode(gearResetButton,      INPUT_PULLUP);
  pinMode(shiftUpButton,        INPUT_PULLUP);
  pinMode(shiftUpIdleButton,    INPUT_PULLUP);
  pinMode(shiftDownButtonPin,   INPUT_PULLUP);
  pinMode(shiftDownIdleButton,  INPUT_PULLUP);

  lcd.begin();
  lcd.backlight();
  Serial.begin(9600);

  prevShiftUpBtnState        = ButtonDebounce(shiftUpButton);
  prevShiftUpIdleBtnState    = ButtonDebounce(shiftUpIdleButton);
  prevShiftDownBtnState      = ButtonDebounce(shiftDownButtonPin);
  prevShiftDownIdleBtnState  = ButtonDebounce(shiftDownIdleButton);
}


void loop() {
  unsigned long currentMillis = millis();

  if (ButtonDebounce(gearResetButton) == LOW) {
    gearIdx = 2;
    Serial.println("Gear Reset: 2단으로 변경됨");
    delay(300);
  }

  if (Serial.available() > 0) {
    speedValue = Serial.parseInt(); 
    controlRearWing(speedValue);
    Serial.print("Input Speed: ");
    Serial.println(speedValue);
  }

  updateWingMotor();

  fuelCutTime   = analogRead(A0);
  cylinderTime  = analogRead(A1);
  cylinderDelay = analogRead(A2);

  if (currentMillis - updateLCDStartTime >= updateLCDTime){
    updateLCD();
    updateLCDStartTime = currentMillis;
  }

  currentShiftUpBtnState       = ButtonDebounce(shiftUpButton);
  currentShiftUpIdleBtnState   = ButtonDebounce(shiftUpIdleButton);
  currentShiftDownBtnState     = ButtonDebounce(shiftDownButtonPin);
  currentShiftDownIdleBtnState = ButtonDebounce(shiftDownIdleButton);

  if (prevShiftUpBtnState == HIGH && currentShiftUpBtnState == LOW && gearIdx < 4) {
    Serial.println("ShiftUp 버튼 눌림");
    digitalWrite(fuelCutPin, HIGH);
    fuelCutTimeOn = true;
    fuelCutStartTime = currentMillis;
    Serial.println("Fuel Cut ON (전류 공급)");
    stopCylinders();
    cylinderDelayOn = true;
    cylinderDelayStartTime = currentMillis;
  }

  if (fuelCutTimeOn && currentMillis - fuelCutStartTime >= fuelCutTime) {
    digitalWrite(fuelCutPin, LOW);
    fuelCutTimeOn = false;
    Serial.println("Fuel Cut OFF (전류 차단)");
  }

  if (cylinderDelayOn && currentMillis - cylinderDelayStartTime >= cylinderDelay) {
    shiftCylinderUp();
    Serial.println("ShiftCylinderUp() 호출됨");
    cylinderOn = true;
    cylinderStartTime = currentMillis;
    cylinderDelayOn = false;
  }

  if (cylinderOn && currentMillis - cylinderStartTime >= cylinderTime) {
    stopCylinders();
    cylinderOn = false;
    if (gearIdx < 4) gearIdx++;
    Serial.println("UpShift 완료, 기어 증가");
    Serial.print("Gear: ");
    Serial.println(gearIdx);
  }

  if (prevShiftUpIdleBtnState == HIGH && currentShiftUpIdleBtnState == LOW && gearIdx < 4) {
    Serial.println("ShiftUpIdle 버튼 눌림");
    shiftCylinderUp();
    Serial.println("ShiftCylinderUp() 호출됨 (Idle)");
    cylinderOn = true;
    cylinderStartTime = currentMillis;
  }

  if (cylinderOn && currentMillis - cylinderStartTime >= cylinderTime) {
    stopCylinders();
    cylinderOn = false;
    if (gearIdx < 4) gearIdx++;
    Serial.println("Idle UpShift 완료, 기어 증가");
    Serial.print("Gear: ");
    Serial.println(gearIdx);
  }

  if (prevShiftDownBtnState == HIGH && currentShiftDownBtnState == LOW && gearIdx > 0) {
    Serial.println("ShiftDown 버튼 눌림");
    digitalWrite(fuelCutPin, HIGH);
    fuelCutTimeOn = true;
    fuelCutStartTime = currentMillis;
    Serial.println("Fuel Cut ON");
    shiftCylinderDown();
    Serial.println("ShiftCylinderDown() 호출됨");
    cylinderOnDown = true;
    cylinderDownStartTime = currentMillis;
  }

  if (cylinderOnDown && currentMillis - cylinderDownStartTime >= cylinderTime) {
    stopCylinders();
    cylinderOnDown = false;
    if (gearIdx > 0) gearIdx--;
    Serial.println("DownShift 완료, 기어 감소");
    Serial.print("Gear: ");
    Serial.println(gearIdx);
  }

  if (prevShiftDownIdleBtnState == HIGH && currentShiftDownIdleBtnState == LOW && gearIdx > 0) {
    Serial.println("ShiftDownIdle 버튼 눌림");
    shiftCylinderDown();
    Serial.println("ShiftCylinderDown() 호출됨 (Idle)");
    cylinderOnDown = true;
    cylinderDownStartTime = currentMillis;
  }

  if (cylinderOnDown && currentMillis - cylinderDownStartTime >= cylinderTime) {
    stopCylinders();
    cylinderOnDown = false;
    if (gearIdx > 0) gearIdx--;
    Serial.println("Idle DownShift 완료, 기어 감소");
    Serial.print("Gear: ");
    Serial.println(gearIdx);
  }

  // 상태 업데이트
  prevShiftUpBtnState        = currentShiftUpBtnState;
  prevShiftUpIdleBtnState    = currentShiftUpIdleBtnState;
  prevShiftDownBtnState      = currentShiftDownBtnState;
  prevShiftDownIdleBtnState  = currentShiftDownIdleBtnState;
}
