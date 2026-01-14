#include <SPI.h>
#include <MFRC522.h>

// Пины для Arduino Nano
#define RST_PIN 9
#define SS_PIN 10

// Объекты компонентов
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// Структура для хранения временной метки
struct Timestamp {
  uint32_t data;
  byte checksum;
};

// Проверенный алгоритм для преобразования Unix timestamp в дату
void unixTimeToDateTime(uint32_t unixtime, uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute, uint8_t &second) {
  // Константы
  const uint32_t SECONDS_PER_DAY = 86400UL;
  const uint16_t EPOCH_YEAR = 1970;
  
  // Массив дней в месяце для обычного и високосного года
  const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  
  // Вычисляем секунды, минуты, часы
  second = unixtime % 60;
  unixtime /= 60;
  minute = unixtime % 60;
  unixtime /= 60;
  hour = unixtime % 24;
  unixtime /= 24;
  
  // Текущий год
  year = EPOCH_YEAR;
  
  // Определяем год
  while (1) {
    uint16_t daysInYear = 365;
    if (isLeapYear(year)) {
      daysInYear = 366;
    }
    
    if (unixtime < daysInYear) {
      break;
    }
    
    unixtime -= daysInYear;
    year++;
  }
  
  // Определяем месяц
  month = 0;
  while (1) {
    uint8_t daysInCurrentMonth = daysInMonth[month];
    
    // Корректируем февраль для високосного года
    if (month == 1 && isLeapYear(year)) {
      daysInCurrentMonth = 29;
    }
    
    if (unixtime < daysInCurrentMonth) {
      break;
    }
    
    unixtime -= daysInCurrentMonth;
    month++;
  }
  
  // Месяц в диапазоне 1-12, а не 0-11
  month++;
  
  // День месяца (дни начинаются с 1)
  day = unixtime + 1;
}

// Функция для определения високосного года
bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Ждем инициализации Serial
  }
  
  delay(1000);
  
  Serial.println(F("\n=== MASTER STATION - ORIENTEERING ==="));
  Serial.println(F("Place tag to read all timestamps...\n"));
  
  // Инициализация RFID
  SPI.begin();
  mfrc522.PCD_Init();
  delay(10);
  
  // Выводим версию прошивки RFID
  byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("MFRC522 Firmware Version: 0x"));
  Serial.print(version, HEX);
  if (version == 0x92) {
    Serial.println(F(" = v2.0"));
  } else if (version == 0x91 || version == 0x90) {
    Serial.println(F(" = v1.0"));
  } else {
    Serial.println(F(" (unknown)"));
  }
  
  // Инициализация ключа (по умолчанию 0xFF...)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  Serial.println(F("\nSystem ready. Waiting for tags...\n"));
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
  Serial.println(F("\n======================================"));
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
  
  // Читаем все блоки с временными метками
  readAllTimestamps();
  
  // Завершаем работу с меткой
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  Serial.println(F("======================================"));
  Serial.println(F("\nReady for next tag...\n"));
  delay(1000);
}

// Функция чтения всех временных меток
void readAllTimestamps() {
  bool foundTimestamps = false;
  int timestampCount = 0;
  
  Serial.println(F("\nScanning for timestamps..."));
  
  // Проходим по всем секторам (0-15)
  for (byte sector = 0; sector < 16; sector++) {
    // Проходим по всем блокам в секторе (0-3)
    for (byte block = 0; block < 4; block++) {
      byte blockAddr = sector * 4 + block;
      
      // Пропускаем трейлерные блоки (последний блок в каждом секторе)
      if (block == 3) {
        continue;
      }
      
      // Пропускаем блоки 0-3 (обычно служебные данные производителя)
      if (blockAddr < 4) {
        continue;
      }
      
      // Читаем блок
      Timestamp ts = readBlock(blockAddr);
      
      // Проверяем, содержит ли блок данные (не 0xFFFFFFFF и не 0)
      if (ts.data != 0xFFFFFFFF && ts.data != 0) {
        // Проверяем контрольную сумму
        byte calculatedChecksum = calculateChecksum(ts.data);
        
        if (ts.checksum == calculatedChecksum) {
          if (!foundTimestamps) {
            Serial.println(F("\n--- TIMESTAMPS FOUND ---"));
            foundTimestamps = true;
          }
          
          timestampCount++;
          
          // Выводим информацию о блоке
          Serial.print(F("#"));
          if (timestampCount < 10) Serial.print("0");
          Serial.print(timestampCount);
          Serial.print(F(" - Block "));
          if (blockAddr < 10) Serial.print(" ");
          Serial.print(blockAddr);
          Serial.print(F(": "));
          
          // Преобразуем timestamp в читаемую дату и время
          printUnixTime(ts.data);
        } else {
          Serial.print(F("Block "));
          if (blockAddr < 10) Serial.print(" ");
          Serial.print(blockAddr);
          Serial.println(F(": INVALID CHECKSUM - corrupted data"));
        }
      }
    }
  }
  
  if (!foundTimestamps) {
    Serial.println(F("\nNo timestamps found on this tag."));
    Serial.println(F("Tag might be empty, corrupted, or written with different keys."));
  } else {
    Serial.print(F("\nTotal timestamps found: "));
    Serial.println(timestampCount);
  }
}

