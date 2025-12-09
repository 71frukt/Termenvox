#include <driver/dac.h>

#define ANTENNA_PIN    32
#define DAC_CH         DAC_CHANNEL_1

const int   SAMPLE_RATE = 22050;
const int   BUFFER_SIZE = 128;
const int   BASE_FREQ = 80;
const int   MAX_FREQ = 1200;
const float LFO_RATE = 0.2f;

float VOLUME = 3.0f;

const int MIN_RAW_VALUE = 100;      // Минимальное значение ADC
const int MAX_RAW_VALUE = 900;    // Максимальное при полном касании


// Плавность регулировки
const float VOLUME_BOOST = 2.0f;
const float VOLUME_POWER_CURVE = 0.4f;
const float FREQ_SMOOTHING = 0.1f;
const float VOLUME_SMOOTHING = 0.05f;

// Диапазон чувствительности
const float DEAD_ZONE = 0.05f;    // Мертвая зона в начале

float smoothedVolume = 0;
float smoothedFrequency = BASE_FREQ;

void initDACOutput() {
  dac_output_enable(DAC_CH);
}

void generateSmoothTone(uint8_t* buffer, float frequency, float volume) {
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

void setup() {
  Serial.begin(115200);
  delay(500);

  initDACOutput();
  pinMode(ANTENNA_PIN, INPUT);
  
  // Настройка ADC
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
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

  while (millis() - startTime < CALIBRATION_TIME) {
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

  while (millis() - startTime < CALIBRATION_TIME) {
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

void loop() {
  uint8_t audioBuffer[BUFFER_SIZE];

  // Чтение с фильтрацией
  static int histValues[3] = {0};
  histValues[0] = analogRead(ANTENNA_PIN);
  int filteredValue = (histValues[0] + histValues[1] + histValues[2]) / 3;
  histValues[2] = histValues[1];
  histValues[1] = histValues[0];

  // Расчет громкости с МЕРТВОЙ ЗОНОЙ
  float targetVolume = 0.0f;
  
  if (filteredValue > MIN_RAW_VALUE) {
    float norm = (float)(filteredValue - MIN_RAW_VALUE) / 
                 (float)(MAX_RAW_VALUE - MIN_RAW_VALUE);
    
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
  if (smoothedVolume > 0.01f) {
    generateSmoothTone(audioBuffer, smoothedFrequency, smoothedVolume);
  } else {
    memset(audioBuffer, 128, sizeof(audioBuffer));
  }

  // Воспроизведение
  int delayUs = 1000000 / SAMPLE_RATE;
  unsigned long nextTime = micros();
  
  for (int i = 0; i < BUFFER_SIZE; i++) {
    dac_output_voltage(DAC_CH, audioBuffer[i]);
    nextTime += delayUs;
    long wait = nextTime - micros();
    if (wait > 0) delayMicroseconds(wait);
  }

  // Отладка
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 150) {
    // График громкости
    int bars = (int)(smoothedVolume * 20.0f);
    
    Serial.printf("ADC:%4d | Г:%5.2f [", filteredValue, smoothedVolume);
    for (int i = 0; i < 20; i++) {
      Serial.print(i < bars ? "█" : "░");
    }
    Serial.printf("] %4.0fHz\n", smoothedFrequency);
    
    lastPrint = millis();
  }
}