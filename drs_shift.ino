#include <LiquidCrystal_I2C.h>

#define DEBOUNCE_DELAY 30 // 스위치 민감도 조정

// --- 주변 장치 및 핀 설정 ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
//l928n 모터 드라이버 모듈 IN1 HIGH일떄 IN2는 LOW 
// 출력 핀l298n2_IN2
const byte fuelCutPin = 5;
const byte shiftUpPin = 6;
const byte shiftDownPin = 7;
const byte l298n2_IN2 = 9;
const byte l298n2_IN4 = 10;
const byte wingMotorPinA = 11;
const byte wingMotorPinB = 12;
const byte wingMotorEN   = 13;

// 입력 핀 (버튼)
const byte gearResetButtonPin   = 0;
const byte shiftUpButtonPin     = 1;
const byte shiftUpIdleButtonPin = 2;
const byte shiftDownButtonPin   = 3;
const byte shiftDownIdleButtonPin = 4;

// --- 상태 머신(State Machine) 정의 ---
enum ShiftState {
  S_IDLE,                         // 대기 상태
  S_UP_SHIFT_FUEL_CUT,            // 쉬프트업: 퓨얼컷 및 실린더 지연 시작
  S_UP_SHIFT_CYLINDER_ACTIVE,     // 쉬프트업: 실린더 동작
  S_DOWN_SHIFT_FUEL_CUT,          // 쉬프트다운: 퓨얼컷 및 실린더 동작
  S_IDLE_UP_SHIFT_ACTIVE,         // 공회전 쉬프트업: 실린더 동작
  S_IDLE_DOWN_SHIFT_ACTIVE        // 공회전 쉬프트다운: 실린더 동작
};
ShiftState currentShiftState = S_IDLE; // 현재 변속 상태 저장 변수

// --- Button 구조체 및 상태 변수 ---
struct Button {
  const byte pin;
  bool lastReading;
  bool debouncedState;
  unsigned long lastDebounceTime;

  // Constructor
  Button(byte p) : pin(p), lastReading(HIGH), debouncedState(HIGH), lastDebounceTime(0) {}
};

Button gearResetButton(gearResetButtonPin);
Button shiftUpButton(shiftUpButtonPin);
Button shiftUpIdleButton(shiftUpIdleButtonPin);
Button shiftDownButton(shiftDownButtonPin);
Button shiftDownIdleButton(shiftDownIdleButtonPin);

// --- 전역 변수 ---
int gearSequence[5] = {1, 0, 2, 3, 4}; // 현재 코드에서는 사용 안함
int gearIdx = 1;

// 아날로그 입력 값
unsigned int fuelCutTime    = 0;
unsigned int cylinderTime   = 0;
unsigned int cylinderDelay  = 0;

// 타이머 관련 변수
unsigned long stateChangeTime = 0; // 상태가 변경된 시간 기록
unsigned long lastLcdUpdateTime = 0;
const unsigned int updateLCDInterval = 500;

// 리어윙 관련 변수
int speedValue = 0;
int speedThreshold = 60;
unsigned long wingMoveStartTime = 0;
const unsigned long wingMoveDuration = 1000;
bool wingIsUp = false;
bool wingMoving = false;
bool wingDirectionUp = false;


// --- 함수 선언 ---
void updateShiftController();
bool wasPressed(Button &btn);


void setup() {
  Serial.begin(9600);
  
  // LCD 초기화
  lcd.begin();
  lcd.backlight();

  // 출력 핀 설정
  pinMode(fuelCutPin, OUTPUT);
  digitalWrite(fuelCutPin, HIGH); // 평소에는 전류 흐름 ON
  pinMode(shiftUpPin, OUTPUT);
  pinMode(shiftDownPin, OUTPUT);
  pinMode(l298n2_IN2, OUTPUT);
  pinMode(l298n2_IN4, OUTPUT);
  pinMode(wingMotorPinA, OUTPUT);
  pinMode(wingMotorPinB, OUTPUT);
  pinMode(wingMotorEN, OUTPUT);
  digitalWrite(wingMotorEN, HIGH);
  
  // 입력 핀 설정
  pinMode(gearResetButton.pin,     INPUT_PULLUP);
  pinMode(shiftUpButton.pin,       INPUT_PULLUP);
  pinMode(shiftUpIdleButton.pin,   INPUT_PULLUP);
  pinMode(shiftDownButton.pin,     INPUT_PULLUP);
  pinMode(shiftDownIdleButton.pin, INPUT_PULLUP);

  stopCylinders();
}

void loop() {
  unsigned long currentTime = millis();

  // 가변저항 값 읽기
  fuelCutTime   = analogRead(A0);
  cylinderTime  = analogRead(A1);
  cylinderDelay = analogRead(A2);
  
  if (Serial.available() > 0) {
    speedValue = Serial.parseInt();
    controlRearWing(speedValue);
    Serial.print("Input Speed: ");
    Serial.println(speedValue);
  }

  // 기어 리셋 버튼 처리
  if (wasPressed(gearResetButton)) {
    gearIdx = 1;
    Serial.println("Gear Reset: 1단으로 변경됨");
  }

  // 상태 머신 업데이트
  updateShiftController();

  // 리어윙 모터 업데이트
  updateWingMotor();

  // LCD 업데이트
  if (currentTime - lastLcdUpdateTime >= updateLCDInterval) {
    updateLCD();
    lastLcdUpdateTime = currentTime;
  }
}

