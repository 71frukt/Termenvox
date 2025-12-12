#define DEBUG

#ifdef DEBUG
  #define ON_DEBUG(...) do { __VA_ARGS__; } while (0)
#else
  #define ON_DEBUG(...)
#endif

//============================================================================================================


//============= ANTENNAS =====================================================================================
#define ANTENNA_X_PIN    32
#define ANTENNA_Y_PIN    34

int MIN_RAW_VALUE = 100;    // Минимальное значение ADC
int MAX_RAW_VALUE = 900;    // Максимальное при полном касании

class Antenna
{
public:
  Antenna(int antenna_pin, float antenna_filter_coeff = 0.1)
    : pin(antenna_pin)
    , filter_coeff(antenna_filter_coeff)
  {

  }

  int   GetFilteredValue();
  float GetFilteredValueNormalized();

  void Calibrate();

  const int pin;
  const float filter_coeff;
};

// void AntennaSetup();
// void AntennaCalibrate();
// int  AntennaGetValue();
//============= /ANTENNAS ====================================================================================


//============= SOUND ========================================================================================
#include <driver/dac.h>

bool USE_SOUND = true;

#define DAC_CH         DAC_CHANNEL_1


const int   SAMPLE_RATE = 22050;
const int   BUFFER_SIZE = 128;
const int   BASE_FREQ   = 80;
const int   MAX_FREQ    = 1200;
const float LFO_RATE    = 0.2f;

float VOLUME            = 3.0f;
float VOLUME_MULTIPLIER = 1.0;


// Плавность регулировки
const float VOLUME_BOOST       = 2.0f;
const float VOLUME_POWER_CURVE = 0.4f;
const float FREQ_SMOOTHING     = 0.1f;
const float VOLUME_SMOOTHING   = 0.05f;

// Диапазон чувствительности
const float DEAD_ZONE = 0.05f;    // Мертвая зона в начале

float smoothedVolume = 0;
float smoothedFrequency = BASE_FREQ;


void DynamicSing(float volume);
void GenerateSmoothTone(uint8_t* buffer, float frequency, float volume);
//============= /SOUND =======================================================================================


//============= MOUSE ========================================================================================
bool USE_MOUSE = true;

class Mouse
{
public:
  explicit Mouse(float smooth_k = 0.05)
    : smooth_k_(smooth_k)
  {}

  float GetX() const { return smoothed_x; }
  float GetY() const { return smoothed_y; }

  void Update(float new_x, float new_y)
  {
    smoothed_x = smoothed_x * (1 - smooth_k_) + new_x * smooth_k_;
    smoothed_y = smoothed_y * (1 - smooth_k_) + new_y * smooth_k_;
  }

  void SerialTranslate()
  {
    Serial.print("MOVE ");
    Serial.print(smoothed_x);
    Serial.print(' ');
    Serial.print(smoothed_y);
    Serial.print('\n');
  }

private:
  const float smooth_k_;

  float smoothed_x;     // [0; 1]
  float smoothed_y;     // [0; 1]
};
//============= /MOUSE =======================================================================================



//============= BLUETOOTH ====================================================================================
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define DEVICE_NAME "ESP32_LED"

static BLEUUID SERVICE_UUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID CHARACTERISTIC_UUID_LED("12345678-1234-5678-1234-56789abcdef1");

BLEServer*         pServer            = nullptr;
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
    String value = pCharacteristic->getValue();

    if (value.length() == 0) {
      return;
    }

    ON_DEBUG(Serial.print("Сырые данные из BLE: "));
    ON_DEBUG(Serial.println(value));

    int a = 0;
    int b = 0;
    int c = 0;
    int s = 0;
    int m = 0;

    // Ждём строку формата:
    // A:<int>;B:<int>;C:<int>;S:<int>;M:<int>
    int parsed = sscanf(
      value.c_str(),
      "A:%d;B:%d;C:%d;S:%d;M:%d",
      &a, &b, &c, &s, &m
    );

    if (parsed >= 2) {               // хотя бы A и B есть
      MIN_RAW_VALUE = a;
      MAX_RAW_VALUE = b;
    }

    if (parsed >= 3) {               // есть ещё C = громкость (0..100)
      VOLUME_MULTIPLIER = (float) m / 100;
    }

    if (parsed >= 5) {               // есть S и M
      USE_SOUND = (s != 0);
      USE_MOUSE = (m != 0);
    }

    ON_DEBUG(
      Serial.print("MIN_RAW_VALUE = ");
      Serial.print(MIN_RAW_VALUE);
      Serial.print(" , MAX_RAW_VALUE = ");
      Serial.println(MAX_RAW_VALUE);

      Serial.print("VOLUME_MULTIPLIER = ");
      Serial.println(VOLUME_MULTIPLIER, 3);

      Serial.print("USE_SOUND = ");
      Serial.print(USE_SOUND ? "ON" : "OFF");
      Serial.print(" , USE_MOUSE = ");
      Serial.println(USE_MOUSE ? "ON" : "OFF");
    );

    if (parsed < 2) {
      ON_DEBUG(Serial.println("Не удалось распарсить строку в формате A:...;B:...;C:...;S:...;M:..."));
    }
  }
};

//============= /BLUETOOTH ===================================================================================


Antenna AntennaX = Antenna(ANTENNA_X_PIN);
Antenna AntennaY = Antenna(ANTENNA_Y_PIN);

Mouse MouseController = Mouse();

void setup()
{
  Serial.begin(115200);
  delay(500);

  dac_output_enable(DAC_CH);

  // Настройка ADC
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  ON_DEBUG(Serial.println("=== ТЕРМЕНВОКС СТАРТУЕТ ==="));

  BleInit();
  // AntennaX = Antenna(ANTENNA_X_PIN);
}

