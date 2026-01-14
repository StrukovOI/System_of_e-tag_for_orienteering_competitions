#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>

// Пины для Arduino Nano
#define RST_PIN 9
#define SS_PIN 10
#define BUZZER_PIN 2
#define BATTERY_PIN A7      // Пин для измерения напряжения аккумулятора
#define CHARGE_PIN 3        // Пин для определения статуса зарядки
#define FULL_PIN 4          // Пин для определения полной зарядки

// Объекты компонентов
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
RTC_DS3231 rtc;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

struct Timestamp {
  uint32_t data;
  byte checksum;
};

// Переменные для управления
bool rtcAvailable = false;
unsigned long lastTagTime = 0;
unsigned long displayOffTime = 0;
const unsigned long TAG_COOLDOWN = 500;
const unsigned long DISPLAY_TIMEOUT = 3000;
bool displayOn = false;
bool tagProcessed = false;

// Переменные для имитации разряда батареи
float simulatedBatteryPercent = 73.0;  // Начинаем с 100%
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 3600000;  // 1 час в миллисекундах (1000*60*60)
const float BATTERY_DISCHARGE_RATE = 3.0;  // 2% в час

// Калибровка батареи
const float REF_VOLTAGE = 5.0;
const float R1 = 10000.0;
const float R2 = 10000.0;
const float VOLTAGE_DIVIDER = R2 / (R1 + R2);
const float MAX_BATTERY_VOLTAGE = 4.2;
const float MIN_BATTERY_VOLTAGE = 3.2;

void setup() {
  Serial.begin(115200);
  
  // Инициализация пина пищалки
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Настройка пина батареи
  pinMode(BATTERY_PIN, INPUT);
  
  // Настройка пинов статуса зарядки
  pinMode(CHARGE_PIN, INPUT_PULLUP);
  pinMode(FULL_PIN, INPUT_PULLUP);
  
  // Инициализация дисплея
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("Display not found!"));
    }
  }
  
  display.clearDisplay();
  display.display();
  displayOn = false;
  
  // Инициализация RTC
  if(!rtc.begin()) {
    rtcAvailable = false;
  } else {
    rtcAvailable = true;
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }
  
  // Инициализация RFID
  SPI.begin();
  mfrc522.PCD_Init();
  delay(4);
  
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  // Тестовый звук при запуске
  playStartupSound();
  
  // Инициализация времени для батареи
  lastBatteryUpdate = millis();
  
  // Показываем стартовый экран на 2 секунды
  showStartupScreen();
  displayOn = true;
  displayOffTime = millis() + 4000;
  
  Serial.println(F("System ready"));
  Serial.println(F("Waiting for tags..."));
}

void loop() {
  // Обновляем состояние батареи (имитация разряда)
  updateBatterySimulation();
  
  // Проверяем, нужно ли выключить экран
  if (displayOn && millis() >= displayOffTime) {
    display.clearDisplay();
    display.display();
    displayOn = false;
  }
  
  // Проверяем таймаут между метками
  if (tagProcessed && (millis() - lastTagTime < TAG_COOLDOWN)) {
    delay(50);
    return;
  }
  tagProcessed = false;
  
  // Проверяем наличие метки
  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }
  
  // Пытаемся прочитать метку
  if (!mfrc522.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }
  
  // Метка обнаружена
  lastTagTime = millis();
  tagProcessed = true;
  
  // Включаем экран
  displayOn = true;
  displayOffTime = millis() + DISPLAY_TIMEOUT;
  
  showMessage("TAG DETECTED", "PROCESSING...");
  
  // Выводим UID метки
  Serial.print(F("UID:"));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();
  
  // Получаем текущее время
  DateTime now;
  if (rtcAvailable) {
    now = rtc.now();
  } else {
    now = DateTime(F(__DATE__), F(__TIME__));
  }
  
  uint32_t timestamp = now.unixtime();
  
  // Выводим время в Serial
  Serial.print(F("Time: "));
  printDateTime(now);
  Serial.print(F(" (Unix: "));
  Serial.print(timestamp);
  Serial.println(')');
  
  // Пытаемся записать время на метку
  bool writeSuccess = false;
  
  // Проверяем тип карты
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&  
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println(F("❌ Unsupported card type!"));
    showMessage("ERROR", "UNSUPPORTED CARD");
    playErrorSound();
  } else {
    // Пытаемся записать
    writeSuccess = writeTimestamp(timestamp);
  }
  
  // Обработка результата
  if (writeSuccess) {
    Serial.println(F("✅ Time saved to tag"));
    showMessageWithBattery("SUCCESS", "TIME SAVED");
    playSuccessSound();
  } else {
    Serial.println(F("❌ Write error"));
    showMessage("ERROR", "WRITE FAILED");
    playErrorSound();
  }
  
  // Быстрое завершение работы с меткой
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  // Короткая пауза перед возвратом к ожиданию
  delay(200);
}

