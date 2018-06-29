#include <OneWire.h>
#include <SPI.h>
#include <Wire.h>

#include <DallasTemperature.h>

#include <RealTimeClockDS1307.h>

#include <Adafruit_PWMServoDriver.h>

#include <MFRC522.h>

#include <Keypad.h>

#include <FastLED.h>

#include <SdFat.h>

#include <LiquidCrystal.h>


SdFat SD;
#define SD_CS_PIN 49

#define LCD_RS_PIN 47
#define LCD_EN_PIN 46
#define LCD_D4_PIN 45
#define LCD_D5_PIN 44
#define LCD_D6_PIN 43
#define LCD_D7_PIN 42
LiquidCrystal lcd(LCD_RS_PIN, LCD_EN_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);

#define KEYPAD_COLS 4
#define KEYPAD_ROWS 4
const char KEYPAD_KEYS[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'},
};
const byte KEYPAD_ROW_PINS[] = {32, 34, 36, 38};
const byte KEYPAD_COL_PINS[] = {33, 35, 37, 39};
Keypad keypad = Keypad(makeKeymap(KEYPAD_KEYS), KEYPAD_ROW_PINS, KEYPAD_COL_PINS, KEYPAD_ROWS, KEYPAD_COLS);

#define COIN_PIN 2
#define COIN_DELAY 150

#define ONE_WIRE_PIN 3
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature temperatureSensor(&oneWire);

#define MFRC522_RS_PIN 48
#define MFRC522_CS_PIN 53
MFRC522 mfrc522(MFRC522_CS_PIN, MFRC522_RS_PIN);

#define CODE_PIN 6

#define I2C_TIMEOUT 0
#define I2C_NOINTERRUPT 0
#define I2C_FASTMODE 0
#define SDA_PORT PORTB
#define SDA_PIN 5 //11
#define SCL_PORT PORTB
#define SCL_PIN 6 //12

#include <SoftI2CMaster.h>
#include <SoftWire.h>

int balance = 0;
volatile long long lastCoin = 0;
volatile int bounceCount = 0;

int coinAmount[6];
int changeServo[6];
int changeStock[6];

double temperature = 0;

#define MAX_PRODUCT_COUNT 50
#define MAX_SHELF_COUNT 10
int productCount = 0;
int productShelfCount[MAX_PRODUCT_COUNT];
int productPrice[MAX_PRODUCT_COUNT];
int productServoIDs[MAX_PRODUCT_COUNT][MAX_SHELF_COUNT];
int productServoBreakouts[MAX_PRODUCT_COUNT][MAX_SHELF_COUNT];
int productStocks[MAX_PRODUCT_COUNT][MAX_SHELF_COUNT];
char productName[MAX_PRODUCT_COUNT][20];

char dateBuffer[20] = {'_'};
int dateLength = 0;

long long int timeTimer = -10000;
long long int codeTimer = 0;
long long int temperatureTimer = -5000;
long long int productTimer = 0;

int productFirstDigit = -1;
int productID = -1;

#define SERVO_BREAKOUT_COUNT 1
Adafruit_PWMServoDriver servoBreakouts[SERVO_BREAKOUT_COUNT] = {Adafruit_PWMServoDriver(0x40)};

int menu = 0;
int lastMenu = 0;
int previousMenu = 0;

SdFile file;

void setup() {

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin();
  SPI.begin();

  i2c_init();

  lcd.begin(20, 4);

  if (RTC.isStopped()) {
    RTC.start();
  }

  temperatureSensor.begin();
  temperatureSensor.setResolution(12);
  temperatureSensor.setWaitForConversion(false);

  if (!SD.begin(SD_CS_PIN)) {
    lcd.print("NO SD CARD. INSERT AND RESET");
    while (true) {}
  }

  mfrc522.PCD_Init();

  for (int i = 0; i < SERVO_BREAKOUT_COUNT; i++) {
    servoBreakouts[i].begin();
    servoBreakouts[i].setPWMFreq(60);
  }
  //GO: 200, ~1080ms
  //STOP: 350

  temperatureSensor.begin();

  attachInterrupt(digitalPinToInterrupt(COIN_PIN), Coin, RISING);


  int bufferSize = 32;
  char sdBuffer[bufferSize];

  file.open("data/coins.txt", O_READ);
  for (int i = 0; i < 6; i++) {
    file.fgets(sdBuffer, bufferSize);
    sscanf(sdBuffer, "%d", &coinAmount[i]);
  }
  file.close();

  SD.chdir("products");
  SD.vwd() -> rewind();
  int id = 0;
  while (file.openNext(SD.vwd(), O_READ)) {
    if (file.isHidden() || id >= MAX_PRODUCT_COUNT) {
      file.close();
    } else {
      file.printName(&Serial);
      file.fgets(productName[id], 20);
      file.fgets(sdBuffer, bufferSize);
      sscanf(sdBuffer, "%d", &productPrice[id]);
      int trayID = 0;
      int n;
      while ((n = file.fgets(sdBuffer, bufferSize)) < bufferSize - 1 && n > 0 && trayID < MAX_SHELF_COUNT) {
        Serial.println(sdBuffer);
        sscanf(sdBuffer, "%d %d %d", &productServoBreakouts[id][trayID], &productServoIDs[id][trayID], &productStocks[id][trayID]);
        trayID++;
      }
      productShelfCount[id] = trayID;
      id++;
      file.close();
    }
  }
  productCount = id;
  SD.chdir("..");

  file.open("data/change.txt", O_READ);
  for (int i = 0; i < 6; i++) {
    file.fgets(sdBuffer, bufferSize);
    sscanf(sdBuffer, "%d %d", &changeServo[i], &changeStock[i]);
  }
  file.close();
  Serial.println(changeServo[4]);
}

