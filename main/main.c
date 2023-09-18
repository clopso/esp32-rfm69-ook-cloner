#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/rmt.h"

#include "rf69.h"
#include "button.h"

#define TAG "MAIN"
#define PIN_DCLK_INTERRUPT 15
#define ESP_INTR_FLAG_DEFAULT 0
#define DATA_IO_PIN 27
#define PIN_LED 26
#define PIN_RX_BUTTON 32
#define PIN_TX_BUTTON 35


/* Global function */
void rx_task(void *pvParameter);
void tx_task(void *pvParameter);

rmt_item32_t dadosRF[512];
nvs_handle nvs_backup_handle;
size_t dadosRF_size = 0;

void polling(void *pvParameter){
	while(1){
	    vTaskDelay(pdMS_TO_TICKS(20));
	    if(gpio_get_level(PIN_TX_BUTTON))
	    {
	    	ESP_LOGE("Botao", "TX");
	        xTaskCreate(tx_task, "tx_task", 4096, NULL, 20, NULL);
		    vTaskDelay(pdMS_TO_TICKS(2000));
	    }
	    else if(gpio_get_level(PIN_RX_BUTTON))
	    {
	    	ESP_LOGE("Botao", "RX");
	        xTaskCreate(rx_task, "rx_task", 4096, NULL, 20, NULL);
		    vTaskDelay(pdMS_TO_TICKS(1000));
	    }
	}
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
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_LED, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_LED, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_LED, 1);
    setTxContinuousMode();

    esp_err_t err;
    rmt_config_t rmt_tx = RMT_DEFAULT_CONFIG_TX(DATA_IO_PIN, 1);

    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 1000, 0);
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
    gpio_set_level(PIN_LED, 0);
    rmt_driver_uninstall(rmt_tx.channel);
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
    rmt_driver_install(rmt_rx.channel, 1000, 0);
    RingbufHandle_t rb = NULL;
    rmt_get_ringbuf_handle(rmt_rx.channel, &rb);
    rmt_rx_start(rmt_rx.channel, 1);

    gpio_set_level(PIN_LED, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_LED, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_LED, 0);

    memset(dadosRF, 0, sizeof(dadosRF));

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
    save_struct();
    vTaskDelete(NULL);
}

#endif // CONFIG_RECEIVER

void app_main()
{
    gpio_pad_select_gpio(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LED, 0);

    gpio_config_t io_rx_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL<<PIN_RX_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 1
    };
    gpio_config(&io_rx_conf);

    gpio_config_t io_tx_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL<<PIN_TX_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 1
    };
    gpio_config(&io_tx_conf);


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

    xTaskCreate(polling, "polling", 2048, NULL, 1, NULL);
}