void loop()
{
  float antenna_x = AntennaX.GetFilteredValueNormalized();
  float antenna_y = AntennaY.GetFilteredValueNormalized();

  if (USE_SOUND)
  {
    DynamicSingTone(antenna_x, antenna_y);
  }

  if (USE_MOUSE)
  {
    MouseController.Update(antenna_x, antenna_y);
    MouseController.SerialTranslate();
  }
}




//============================================================================================================




//============= ANTENNAS =====================================================================================

int Antenna::GetFilteredValue()
{
  static float filtered = 0;

  int raw = analogRead(pin);

  filtered = filtered * (1 - filter_coeff) + raw * filter_coeff;
  return (int)filtered;
}

float Antenna::GetFilteredValueNormalized()
{
  int range = MAX_RAW_VALUE - MIN_RAW_VALUE;
  
  if (range <= 0)
  {
    ON_DEBUG(Serial.printf("equal MAX_RAW_VALUE and MIN_RAW_VALUE!"));
    return 0;
  }

  float norm = float(GetFilteredValue() - MIN_RAW_VALUE) / float(range);
  norm = constrain(norm, 0.0f, 1.0f);

  return norm;
}

// void AntennaSetup()
// {
//   pinMode(ANTENNA_PIN, INPUT);
  
//   // Настройка ADC
// }

void Antenna::Calibrate()
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
      int val = analogRead(pin);
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
      int val = analogRead(pin);
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

// int AntennaGetValue()
// {
//   // // Чтение с фильтрацией
//   // static int histValues[3] = {0};
//   // histValues[0] = analogRead(ANTENNA_PIN);
//   // int filteredValue = (histValues[0] + histValues[1] + histValues[2]) / 3;
//   // histValues[2] = histValues[1];
//   // histValues[1] = histValues[0];

//   // return filteredValue;

//   static float filtered = 0;
//   int raw = analogRead(ANTENNA_PIN);
//   filtered = filtered * 0.9f + raw * 0.1f;
//   return (int)filtered;
// }
//============= /ANTENNAS ====================================================================================


//============= SOUND ========================================================================================

void DynamicSing(float volume_normd)
{
  static uint8_t audioBuffer[BUFFER_SIZE];

  // Расчет громкости с МЕРТВОЙ ЗОНОЙ
  float targetVolume = 0.0f;
    
    // Убираем мертвую зону
  if (volume_normd > DEAD_ZONE) {
    volume_normd = (volume_normd - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    volume_normd = constrain(volume_normd, 0.0f, 1.0f);
    
    // Степенное преобразование
    targetVolume = powf(volume_normd, VOLUME_POWER_CURVE);
    targetVolume = fminf(targetVolume * 1.3f, 1.0f);
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
      
      Serial.printf("ADC:%4d | Г:%5.2f [", volume_normd, smoothedVolume);
      for (int i = 0; i < 20; i++) {
        Serial.print(i < bars ? "█" : "░");
      }
      Serial.printf("] %4.0fHz\n", smoothedFrequency);
      
      lastPrint = millis();
    }
  );
}


void DynamicSingTone(float volume_normd, float tone_normd)
{
  static uint8_t audioBuffer[BUFFER_SIZE];

  // ---------- ГРОМКОСТЬ: нормализация + мёртвая зона + степенная кривая ----------
  float targetVolume = 0.0f;

  // ограничим на всякий случай
  volume_normd = constrain(volume_normd, 0.0f, 1.0f);

  if (volume_normd > DEAD_ZONE) {
    // сдвигаем относительно мёртвой зоны и растягиваем
    float v = (volume_normd - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    v = constrain(v, 0.0f, 1.0f);

    // степенная кривая чувствительности
    targetVolume = powf(v, VOLUME_POWER_CURVE);
    targetVolume = fminf(targetVolume * 1.3f, 1.0f);
  }

  // ---------- ТОН: независимый от громкости ----------
  tone_normd = constrain(tone_normd, 0.0f, 1.0f);
  float targetFrequency = BASE_FREQ + (MAX_FREQ - BASE_FREQ) * tone_normd;

  // ---------- СГЛАЖИВАНИЕ ----------
  smoothedVolume    = smoothedVolume    * (1.0f - VOLUME_SMOOTHING) + 
                      targetVolume      * VOLUME_SMOOTHING;
  smoothedFrequency = smoothedFrequency * (1.0f - FREQ_SMOOTHING) + 
                      targetFrequency   * FREQ_SMOOTHING;

  // ---------- ГЕНЕРАЦИЯ ЗВУКА ----------
  if (USE_SOUND && smoothedVolume > 0.01f)
  {
    GenerateSmoothTone(audioBuffer, smoothedFrequency, smoothedVolume);
  }
  else
  {
    // тишина (середина диапазона DAC)
    memset(audioBuffer, 128, sizeof(audioBuffer));
  }

  // ---------- ВОСПРОИЗВЕДЕНИЕ ----------
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
      int bars = (int)(smoothedVolume * 20.0f);
      
      Serial.printf("VOL_N:%.2f TONE_N:%.2f | VOL:%.2f [", 
                    volume_normd, tone_normd, smoothedVolume);
      for (int i = 0; i < 20; i++) {
        Serial.print(i < bars ? "█" : "░");
      }
      Serial.printf("] %4.0f Hz\n", smoothedFrequency);
      
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
    sample = tanhf(sample * volume * VOLUME_BOOST * VOLUME * VOLUME_MULTIPLIER) * 0.9f;

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