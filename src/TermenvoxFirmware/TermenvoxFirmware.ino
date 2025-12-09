#define DEBUG

#ifdef DEBUG
  #define ON_DEBUG(...) do { __VA_ARGS__; } while (0)
#else
  #define ON_DEBUG(...)
#endif

//============================================================================================================


//============= SOUND ========================================================================================
#include <driver/dac.h>

#define ANTENNA_PIN    32
#define DAC_CH         DAC_CHANNEL_1


const int   SAMPLE_RATE = 22050;
const int   BUFFER_SIZE = 128;
const int   BASE_FREQ   = 80;
const int   MAX_FREQ    = 1200;
const float LFO_RATE    = 0.2f;

float VOLUME = 3.0f;

int MIN_RAW_VALUE = 100;    // Минимальное значение ADC
int MAX_RAW_VALUE = 900;    // Максимальное при полном касании


// Плавность регулировки
const float VOLUME_BOOST       = 2.0f;
const float VOLUME_POWER_CURVE = 0.4f;
const float FREQ_SMOOTHING     = 0.1f;
const float VOLUME_SMOOTHING   = 0.05f;

// Диапазон чувствительности
const float DEAD_ZONE = 0.05f;    // Мертвая зона в начале

float smoothedVolume = 0;
float smoothedFrequency = BASE_FREQ;


void AntennaSetup();
void AntennaCalibrate();
int  AntennaGetValue();

void DynamicSing(int antenna_value);
void GenerateSmoothTone(uint8_t* buffer, float frequency, float volume);
//============= /SOUND =======================================================================================


//============= BLUETOOTH ====================================================================================
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define DEVICE_NAME "ESP32_LED"

static BLEUUID SERVICE_UUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID CHARACTERISTIC_UUID_LED("12345678-1234-5678-1234-56789abcdef1");

BLEServer*        pServer           = nullptr;
BLECharacteristic* pLedCharacteristic = nullptr;

bool deviceConnected = false;

void BleInit();

// ----------------- Колбэки сервера -----------------

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer* pServer) override
  {
    deviceConnected = true;
    ON_DEBUG(Serial.println("Клиент подключился"));
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    ON_DEBUG(Serial.println("Клиент отключился"));
    pServer->getAdvertising()->start();
    ON_DEBUG(Serial.println("Реклама снова запущена"));
  }
};

// ----------------- Колбэк характеристики -----------------

class ParamCharCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic* pCharacteristic) override
  {
    String value = pCharacteristic->getValue();  // <-- исправлено

    if (value.length() == 0) {
      return;
    }

    ON_DEBUG(Serial.print("Сырые данные из BLE: "));
    ON_DEBUG(Serial.println(value));

    int a = 0;
    int b = 0;

    // Парсим строку формата "A:<int>;B:<int>"
    // value.c_str() даёт const char* для sscanf
    int parsed = sscanf(value.c_str(), "A:%d;B:%d", &a, &b);

    if (parsed == 2)
    {
      ON_DEBUG(
        Serial.print("Успешно распарсили: A = ");
        Serial.print(a);
        Serial.print(" , B = ");
        Serial.println(b);
      );

      MIN_RAW_VALUE = a;
      MAX_RAW_VALUE = b;
    }
    
    else
    {
      ON_DEBUG(Serial.println("Не удалось распарсить строку в формате A:<int>;B:<int>"));
    }
  }
};
//============= /BLUETOOTH ===================================================================================




void setup()
{
  Serial.begin(115200);
  delay(500);

  dac_output_enable(DAC_CH);

  ON_DEBUG(Serial.println("=== ТЕРМЕНВОКС СТАРТУЕТ ==="));

  BleInit();
  AntennaSetup();
}

void loop()
{
  // Чтение с фильтрацией
  DynamicSing(AntennaGetValue());
}




//============================================================================================================




//============= SOUND ========================================================================================