// Функция имитации разряда батареи
void updateBatterySimulation() {
  unsigned long currentTime = millis();
  
  // Проверяем, прошел ли час (3600000 мс)
  if (currentTime - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL) {
    // Уменьшаем заряд на 2%
    simulatedBatteryPercent -= BATTERY_DISCHARGE_RATE;
    
    // Не даем упасть ниже 0%
    if (simulatedBatteryPercent < 0) {
      simulatedBatteryPercent = 0;
    }
    
    // Обновляем время последнего изменения
    lastBatteryUpdate = currentTime;
    
    // Выводим информацию в Serial для отладки
    Serial.print(F("Battery update: "));
    Serial.print(simulatedBatteryPercent);
    Serial.println(F("%"));
  }
}

// Функция измерения напряжения аккумулятора
float readBatteryVoltage() {
  int sensorValue = analogRead(BATTERY_PIN);
  float voltage = sensorValue * (REF_VOLTAGE / 1023.0);
  voltage = voltage / VOLTAGE_DIVIDER;
  return voltage;
}

// Функция расчета уровня заряда в процентах (на основе напряжения)
int getRealBatteryPercent() {
  float voltage = readBatteryVoltage();
  
  if (voltage >= MAX_BATTERY_VOLTAGE) {
    return 100;
  } else if (voltage <= MIN_BATTERY_VOLTAGE) {
    return 0;
  }
  
  int percent = (voltage - MIN_BATTERY_VOLTAGE) * 100 / (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE);
  return percent;
}

// Функция получения имитированного заряда батареи
int getSimulatedBatteryPercent() {
  return (int)simulatedBatteryPercent;
}

// Функция определения статуса зарядки
int getChargingStatus() {
  // 0 = не заряжается
  // 1 = идет зарядка
  // 2 = зарядка завершена
  
  if (digitalRead(FULL_PIN) == LOW) {
    return 2;
  }
  
  if (digitalRead(CHARGE_PIN) == LOW) {
    return 1;
  }
  
  return 0;
}

// Функция получения отображаемого процента батареи
int getDisplayBatteryPercent() {
  int chargingStatus = getChargingStatus();
  
  // Если идет зарядка или батарея полная, используем реальный процент
  if (chargingStatus == 1 || chargingStatus == 2) {
    return getRealBatteryPercent();
  }
  
  // В остальных случаях используем имитированный процент
  // Но не даем ему быть выше реального (для реалистичности)
  int realPercent = getRealBatteryPercent();
  int simulatedPercent = getSimulatedBatteryPercent();
  
  if (simulatedPercent < realPercent) {
    return simulatedPercent;
  } else {
    return realPercent;
  }
}

// Функция записи времени на метку
bool writeTimestamp(uint32_t timestamp) {
  byte blockAddr = 4;
  
  Timestamp newTs;
  newTs.data = timestamp;
  newTs.checksum = calculateChecksum(timestamp);
  
  return writeBlock(blockAddr, newTs);
}

Timestamp readBlock(byte blockAddr) {
  Timestamp result;
  result.data = 0xFFFFFFFF;
  
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = sizeof(buffer);
  
  status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A,
    blockAddr, &key, &(mfrc522.uid)
  );
  
  if (status != MFRC522::STATUS_OK) {
    return result;
  }
  
  status = mfrc522.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    return result;
  }
  
  result.data = (uint32_t)buffer[0] << 24 | 
                (uint32_t)buffer[1] << 16 | 
                (uint32_t)buffer[2] << 8 | 
                buffer[3];
  result.checksum = buffer[4];
  
  return result;
}

bool writeBlock(byte blockAddr, Timestamp ts) {
  MFRC522::StatusCode status;
  byte buffer[16] = {0};
  
  buffer[0] = (ts.data >> 24) & 0xFF;
  buffer[1] = (ts.data >> 16) & 0xFF;
  buffer[2] = (ts.data >> 8) & 0xFF;
  buffer[3] = ts.data & 0xFF;
  buffer[4] = ts.checksum;
  
  status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A,
    blockAddr, &key, &(mfrc522.uid)
  );
  
  if (status != MFRC522::STATUS_OK) {
    return false;
  }
  
  status = mfrc522.MIFARE_Write(blockAddr, buffer, 16);
  return status == MFRC522::STATUS_OK;
}

byte calculateChecksum(uint32_t data) {
  byte checksum = 0;
  for (byte i = 0; i < 4; i++) {
    checksum ^= (data >> (i * 8)) & 0xFF;
  }
  return checksum;
}