// 중복 동작 방지를 위해 상태머신 도입
void updateShiftController() {
  unsigned long currentTime = millis();

  switch (currentShiftState) {
    case S_IDLE:
      // 쉬프트업 버튼 감지
      if (wasPressed(shiftUpButton) && gearIdx < 4) {
        Serial.println("ShiftUp: Fuel Cut 시작");
        digitalWrite(fuelCutPin, LOW); // 퓨얼컷 시작
        stopCylinders();
        stateChangeTime = currentTime;
        currentShiftState = S_UP_SHIFT_FUEL_CUT;
      }
      // 쉬프트다운 버튼 감지
      else if (wasPressed(shiftDownButton) && gearIdx > 0) {
        Serial.println("ShiftDown: Fuel Cut 및 실린더 동작 시작");
        digitalWrite(fuelCutPin, LOW); // 퓨얼컷 시작
        shiftCylinderDown();
        stateChangeTime = currentTime;
        currentShiftState = S_DOWN_SHIFT_FUEL_CUT;
      }
      // 공회전 쉬프트업 버튼 감지
      else if (wasPressed(shiftUpIdleButton) && gearIdx < 4) {
        Serial.println("Idle ShiftUp: 실린더 동작 시작");
        shiftCylinderUp();
        stateChangeTime = currentTime;
        currentShiftState = S_IDLE_UP_SHIFT_ACTIVE;
      }
      // 공회전 쉬프트다운 버튼 감지
      else if (wasPressed(shiftDownIdleButton) && gearIdx > 0) {
        Serial.println("Idle ShiftDown: 실린더 동작 시작");
        shiftCylinderDown();
        stateChangeTime = currentTime;
        currentShiftState = S_IDLE_DOWN_SHIFT_ACTIVE;
      }
      break;

    case S_UP_SHIFT_FUEL_CUT:
      // 실린더 딜레이 시간이 경과하면
      if (currentTime - stateChangeTime >= cylinderDelay) {
        Serial.println("ShiftUp: 실린더 동작 시작");
        shiftCylinderUp();
        stateChangeTime = currentTime;
        currentShiftState = S_UP_SHIFT_CYLINDER_ACTIVE;
      }
      // (퓨얼컷은 다음 상태에서도 계속 유지)
      break;

    case S_UP_SHIFT_CYLINDER_ACTIVE:
      // 실린더 동작 시간이 경과하면
      if (currentTime - stateChangeTime >= cylinderTime) {
        Serial.println("ShiftUp: 완료, 기어 증가");
        stopCylinders();
        digitalWrite(fuelCutPin, HIGH); // 퓨얼컷 종료
        if (gearIdx < 4) gearIdx++;
        currentShiftState = S_IDLE;
      }
      break;

    case S_DOWN_SHIFT_FUEL_CUT:
      if (currentTime - stateChangeTime >= cylinderTime) {
        Serial.println("ShiftDown: 완료, 기어 감소");
        stopCylinders();
        digitalWrite(fuelCutPin, HIGH); // 퓨얼컷 종료
        if (gearIdx > 0) gearIdx--;
        currentShiftState = S_IDLE;
      }
      break;

    case S_IDLE_UP_SHIFT_ACTIVE:
      // 실린더 동작 시간이 경과하면
      if (currentTime - stateChangeTime >= cylinderTime) {
        Serial.println("Idle ShiftUp: 완료, 기어 증가");
        stopCylinders();
        if (gearIdx < 4) gearIdx++;
        currentShiftState = S_IDLE;
      }
      break;

    case S_IDLE_DOWN_SHIFT_ACTIVE:
      // 실린더 동작 시간이 경과하면
      if (currentTime - stateChangeTime >= cylinderTime) {
        Serial.println("Idle ShiftDown: 완료, 기어 감소");
        stopCylinders();
        if (gearIdx > 0) gearIdx--;
        currentShiftState = S_IDLE;
      }
      break;
  }
}

// 버튼이 눌렸는지(Falling Edge) 감지
bool wasPressed(Button &btn) {
  bool oldDebouncedState = btn.debouncedState;
  bool reading = digitalRead(btn.pin);

  if (reading != btn.lastReading) {
    btn.lastDebounceTime = millis();
  }
  
  if ((millis() - btn.lastDebounceTime) > DEBOUNCE_DELAY) {
    btn.debouncedState = reading;
  }
  
  btn.lastReading = reading;
  
  return (oldDebouncedState == HIGH && btn.debouncedState == LOW);
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FuelCut:");
  lcd.print(fuelCutTime);
  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(cylinderTime);
  lcd.setCursor(7,1);
  lcd.print("D:");
  lcd.print(cylinderDelay);
  lcd.setCursor(13,1);
  lcd.print("G:");
  lcd.print(gearIdx);
}

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
  digitalWrite(l298n2_IN2,LOW);
  Serial.println("실린더 UP");
}

void shiftCylinderDown() {
  digitalWrite(shiftDownPin, HIGH);
  digitalWrite(l298n2_IN4, LOW);
  Serial.println("실린더 DOWN");
}

void stopCylinders() {
  digitalWrite(shiftUpPin, LOW);
  digitalWrite(shiftDownPin, LOW);
  digitalWrite(l298n2_IN2, LOW);
  digitalWrite(l298n2_IN4, LOW);
  Serial.println("실린더 STOP");
}
