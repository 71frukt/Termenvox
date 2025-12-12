#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <stdio.h>

#define LED_PIN 2

#define DEVICE_NAME "ESP32_LED"

static BLEUUID SERVICE_UUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID CHARACTERISTIC_UUID_LED("12345678-1234-5678-1234-56789abcdef1");

BLEServer*         pServer            = nullptr;
BLECharacteristic* pLedCharacteristic = nullptr;

bool deviceConnected = false;

// ----------------- Колбэки сервера -----------------

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("Клиент подключился");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("Клиент отключился");
    pServer->getAdvertising()->start();
    Serial.println("Реклама снова запущена");
  }
};

// ----------------- Колбэк характеристики -----------------

class ParamCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String value = pCharacteristic->getValue();  // <-- исправлено

    if (value.length() == 0) {
      return;
    }

    Serial.print("Сырые данные из BLE: ");
    Serial.println(value);

    int a = 0;
    int b = 0;

    // Парсим строку формата "A:<int>;B:<int>"
    // value.c_str() даёт const char* для sscanf
    int parsed = sscanf(value.c_str(), "A:%d;B:%d", &a, &b);

    if (parsed == 2) {
      Serial.print("Успешно распарсили: A = ");
      Serial.print(a);
      Serial.print(" , B = ");
      Serial.println(b);

      // Здесь можно дальше использовать a и b как тебе нужно
    } else {
      Serial.println("Не удалось распарсить строку в формате A:<int>;B:<int>");
    }
  }
};

// ----------------- setup / loop -----------------

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Запуск ESP32 BLE сервера с протоколом A:..;B:..");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Инициализация BLE
  BLEDevice::init(DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pLedCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_LED,
    BLECharacteristic::PROPERTY_WRITE
  );

  pLedCharacteristic->setCallbacks(new ParamCharCallbacks());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("BLE сервер запущен, реклама включена");
  Serial.print("Имя устройства: ");
  Serial.println(DEVICE_NAME);
}

void loop() {
  // Всё делается в колбэках
  delay(1000);
}