// Функция отображения двухстрочного сообщения
void showMessage(const char* line1, const char* line2) {
  if (!displayOn) {
    display.clearDisplay();
    display.display();
  }
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  int16_t x1 = (128 - strlen(line1) * 12) / 2;
  if (x1 < 0) x1 = 0;
  display.setCursor(x1, 10);
  display.println(line1);
  
  display.setTextSize(1);
  int16_t x2 = (128 - strlen(line2) * 6) / 2;
  if (x2 < 0) x2 = 0;
  display.setCursor(x2, 40);
  display.println(line2);
  
  display.display();
}

// Функция отображения сообщения с индикатором батареи
void showMessageWithBattery(const char* line1, const char* line2) {
  if (!displayOn) {
    display.clearDisplay();
    display.display();
  }
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  int16_t x1 = (128 - strlen(line1) * 12) / 2;
  if (x1 < 0) x1 = 0;
  display.setCursor(x1, 5);
  display.println(line1);
  
  display.setTextSize(1);
  int16_t x2 = (128 - strlen(line2) * 6) / 2;
  if (x2 < 0) x2 = 0;
  display.setCursor(x2, 30);
  display.println(line2);
  
  // Индикатор батареи и статус зарядки
  int displayPercent = getDisplayBatteryPercent();
  float voltage = readBatteryVoltage();
  int chargingStatus = getChargingStatus();
  
  // Рисуем иконку батареи
  drawBatteryIcon(40, 45, displayPercent, chargingStatus);
  
  // Текстовый индикатор
  display.setCursor(75, 45);
  display.print(displayPercent);
  display.print("%");
  
  display.setCursor(75, 55);
  if (chargingStatus == 1) {
    display.print("CHRG ");
    display.print(voltage, 1);
    display.print("V");
  } else if (chargingStatus == 2) {
    display.print("FULL ");
    display.print(voltage, 1);
    display.print("V");
  } else {
    // Показываем статус разряда
    display.print("DIS ");
    display.print(voltage, 1);
    display.print("V");
  }
  
  display.display();
}

// Функция рисования иконки батареи с индикацией зарядки
void drawBatteryIcon(int x, int y, int percent, int chargingStatus) {
  // Контур батареи
  display.drawRect(x, y, 30, 12, SSD1306_WHITE);
  
  // Положительный контакт
  display.fillRect(x + 30, y + 3, 3, 6, SSD1306_WHITE);
  
  // Уровень заряда
  int fillWidth = map(percent, 0, 100, 0, 26);
  display.fillRect(x + 2, y + 2, fillWidth, 8, SSD1306_WHITE);
  
  // Индикация зарядки
  if (chargingStatus == 1) {
    // Рисуем молнию (символ зарядки)
    display.setCursor(x + 12, y + 2);
    display.setTextSize(1);
    display.print("+");
  } else if (chargingStatus == 2) {
    // Полная зарядка - галочка
    display.setCursor(x + 12, y + 2);
    display.setTextSize(1);
    display.print("✓");
  }
}

// Функция отображения стартового экрана
void showStartupScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(20, 0);
  display.println(F("BASE"));
  display.setCursor(10, 20);
  display.println(F("STATION"));
  
  display.setTextSize(1);
  display.setCursor(10, 40);
  display.print(F("RTC: "));
  if (rtcAvailable) {
    display.println(F("OK"));
  } else {
    display.println(F("NO"));
  }
  
  display.setCursor(10, 50);
  int chargingStatus = getChargingStatus();
  int batteryPercent = getDisplayBatteryPercent();
  
  if (chargingStatus == 1) {
    display.println(F("BAT: CHARGING"));
  } else if (chargingStatus == 2) {
    display.println(F("BAT: FULL"));
  } else {
    display.print(F("BAT: "));
    display.print(batteryPercent);
    display.println(F("% (SIM)"));
  }
  
  display.display();
}

void printDateTime(DateTime dt) {
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  Serial.print(dt.day(), DEC);
  Serial.print(' ');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
  Serial.print(dt.second(), DEC);
  Serial.println();
}

// Функции звука
void playStartupSound() {
  tone(BUZZER_PIN, 1500, 100);
  delay(120);
  tone(BUZZER_PIN, 2000, 100);
  delay(120);
  noTone(BUZZER_PIN);
}

void playSuccessSound() {
  tone(BUZZER_PIN, 2000, 80);
  delay(100);
  noTone(BUZZER_PIN);
  delay(50);
  tone(BUZZER_PIN, 2500, 80);
  delay(100);
  noTone(BUZZER_PIN);
}

void playErrorSound() {
  tone(BUZZER_PIN, 400, 200);
  delay(250);
  noTone(BUZZER_PIN);
}