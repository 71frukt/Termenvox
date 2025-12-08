#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ПИН СО СВЕТОДИОДОМ
// Для большинства ESP32 встроенный LED сидит на GPIO2.
// Если у тебя отдельный светодиод – укажи здесь свой пин:
#define LED_PIN 2

// Имя BLE-устройства (телефон ищет именно его)
#define DEVICE_NAME "ESP32_LED"

// UUID сервиса и характеристики – ДОЛЖНЫ совпадать с Android
// SERVICE_UUID = 12345678-1234-5678-1234-56789abcdef0
// CHARACTERISTIC_UUID_LED = 12345678-1234-5678-1234-56789abcdef1

static BLEUUID SERVICE_UUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID CHARACTERISTIC_UUID_LED("12345678-1234-5678-1234-56789abcdef1");

// Глобальные указатели на сервер и характеристику (на будущее, если захочешь расширять)
BLEServer* pServer = nullptr;
BLECharacteristic* pLedCharacteristic = nullptr;

// Флаг подключения (чисто информационно)
bool deviceConnected = false;

// Колбэк сервера – просто отслеживаем подключение/отключение
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("Клиент подключился");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("Клиент отключился");
    // Можно снова запустить рекламу, если нужно автоподключение
    pServer->getAdvertising()->start();
    Serial.println("Реклама снова запущена");
  }
};

class LedCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    const uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();
    
    if (len == 0) return;

    char cmd = (char)data[0]; // первый байт команды

    if (cmd == '1') {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("Команда: ВКЛ (1)");
    } 
    else if (cmd == '0') {
      digitalWrite(LED_PIN, LOW);
      Serial.println("Команда: ВЫКЛ (0)");
    } 
    else {
      Serial.print("Неизвестная команда: ");
      Serial.println(cmd);
    }
  }
};


void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Запуск ESP32 BLE LED сервера...");

  // Настраиваем пин со светодиодом
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // по умолчанию выкл

  // Инициализация BLE-стека, задаём имя устройства
  BLEDevice::init(DEVICE_NAME);

  // Создаём BLE-сервер
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Создаём сервис
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Создаём характеристику для управления LED
  // Нам нужна только запись (WRITE)
  pLedCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_LED,
    BLECharacteristic::PROPERTY_WRITE
  );

  // Вешаем колбэк на запись в характеристику
  pLedCharacteristic->setCallbacks(new LedCharCallbacks());

  // Запускаем сервис
  pService->start();

  // Запускаем рекламу (advertising),
  // чтобы телефон мог нас увидеть при сканировании
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // параметры можно не трогать
  pAdvertising->setMaxPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("BLE сервер запущен, реклама включена");
  Serial.print("Имя устройства: ");
  Serial.println(DEVICE_NAME);
}

void loop() {
  // BLE работает через колбэки, поэтому тут может вообще ничего не быть
  delay(1000);
}
