#define DT_DRV_COMPAT ingchips_ing_adc

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/__assert.h>

struct adc_yourvendor_data {
    // ADC-specific runtime data
    struct k_sem sync;  // Synchronization primitive for blocking operations
};

struct adc_yourvendor_cfg {
    uint32_t base_address;
    uint32_t clock_frequency;
};

static int adc_yourvendor_channel_setup(const struct device *dev,
                                        const struct adc_channel_cfg *channel_cfg)
{
    // Implement the logic to configure the ADC channel (gain, reference, resolution, etc.)
    // This may involve configuring hardware registers or internal data structures.
    return 0;
}

static int adc_yourvendor_start_read(const struct device *dev, const struct adc_sequence *sequence)
{
    struct adc_yourvendor_data *data = dev->data;
    struct adc_yourvendor_cfg *cfg = dev->config;

    // Here, configure ADC registers to start conversion
    // For example, set the channel and trigger the conversion
    // Then wait for the result to be ready, and store it in the buffer provided in 'sequence'

    // Simulate data read
    for (int i = 0; i < sequence->buffer_size; i++) {
        ((uint16_t *)sequence->buffer)[i] = i * 100; // Example dummy data
    }

    return 0;
}

static int adc_yourvendor_init(const struct device *dev)
{
    struct adc_yourvendor_data *data = dev->data;
    struct adc_yourvendor_cfg *cfg = dev->config;

    // Initialize the ADC controller, reset it, enable clocks, etc.
    k_sem_init(&data->sync, 0, 1);

    return 0;
}

static const struct adc_driver_api adc_yourvendor_api = {
        .channel_setup = adc_yourvendor_channel_setup,
        .read = adc_yourvendor_start_read,
};

DEVICE_DT_INST_DEFINE(0, adc_yourvendor_init, NULL,
                      &adc_yourvendor_data_0, &adc_yourvendor_cfg_0,
                      POST_KERNEL, CONFIG_ADC_INIT_PRIORITY,
                      &adc_yourvendor_api);

DT_INST_FOREACH_STATUS_OKAY(DEVICE_DT_INST_DEFINE)
