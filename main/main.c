#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "rf69.h"

#define TAG "MAIN"

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

	static const int syncBytes = 12;
	static const int dataBin[] = {1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1};

	while (1)
	{

		for (int i = 0; i < syncBytes; i++)
		{
			gpio_set_level(PIN_DIO2, 1);
			delay_us(400);
			gpio_set_level(PIN_DIO2, 0);
			delay_us(400);
		}
		gpio_set_level(PIN_DIO2, 0);
		delay_us(4000);

		for (int i = 0; i < 196; i++)
		{
			gpio_set_level(PIN_DIO2, dataBin[i]);
			delay_us(400);
		}
		gpio_set_level(PIN_DIO2, 0);
		vTaskDelay(1500 / portTICK_PERIOD_MS);
	}
}
#endif // CONFIG_TRANSMITTER

#if CONFIG_RECEIVER
void rx_task(void *pvParameter)
{
	uint8_t cur_state;
	uint8_t frame_index[1024] = {0};

	setRxContinuousMode();

	while (1)
	{
		memset(&frame_index, 0, sizeof(frame_index));
		// enter critical or RMT
		for (int i = 0; i < 1024; i++)
		{
			cur_state = filter_data();
			frame_index[i] = cur_state + '0';

			delay_us(400);
		}

		ESP_LOGI(TAG, "%s", frame_index);
		vTaskDelay(600 / portTICK_PERIOD_MS);
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
	xTaskCreate(&rx_task, "rx_task", 1024 * 3, NULL, configMAX_PRIORITIES, NULL);
#endif // CONFIG_RECEIVER
}
