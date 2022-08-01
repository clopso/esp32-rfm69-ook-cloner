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

// static rmt_rx_channel = RMT_CHANNEL_1;

/* Global function */

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

#if CONFIG_TRANSMITTER
void tx_task(void *pvParameter)
{
    setTxContinuousMode();

    esp_err_t err;

    rmt_config_t rmt_tx = RMT_DEFAULT_CONFIG_TX(DATA_IO_PIN, 0);
    static const char dataBin[] = {
        "1001011011001011011011011001001011011001011001001001011011001001011011"
        "001011001011001"};
    rmt_item32_t tx_data[50];
    int j = 0, i = 0;

    for (i = 0; i < strlen(dataBin); i++)
    {
        (tx_data + i)->duration0 = 400;
        (tx_data + i)->level0 = dataBin[j++] - '0';

        (tx_data + i)->duration1 = 400;
        (tx_data + i)->level1 = dataBin[j++] - '0';
    }

    // i++;
    // (tx_data + i)->duration0 = 0;
    // (tx_data + i)->level0 = 1;
    // (tx_data + i)->duration1 = 0;
    // (tx_data + i)->level1 = 0;

    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 1000, 0);
    rmt_tx_start(rmt_tx.channel, 1);

    gpio_set_level(PIN_DIO2, 0);

    while (1)
    {
        ESP_LOGE(TAG, "Send TX!");
        for (int i = 0; i < 10; i++)
        {
            rmt_write_items(rmt_tx.channel, tx_data,
                            sizeof(tx_data) / sizeof(tx_data[0]), true);

            err = rmt_wait_tx_done(rmt_tx.channel, 1000);

            printf((err != ESP_OK) ? "Failure!\n" : "Success!\n");
            
            delay_us(9000);
        }

        vTaskDelay(1500 / portTICK_PERIOD_MS);
    }
}
#endif // CONFIG_TRANSMITTER

#if CONFIG_RECEIVER
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

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

    while (rb)
    {
        size_t rx_size = 0;
        rmt_item32_t *item =
            (rmt_item32_t *)xRingbufferReceive(rb, &rx_size, 1000);
        if (item)
        {
            for (int i = 0; i < rx_size >> 2; i++)
            {
                // printf("%d:%dus %d:%dus\n", (item+i)->level0,
                // (item+i)->duration0, (item+i)->level1, (item+i)->duration1);
                if (((item + i)->duration0) >= 1000)
                {
                    printf("  fim\n");
                    break;
                }

                if (((item + i)->duration0) >= 700)
                {
                    printf("00");
                }
                else
                {
                    printf("0");
                }

                if (((item + i)->duration1) >= 700)
                {
                    printf("11");
                }
                else
                {
                    printf("1");
                }
            }
            vRingbufferReturnItem(rb, (void *)item);
        }
        else
        {
            break;
        }
    }
    vTaskDelete(NULL);
}

#endif // CONFIG_RECEIVER

void app_main()
{
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

    // Set frequency
    float freq;
    freq = 433.0;
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
    gpio_config_t in_conf = {
        .intr_type = GPIO_INTR_POSEDGE, // interrupt of rising edge
        .pin_bit_mask =
            (1ULL
             << PIN_DCLK_INTERRUPT), // bit mask of the pins, use GPIO15 here
        .mode = GPIO_MODE_INPUT,     // set as input mode
        .pull_up_en = 0,             // disable pull-up mode
        .pull_down_en = 1,           // enable pull-down mode
    };
    gpio_config(&in_conf);
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    xTaskCreate(&rx_task, "rx_task", 1024 * 3, NULL, configMAX_PRIORITIES,
                NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(PIN_DCLK_INTERRUPT, gpio_isr_handler,
                         (void *)DATA_IO_PIN);
    printf("Minimum free heap size: %d bytes\n",
           esp_get_minimum_free_heap_size());

#endif // CONFIG_RECEIVER
}
