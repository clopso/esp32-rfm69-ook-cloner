#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/rmt.h"

#include "rf69.h"

#define TAG "MAIN"
#define PIN_DCLK_INTERRUPT 15
#define ESP_INTR_FLAG_DEFAULT 0
#define DATA_IO_PIN 27
#define PIN_LED 26
#define PIN_SWITCH 32

/* Global function */

rmt_item32_t dadosRF[512];
size_t dadosRF_size = 0;
char dadosRF_binario[256];
uint8_t install_tx_rmt = 0, install_rx_rmt = 0;

static inline uint8_t filter_data(void)
{
    uint8_t i;
    uint8_t x = gpio_get_level(PIN_DIO2);
    for (i = 0; i != 32; ++i)
        x &= gpio_get_level(PIN_DIO2);
    return x;
}

void delay_us(uint64_t number_of_us)
{
    uint64_t microseconds = (uint64_t)esp_timer_get_time();
    if (number_of_us)
    {
        uint64_t total = (microseconds + number_of_us);
        if (microseconds > total)
        {
            while ((uint64_t)esp_timer_get_time() > total)
                ;
        }
        while ((uint64_t)esp_timer_get_time() < total)
            ;
    }
}

void tx_task(void *pvParameter)
{

    gpio_set_level(PIN_LED, 0);
    setTxContinuousMode();

    esp_err_t err;

    rmt_config_t rmt_tx = RMT_DEFAULT_CONFIG_TX(DATA_IO_PIN, 1);

    rmt_config(&rmt_tx);

    if (install_tx_rmt == 0)
    {
        rmt_driver_install(rmt_tx.channel, 1000, 0);
        install_tx_rmt++;
    }

    rmt_tx_start(rmt_tx.channel, 1);

    gpio_set_level(PIN_DIO2, 0);

    ESP_LOGE(TAG, "Send TX!");
    for (int i = 0; i < 10; i++)
    {
        rmt_write_items(rmt_tx.channel, dadosRF,
                        sizeof(dadosRF) / sizeof(dadosRF[0]), true);

        err = rmt_wait_tx_done(rmt_tx.channel, 1000);

        printf((err != ESP_OK) ? "Failure!\n" : "Success!\n");

        delay_us(9000);
    }
    gpio_set_level(PIN_LED, 1);
    vTaskDelete(NULL);
}

#if CONFIG_RECEIVER

void rx_task(void *pvParameter)
{

    setRxContinuousMode();

    rmt_config_t rmt_rx = {.channel = 0,
                           .gpio_num = DATA_IO_PIN,
                           .clk_div = 80, // 1MHz
                           .mem_block_num = 1,
                           .rmt_mode = RMT_MODE_RX,
                           .rx_config.filter_en = true,
                           .rx_config.filter_ticks_thresh = 100,
                           .rx_config.idle_threshold =
                               9500}; // rmt receiver configurations

    rmt_config(&rmt_rx);

    if (install_rx_rmt == 0)
    {
        rmt_driver_install(rmt_rx.channel, 1000, 0);
        install_rx_rmt++;
    }

    RingbufHandle_t rb = NULL;
    rmt_get_ringbuf_handle(rmt_rx.channel, &rb);
    rmt_rx_start(rmt_rx.channel, 1);

    while (1)
    {
        int j = 0;
        uint64_t duration = 0, counter = 0;
        size_t rx_size = 0;
        rmt_item32_t *item =
            (rmt_item32_t *)xRingbufferReceive(rb, &rx_size, 1000);
        if (item)
        {
            for (int i = 0; i < rx_size; i++)
            {
                if ((item + i)->duration0 > 2000 ||
                    (item + i)->duration0 < 200 ||
                    (item + i)->duration1 > 2000 || (item + i)->duration1 < 200)
                {
                    continue;
                }
                duration += (item + i)->duration0;
                counter++;
                duration += (item + i)->duration1;
                counter++;
            }

            if (counter != 0)
            {
                duration /= counter;
            }

            for (int i = 0; i < rx_size; i++)
            {
                if ((item + i)->duration0 >= duration * 2 && duration != 0)
                {
                    break;
                }
                if ((item + i)->duration0 >= duration / 2)
                {
                    dadosRF[j].duration0 = (item + i)->duration0;
                    dadosRF[j].level0 = (item + i)->level0;
                }
                if ((item + i)->duration1 >= duration / 2)
                {
                    dadosRF[j].duration1 = (item + i)->duration1;
                    dadosRF[j].level1 = (item + i)->level1;
                }
                j++;
            }
            gpio_set_level(PIN_LED, 1);

            ESP_LOGE(TAG, "%ld", (long)duration);
            vRingbufferReturnItem(rb, (void *)item);

            break;
        }
    }

    while (1)
    {
        if (gpio_get_level(PIN_SWITCH))
        {
            xTaskCreate(&tx_task, "tx_task", 1024 * 3, NULL,
                        configMAX_PRIORITIES, NULL);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

#endif // CONFIG_RECEIVER

void app_main()
{
    gpio_pad_select_gpio(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio(PIN_SWITCH);
    gpio_set_direction(PIN_SWITCH, GPIO_MODE_INPUT);
    gpio_set_level(PIN_LED, 0);

    gpio_pulldown_en(PIN_SWITCH);
    gpio_pullup_dis(PIN_SWITCH);
    // Initialize
    if (!init())
    {
        ESP_LOGE(TAG, "RFM69 radio init failed");
        while (1)
        {
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "RFM69 radio init OK!");

    rfm69_write_bitrate((uint16_t)(32000.0 / 32.0));

    // Set frequency
    float freq;
    freq = 433.90;
    ESP_LOGW(TAG, "Set frequency to %.1fMHz", freq);

    // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM (for
    // low power module) No encryption
    if (!setFrequency(freq))
    {
        ESP_LOGE(TAG, "setFrequency failed");
        while (1)
        {
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "RFM69 radio setFrequency OK!");

    rfm69_write_ook_peak(1 << 6);
    rfm69_write_ook_fix(70);
    rfm69_write_pa_level((1 << 7) | (0x10));

    uint8_t x;
    x = rfm69_read_lna();
    x = (1 << 7) | (1 << 0);
    rfm69_write_lna(x);
    rfm69_write_rx_bw((2 << 5) | (1 << 3) | (1 << 0));
    rfm69_write_rssi_threshold(35);

#if CONFIG_RF69_POWER_HIGH
    // If you are using a high power RF69 eg RFM69HW, you *must* set a Tx power
    // with the ishighpowermodule flag set like this:
    setTxPower(
        20, true); // range from 14-20 for power, 2nd arg must be true for 69HCW
    ESP_LOGW(TAG, "Set TX power high");
#endif

    // Deactivating encryption key
    setEncryptionKey(NULL);

#if CONFIG_TRANSMITTER
    xTaskCreate(&tx_task, "tx_task", 1024 * 3, NULL, configMAX_PRIORITIES,
                NULL);
#endif // CONFIG_TRANSMITTER
#if CONFIG_RECEIVER
    xTaskCreate(&rx_task, "rx_task", 1024 * 3, NULL, configMAX_PRIORITIES,
                NULL);

    printf("Minimum free heap size: %d bytes\n",
           esp_get_minimum_free_heap_size());

#endif // CONFIG_RECEIVER
}