void AntennaSetup()
{
  pinMode(ANTENNA_PIN, INPUT);
  
  // Настройка ADC
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

void AntennaCalibrate()
{
  Serial.println("=== ТЕРМЕНВОКС ===");
  Serial.println("Определи значения MIN и MAX:");
  Serial.println("1. Не касайся антенны 3 секунды...");

  const int CALIBRATION_TIME = 3000;       // время сбора данных (мс)
  const int SAMPLE_DELAY = 5;              // задержка между измерениями (мс)
                                          // меньше — больше точность

  // ---- Считывание среднего без касания ----
  unsigned long startTime = millis();
  long sumBase = 0;
  int countBase = 0;

  while (millis() - startTime < CALIBRATION_TIME)
  {
      int val = analogRead(ANTENNA_PIN);
      sumBase += val;
      countBase++;

      Serial.printf("Без касания: %d\n", val);   // показываем все значения
      delay(SAMPLE_DELAY);
  }

  int baseValue = sumBase / countBase;      // среднее значение
  Serial.printf("Среднее без касания: %d\n\n", baseValue);


  // ---- Считывание среднего с касанием ----
  Serial.println("2. Теперь коснись антенны...");
  delay(1000);   // небольшая пауза перед измерениями

  startTime = millis();
  long sumTouch = 0;
  int countTouch = 0;

  while (millis() - startTime < CALIBRATION_TIME)
  {
      int val = analogRead(ANTENNA_PIN);
      sumTouch += val;
      countTouch++;

      Serial.printf("С касанием: %d\n", val);    // показываем все значения
      delay(SAMPLE_DELAY);
  }

  int touchValue = sumTouch / countTouch;    // среднее значение
  Serial.printf("Среднее с касанием: %d\n\n", touchValue);


  // ---- Вывод итоговых значений ----
  Serial.println("Скопируй эти значения и поменяй в коде:");
  Serial.printf("const int MIN_RAW_VALUE = %d;\n", baseValue);
  Serial.printf("const int MAX_RAW_VALUE = %d;\n", touchValue);

  
  Serial.println("\nЗагрузи код с новыми значениями!");
}

int AntennaGetValue()
{
  // Чтение с фильтрацией
  static int histValues[3] = {0};
  histValues[0] = analogRead(ANTENNA_PIN);
  int filteredValue = (histValues[0] + histValues[1] + histValues[2]) / 3;
  histValues[2] = histValues[1];
  histValues[1] = histValues[0];

  return filteredValue;
}


void DynamicSing(int antenna_value)
{
  static uint8_t audioBuffer[BUFFER_SIZE];

  // Расчет громкости с МЕРТВОЙ ЗОНОЙ
  float targetVolume = 0.0f;
  
  if (antenna_value > MIN_RAW_VALUE)
  {
    int range = MAX_RAW_VALUE - MIN_RAW_VALUE;
    if (range <= 0)
    {
      ON_DEBUG(Serial.printf("equal MAX_RAW_VALUE and MIN_RAW_VALUE!"));
      return;
    }

    float norm = float(antenna_value - MIN_RAW_VALUE) / float(range);
    
    // Ограничение
    norm = constrain(norm, 0.0f, 1.0f);
    
    // Убираем мертвую зону
    if (norm > DEAD_ZONE) {
      norm = (norm - DEAD_ZONE) / (1.0f - DEAD_ZONE);
      norm = constrain(norm, 0.0f, 1.0f);
      
      // Степенное преобразование
      targetVolume = powf(norm, VOLUME_POWER_CURVE);
      targetVolume = fminf(targetVolume * 1.3f, 1.0f);
    }
  }

  // Расчет частоты
  float targetFrequency = BASE_FREQ + (MAX_FREQ - BASE_FREQ) * targetVolume;

  // Сглаживание
  smoothedVolume = smoothedVolume * (1.0f - VOLUME_SMOOTHING) + 
                   targetVolume * VOLUME_SMOOTHING;
  smoothedFrequency = smoothedFrequency * (1.0f - FREQ_SMOOTHING) + 
                      targetFrequency * FREQ_SMOOTHING;

  // Генерация звука
  if (smoothedVolume > 0.01f)
  {
    GenerateSmoothTone(audioBuffer, smoothedFrequency, smoothedVolume);
  }
  
  else
  {
    memset(audioBuffer, 128, sizeof(audioBuffer));
  }

  // Воспроизведение
  int delayUs = 1000000 / SAMPLE_RATE;
  unsigned long nextTime = micros();
  
  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    dac_output_voltage(DAC_CH, audioBuffer[i]);
    nextTime += delayUs;
    long wait = nextTime - micros();
    if (wait > 0) delayMicroseconds(wait);
  }

  ON_DEBUG(
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 150)
    {
      // График громкости
      int bars = (int)(smoothedVolume * 20.0f);
      
      Serial.printf("ADC:%4d | Г:%5.2f [", antenna_value, smoothedVolume);
      for (int i = 0; i < 20; i++) {
        Serial.print(i < bars ? "█" : "░");
      }
      Serial.printf("] %4.0fHz\n", smoothedFrequency);
      
      lastPrint = millis();
    }
  );
}


void GenerateSmoothTone(uint8_t* buffer, float frequency, float volume)
{
  static float phase = 0.0f;
  static float lfoPhase = 0.0f;
  float dt = 1.0f / SAMPLE_RATE;

  float lfo = sin(lfoPhase) * 0.03f;
  lfoPhase += 2 * PI * LFO_RATE * dt;
  if (lfoPhase > 2 * PI) lfoPhase -= 2 * PI;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float f = frequency * (1.0f + lfo);
    float sample = sinf(2.0f * PI * phase);
    phase += f * dt;
    if (phase >= 1.0f) phase -= 1.0f;

    // Усиление с насыщением
    sample = tanhf(sample * volume * VOLUME_BOOST * VOLUME) * 0.9f;

    float dacValue = 128.0f + sample * 120.0f;
    if (dacValue < 0.0f) dacValue = 0.0f;
    if (dacValue > 255.0f) dacValue = 255.0f;
    
    buffer[i] = (uint8_t)dacValue;
  }
}

//============= /SOUND =======================================================================================



//============= BLUETOOTH ====================================================================================

void BleInit()
{
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

  //ON_DEBUG(
    Serial.println("BLE сервер запущен, реклама включена");
    Serial.print("Имя устройства: ");
    Serial.println(DEVICE_NAME);
  //);
}

//============= /BLUETOOTH ===================================================================================
