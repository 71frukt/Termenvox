#include <driver/rmt.h>
#include <driver/dac.h>

#define RMT_RX_GPIO   34
#define DAC_OUT       DAC_CHANNEL_1

const int SAMPLE_RATE = 22050;
const int BUFFER_SIZE = 128;

const float SMOOTHING = 0.05f;
const float BASE_AUDIO_FREQ = 80.0f;
const float MAX_AUDIO_FREQ  = 1200.0f;

float baseFreq = 0;
float smoothFreq = 0;

void initDAC()
{
    dac_output_enable(DAC_OUT);
}

void generateTone(uint8_t* buffer, float frequency, float volume)
{
    static float phase = 0;
    static float lfo1 = 0, lfo2 = 0;

    float dt = 1.0f / SAMPLE_RATE;

    for (int i = 0; i < BUFFER_SIZE; i++)
    {

        float fm = sin(lfo1) * 0.06f + sin(lfo2) * 0.1f;
        lfo1 += 2 * PI * 6.5f * dt;
        lfo2 += 2 * PI * 0.5f * dt;

        float f = frequency * (1.0f + fm);
        phase += f * dt;
        if (phase >= 1.0f) phase -= 1.0f;

        float s = sin(2 * PI * phase);
        s = tanh(s * volume * 2.5f);

        buffer[i] = 128 + s * 120;
    }
}

float measureFreq()
{
    rmt_item32_t item;
    size_t length;

    esp_err_t res = rmt_receive(RMT_CHANNEL_0, &item, sizeof(item), &length, 10);

    if (res != ESP_OK || length == 0) return -1;

    float periodTicks = item.duration0 + item.duration1;

    float period = periodTicks * 12.5e-9;

    if (period <= 0) return -1;

    return 1.0f / period;
}

void initRMT()
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_RX(RMT_RX_GPIO, RMT_CHANNEL_0);
    config.clk_div = 1;
    config.rx_config.filter_en = true;
    config.rx_config.filter_ticks_thresh = 100;

    rmt_config(&config);
    rmt_driver_install(RMT_CHANNEL_0, 2000, 0);
    rmt_get_ringbuf_handle(RMT_CHANNEL_0, NULL);
    rmt_rx_start(RMT_CHANNEL_0, true);
}


void setup()
{
    Serial.begin(115200);
    initDAC();
    initRMT();

    Serial.println("Calibrating baseline...");

    float sum = 0;
    int count = 0;

    while (count < 200)
    {
        float f = measureFreq();

        if (f > 0 && f < 5e6)
        {
            sum += f;
            count++;
        }
    }
    baseFreq = sum / count;
    smoothFreq = baseFreq;

    Serial.printf("Baseline freq = %.2f Hz\n", baseFreq);
}


void loop()
{
    uint8_t buf[BUFFER_SIZE];

    float f = measureFreq();

    if (f > 0 && f < 5e6)
    {
        smoothFreq = smoothFreq * (1 - SMOOTHING) + f * SMOOTHING;

        float delta = smoothFreq - baseFreq;

        if (delta < 0) delta = 0;
        if (delta > baseFreq * 0.02f) delta = baseFreq * 0.02f;

        float norm = delta / (baseFreq * 0.02f);

        float audioFreq = BASE_AUDIO_FREQ + norm * (MAX_AUDIO_FREQ - BASE_AUDIO_FREQ);

        float volume = norm;

        generateTone(buf, audioFreq, volume);

        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            dac_output_voltage(DAC_OUT, buf[i]);
            delayMicroseconds(1000000 / SAMPLE_RATE);
        }

        static uint32_t lastPrint = 0;

        if (millis() - lastPrint > 100)
        {
            Serial.printf("LC freq: %.1f Hz, Δ=%.1f, audioFreq=%.1f\n",
                          smoothFreq, delta, audioFreq);
            lastPrint = millis();
        }

    }

    else
    {
        delay(10);
    }
}