void Coin() {
  Serial.println((double)lastCoin - millis());
  if (millis() > lastCoin + COIN_DELAY - 10 && millis() < lastCoin + COIN_DELAY + 10) {
    bounceCount++;
  } else {
    bounceCount = 1;
  }
  lastCoin = millis();
}

void dispenseProduct(int productWanted) {
  lcd.clear();
  if (balance >= productPrice[productWanted]) {
    int largestStockShelfID = 0;
    for (int i = 0; i < productShelfCount[productWanted]; i++) {
      if (productStocks[productWanted][i] > productStocks[productWanted][largestStockShelfID]) {
        largestStockShelfID = i;
      }
      Serial.println(productStocks[productWanted][i]);
    }
    balance -= productPrice[productWanted];
    servoBreakouts[productServoBreakouts[productWanted][largestStockShelfID]].setPin(productServoIDs[productWanted][largestStockShelfID], 200, false);
    delay(1100);
    servoBreakouts[productServoBreakouts[productWanted][largestStockShelfID]].setPin(productServoIDs[productWanted][largestStockShelfID], 0, false);
    if (balance > 0) {
      giveChange();
    }
  } else {
    previousMenu = menu == 1 ? 0 : menu;
    menu = 1;
    productID = productWanted;
    productTimer = millis();
  }
}

void giveChange() {
  for (int i = 5; i >= 0; i--) {
    while (balance >= coinAmount[i]) {
      balance -= coinAmount[i];
    }
  }
  balance = 0;
  menu = 0;
}

void selectProduct(int productDigit) {
  if (productFirstDigit == -1) {
    productFirstDigit = productDigit;
    productTimer = millis();
    previousMenu = menu;
    menu = 3;
  } else {
    if (10 * productFirstDigit + productDigit < productCount) {
      dispenseProduct(10 * productFirstDigit + productDigit);
    } else {
      menu = previousMenu;
    }
    productFirstDigit = -1;
  }
}

void loop() {
  if (lastMenu != menu) {
    lcd.clear();
  }
  lastMenu = menu;

  if (millis() > lastCoin + COIN_DELAY + 30 && bounceCount > 0) {
    if (bounceCount > 27) {
      balance += coinAmount[5];
    } else if (bounceCount > 22) {
      balance += coinAmount[4];
    } else if (bounceCount > 17) {
      balance += coinAmount[3];
    } else if (bounceCount > 12) {
      balance += coinAmount[2];
    } else if (bounceCount > 7) {
      balance += coinAmount[1];
    } else if (bounceCount > 2) {
      balance += coinAmount[0];
    }
    bounceCount = 0;
    if (balance > 0) {
      menu = 2;
    }
  }

  if (millis() > productTimer + 2500 && productFirstDigit > -1) {
    menu = 0;
    productFirstDigit = -1;
  }

  if (millis() > codeTimer + 3000) {
    digitalWrite(CODE_PIN, HIGH);
  }
  if (millis() > codeTimer + 3010) {
    digitalWrite(CODE_PIN, LOW);
    codeTimer = millis();
  }

  if (millis() > timeTimer + 10000) {
    RTC.readClock();
    dateLength = sprintf(dateBuffer, "%02d-%02d  %02d:%02d", RTC.getMonth(), RTC.getDay(), RTC.getHours(), RTC.getMinutes());
  }

  if (millis() > temperatureTimer + 5000) {
    temperatureSensor.requestTemperatures();
  }
  if (millis() > temperatureTimer + 5750) {
    temperature = temperatureSensor.getTempCByIndex(0);
    temperatureTimer = millis();
  }

  if (menu == 0) {
    lcd.setCursor(4, 1);
    for (int i = 0; i < dateLength; i++) {
      lcd.print(dateBuffer[i]);
    }

    lcd.setCursor(6, 2);
    lcd.print(temperature);
    lcd.print((char)0xDF);
    lcd.print("C");
  }

  if (menu == 1) {
    lcd.setCursor(10 - strlen(productName[productID]) / 2, 1);
    for (int i = 0; i < strlen(productName[productID]) - 1; i++) {
      lcd.print(productName[productID][i]);
    }
    if (millis() > productTimer + 2500) {
      menu = previousMenu;
    }
    if (productPrice[productID] >= 100) {
      lcd.setCursor(1, 2);
    } else if (productPrice[productID] >= 10) {
      lcd.setCursor(1, 2);
    } else {
      lcd.setCursor(2, 2);
    }
    lcd.print("Termek ara: ");
    lcd.print(productPrice[productID]);
    lcd.print(" Ft");
  }

  if (menu == 2) {
    if (balance >= 100) {
      lcd.setCursor(7, 1);
    } else if (balance >= 10) {
      lcd.setCursor(7, 1);
    } else {
      lcd.setCursor(8, 1);
    }
    if (balance > 0) {
      lcd.print(balance);
      lcd.print(" Ft");
    }
  }

  if (menu == 3) {
    lcd.setCursor(9, 1);
    lcd.print(productFirstDigit);
  }

  char key = keypad.getKey();
  if (key) {
    switch (key) {
      case 'D': giveChange(); break;
      case '0': selectProduct(0); break;
      case '1': selectProduct(1); break;
      case '2': selectProduct(2); break;
      case '3': selectProduct(3); break;
      case '4': selectProduct(4); break;
      case '5': selectProduct(5); break;
      case '6': selectProduct(6); break;
      case '7': selectProduct(7); break;
      case '8': selectProduct(8); break;
      case '9': selectProduct(9); break;
      default: break;
    }
  }
}
