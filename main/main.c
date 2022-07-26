#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "rf69.h"

#define TAG "MAIN"
#define PIN_DCLK_INTERRUPT 15
#define ESP_INTR_FLAG_DEFAULT 0
#define DATA_IO_PIN 27

static xQueueHandle gpio_evt_queue = NULL;

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

	static const int repeat = 20;
	static const int syncBytes = 0;
	static const char dataBin[] = {"1001011011001011011011011001001011011001011001001001011011001001011011001011001011001"};

	gpio_set_level(PIN_DIO2, 0);

	while (1)
	{
		ESP_LOGE(TAG, "Send TX!");

		for (int i = 0; i < repeat; i++)
		{
			for (int j = 0; j < syncBytes; j++)
			{
				gpio_set_level(PIN_DIO2, 1);
				delay_us(400);
				gpio_set_level(PIN_DIO2, 0);
				delay_us(400);
			}
			gpio_set_level(PIN_DIO2, 0);

			for (int k = 0; k < strlen(dataBin); k++)
			{
				gpio_set_level(PIN_DIO2, dataBin[k] - '0');
				delay_us(400);
			}
			gpio_set_level(PIN_DIO2, 0);
			vTaskDelay(9 / portTICK_PERIOD_MS);
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
	uint8_t cur_state;
	uint32_t data_io_num;
	int count = 0;
	uint8_t frame_index[1024] = {0};

	setRxContinuousMode();

	while (1)
	{

		if (xQueueReceive(gpio_evt_queue, &data_io_num, portMAX_DELAY))
		{
			cur_state = gpio_get_level(data_io_num);
			frame_index[count] = cur_state + '0';
			count++;
		}
		if (count >= 1023)
		{
			count = 0;
			ESP_LOGI(TAG, "%s", frame_index);
			memset(&frame_index, 0, sizeof(frame_index));
			vTaskDelay(2);
		}
	}
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

	// Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM (for low power module)
	// No encryption
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
	// If you are using a high power RF69 eg RFM69HW, you *must* set a Tx power with the
	// ishighpowermodule flag set like this:
	setTxPower(20, true); // range from 14-20 for power, 2nd arg must be true for 69HCW
	ESP_LOGW(TAG, "Set TX power high");
#endif

	// Deactivating encryption key
	setEncryptionKey(NULL);

#if CONFIG_TRANSMITTER
	xTaskCreate(&tx_task, "tx_task", 1024 * 3, NULL, configMAX_PRIORITIES, NULL);
#endif // CONFIG_TRANSMITTER
#if CONFIG_RECEIVER
	gpio_config_t in_conf = {
		.intr_type = GPIO_INTR_POSEDGE, // interrupt of rising edge
		.pin_bit_mask =
			(1ULL << PIN_DCLK_INTERRUPT), // bit mask of the pins, use GPIO15 here
		.mode = GPIO_MODE_INPUT,		  // set as input mode
		.pull_up_en = 0,				  // disable pull-up mode
		.pull_down_en = 1,				  // enable pull-down mode
	};
	gpio_config(&in_conf);
	gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

	xTaskCreate(&rx_task, "rx_task", 1024 * 3, NULL, configMAX_PRIORITIES, NULL);

	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(PIN_DCLK_INTERRUPT, gpio_isr_handler, (void *)DATA_IO_PIN);
	printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());

#endif // CONFIG_RECEIVER
}
