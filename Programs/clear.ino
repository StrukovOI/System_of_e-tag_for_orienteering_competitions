#include <SPI.h>
#include <MFRC522.h>

// Пины для Arduino Nano
#define RST_PIN 9
#define SS_PIN 10

// Объекты компонентов
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// Глобальная переменная для подтверждения
bool confirmationReceived = false;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Ждем инициализации Serial
  }
  
  delay(1000);
  
  Serial.println(F("\n=== RFID TAG CLEANER ==="));
  Serial.println(F("This program will erase ALL timestamps from a tag"));
  Serial.println(F("WARNING: This action cannot be undone!"));
  Serial.println(F(""));
  
  // Инициализация RFID
  SPI.begin();
  mfrc522.PCD_Init();
  delay(10);
  
  // Инициализация ключа (по умолчанию 0xFF...)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  Serial.println(F("System ready."));
  Serial.println(F("Place a tag on the reader to begin..."));
  Serial.println(F(""));
}

void loop() {
  // Сбрасываем состояние RFID
  mfrc522.PCD_Init();
  
  // Проверяем наличие новой метки
  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(100);
    return;
  }
  
  // Пытаемся прочитать метку
  if (!mfrc522.PICC_ReadCardSerial()) {
    delay(100);
    return;
  }
  
  // Метка обнаружена
  Serial.println(F("======================================"));
  Serial.println(F("TAG DETECTED"));
  
  // Выводим UID метки
  Serial.print(F("UID:"));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();
  
  // Определяем тип карты
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.print(F("Card type: "));
  Serial.println(mfrc522.PICC_GetTypeName(piccType));
  
  // Проверяем, поддерживается ли тип карты
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&  
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println(F("ERROR: This card type is not supported."));
    Serial.println(F("Only MIFARE Classic cards are supported."));
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }
  
  // Запрашиваем подтверждение
  Serial.println(F("\nWARNING: This will erase ALL data from the tag!"));
  Serial.println(F("Type 'YES' (in uppercase) to confirm, or anything else to cancel."));
  
  // Ожидаем ввода пользователя
  waitForUserConfirmation();
  
  // Если пользователь подтвердил, очищаем метку
  if (confirmationReceived) {
    eraseTag();
  } else {
    Serial.println(F("Operation cancelled."));
  }
  
  // Сбрасываем флаг подтверждения
  confirmationReceived = false;
  
  // Завершаем работу с меткой
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  Serial.println(F("======================================"));
  Serial.println(F("\nReady for next tag...\n"));
  delay(2000);
}

// Ожидание подтверждения от пользователя
void waitForUserConfirmation() {
  String input = "";
  
  // Ждем ввода от пользователя
  while (Serial.available() == 0) {
    delay(100);
  }
  
  // Читаем ввод
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      break;
    }
    input += c;
    delay(10);
  }
  
  // Очищаем буфер Serial
  while (Serial.available() > 0) {
    Serial.read();
  }
  
  // Проверяем ввод
  if (input.equals("YES")) {
    Serial.println(F("Confirmation received. Erasing tag..."));
    confirmationReceived = true;
  } else {
    Serial.println(F("Confirmation NOT received. Cancelling..."));
    confirmationReceived = false;
  }
}

// Функция очистки всей метки
void eraseTag() {
  int erasedBlocks = 0;
  int totalBlocks = 0;
  
  Serial.println(F("\nStarting erase process..."));
  
  // Проходим по всем секторам (0-15)
  for (byte sector = 0; sector < 16; sector++) {
    // Проходим по всем блокам в секторе (0-3)
    for (byte block = 0; block < 4; block++) {
      byte blockAddr = sector * 4 + block;
      
      // Пропускаем трейлерные блоки (последний блок в каждом секторе)
      // Трейлерные блоки содержат ключи доступа, их нельзя трогать!
      if (block == 3) {
        continue;
      }
      
      // Пропускаем блоки 0-3 (служебные данные производителя)
      if (blockAddr < 4) {
        continue;
      }
      
      totalBlocks++;
      
      // Очищаем блок
      if (eraseBlock(blockAddr)) {
        erasedBlocks++;
        Serial.print(F("Block "));
        if (blockAddr < 10) Serial.print(" ");
        Serial.print(blockAddr);
        Serial.println(F(": ERASED"));
      } else {
        Serial.print(F("Block "));
        if (blockAddr < 10) Serial.print(" ");
        Serial.print(blockAddr);
        Serial.println(F(": FAILED"));
      }
      
      delay(50); // Небольшая задержка между блоками
    }
  }
  
  Serial.println(F("\n--- ERASE COMPLETE ---"));
  Serial.print(F("Successfully erased: "));
  Serial.print(erasedBlocks);
  Serial.print(F(" of "));
  Serial.print(totalBlocks);
  Serial.println(F(" blocks"));
  
  if (erasedBlocks == totalBlocks) {
    Serial.println(F("Tag has been completely erased."));
    Serial.println(F("The tag is now ready for reuse."));
  } else {
    Serial.println(F("Some blocks could not be erased."));
    Serial.println(F("The tag may be partially cleaned."));
  }
}

// Функция очистки одного блока
bool eraseBlock(byte blockAddr) {
  MFRC522::StatusCode status;
  
  // Создаем пустой блок данных (16 байт 0xFF)
  byte emptyBlock[16];
  for (int i = 0; i < 16; i++) {
    emptyBlock[i] = 0xFF;
  }
  
  // Для совместимости с нашей структурой временных меток:
  // Первые 4 байта = 0xFFFFFFFF (пустой timestamp)
  // 5-й байт = 0x00 (контрольная сумма для 0xFFFFFFFF)
  // Остальные байты = 0xFF
  
  emptyBlock[0] = 0xFF;
  emptyBlock[1] = 0xFF;
  emptyBlock[2] = 0xFF;
  emptyBlock[3] = 0xFF;
  emptyBlock[4] = 0x00; // Контрольная сумма для 0xFFFFFFFF
  
  // Аутентификация
  status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A,
    blockAddr, &key, &(mfrc522.uid)
  );
  
  if (status != MFRC522::STATUS_OK) {
    return false;
  }
  
  // Запись пустого блока
  status = mfrc522.MIFARE_Write(blockAddr, emptyBlock, 16);
  return status == MFRC522::STATUS_OK;
}

// Функция проверки контрольной суммы (для информации)
byte calculateChecksum(uint32_t data) {
  byte checksum = 0;
  for (byte i = 0; i < 4; i++) {
    checksum ^= (data >> (i * 8)) & 0xFF;
  }
  return checksum;
}