// Функция чтения блока
Timestamp readBlock(byte blockAddr) {
  Timestamp result;
  result.data = 0xFFFFFFFF;
  
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = sizeof(buffer);
  
  // Аутентификация
  status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A,
    blockAddr, &key, &(mfrc522.uid)
  );
  
  if (status != MFRC522::STATUS_OK) {
    // Не выводим ошибку для каждого блока, чтобы не засорять вывод
    return result;
  }
  
  // Чтение
  status = mfrc522.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    return result;
  }
  
  // Преобразуем данные из буфера (первые 4 байта - timestamp, 5-й байт - checksum)
  result.data = (uint32_t)buffer[0] << 24 | 
                (uint32_t)buffer[1] << 16 | 
                (uint32_t)buffer[2] << 8 | 
                buffer[3];
  result.checksum = buffer[4];
  
  return result;
}

// Функция расчета контрольной суммы
byte calculateChecksum(uint32_t data) {
  byte checksum = 0;
  for (byte i = 0; i < 4; i++) {
    checksum ^= (data >> (i * 8)) & 0xFF;
  }
  return checksum;
}

// Функция преобразования Unix timestamp в читаемую дату и время
void printUnixTime(uint32_t unixtime) {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  
  // Используем проверенный алгоритм преобразования
  unixTimeToDateTime(unixtime, year, month, day, hour, minute, second);
  
  // Выводим Unix timestamp
  Serial.print(F("Unix: "));
  Serial.print(unixtime);
  Serial.print(F(" -> "));
  
  // Выводим дату и время в формате YYYY-MM-DD HH:MM:SS
  Serial.print(year);
  Serial.print("-");
  if (month < 10) Serial.print("0");
  Serial.print(month);
  Serial.print("-");
  if (day < 10) Serial.print("0");
  Serial.print(day);
  Serial.print(" ");
  
  if (hour < 10) Serial.print("0");
  Serial.print(hour);
  Serial.print(":");
  if (minute < 10) Serial.print("0");
  Serial.print(minute);
  Serial.print(":");
  if (second < 10) Serial.print("0");
  Serial.println(second);
}

// Альтернативный вариант: если вы используете библиотеку RTClib на базовой станции,
// то можно сравнить Unix timestamp с текущим временем для проверки
void printTimeInfo(uint32_t unixtime) {
  // Выводим дополнительную информацию для отладки
  uint32_t currentTime = getCurrentUnixTime(); // Нужно реализовать эту функцию
  
  Serial.print(F("Unix: "));
  Serial.print(unixtime);
  Serial.print(F(" -> "));
  
  if (unixtime < 1000000000) {
    Serial.println(F("ERROR: Timestamp too small (before 2001)"));
  } else if (unixtime > 2000000000) {
    Serial.println(F("ERROR: Timestamp too large (after 2033)"));
  } else {
    // Преобразуем и выводим
    printUnixTime(unixtime);
  }
}

// Функция для получения текущего Unix времени (для отладки)
uint32_t getCurrentUnixTime() {
  // Эта функция нужна только для отладки
  // В реальной мастер-станции она не используется
  // Можно закомментировать или удалить
  return 0;
}