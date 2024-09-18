#define DT_DRV_COMPAT ingchips_ing_gpio

#include <errno.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <soc.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/kernel.h>
#include <peripheral_pinctrl.h>
#include <peripheral_gpio.h>
#include <peripheral_sysctrl.h>
#include <platform_api.h>


#define  GPIO1_PIN_MAX      (20)
#define GPIO0_BASE      0x40015000
#define GPIO1_BASE      0x40016000

typedef void (*config_func_t)(const struct device *dev);

struct gpio_ingchips_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	uint32_t base;
	uint32_t port_map;
	config_func_t config_func;
};

struct gpio_ingchips_runtime {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	sys_slist_t cb;
};

static void gpio_ingchips_isr(const struct device *dev)
{
	const struct gpio_ingchips_config * const cfg = dev->config;
	struct gpio_ingchips_runtime *context = dev->data;
	uint32_t base = cfg->base;
    GIO_TypeDef *gio_type_ptr = (GIO_TypeDef *)base;
    uint32_t int_stat = gio_type_ptr->IntStatus;

    gio_type_ptr->IntStatus = (uint32_t)-1;
	gpio_fire_callbacks(&context->cb, dev, int_stat);
}

static int gpio_ingchips_configure(const struct device *dev,
				    gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_ingchips_config *cfg = dev->config;

	uint32_t base = cfg->base;
    if(base == GPIO0_BASE){
        SYSCTRL_ClearClkGateMulti( (1 << SYSCTRL_ClkGate_APB_GPIO0));
    } else if(base == GPIO1_BASE){
        SYSCTRL_ClearClkGateMulti( (1 << SYSCTRL_ClkGate_APB_GPIO1));
        pin += GPIO1_PIN_MAX;
    } else{
        return -ENODEV;
    }

    if(0 != (flags & GPIO_PULL_UP)){
        PINCTRL_Pull((GIO_Index_t)pin,PINCTRL_PULL_UP);
    } else if(0 != (flags & GPIO_PULL_DOWN)){
        PINCTRL_Pull((GIO_Index_t)pin,PINCTRL_PULL_DOWN);
    } else{
        PINCTRL_Pull((GIO_Index_t)pin,PINCTRL_PULL_DISABLE);
    }
	if ((flags & GPIO_SINGLE_ENDED) != 0) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_OUTPUT) != 0) {
        GIO_SetDirection((GIO_Index_t)pin,GIO_DIR_OUTPUT);
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			GIO_WriteValue(pin, 1);
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			GIO_WriteValue(pin, 0);
		}
	} else if ((flags & GPIO_INPUT) != 0) {
		GIO_SetDirection((GIO_Index_t)pin, GIO_DIR_INPUT);
	} else {
		/* Pin digital disable */
	}

	return 0;
}

static int gpio_ingchips_port_get_raw(const struct device *dev,
				       uint32_t *value)
{
    const struct gpio_ingchips_config *cfg = dev->config;
    uint32_t base = cfg->base;
    uint64_t  pin_mask;
    if(base == GPIO0_BASE){
        *value = (uint32_t)GIO_ReadAll();
    } else if(base == GPIO1_BASE){
        *value = (uint32_t)(GIO_ReadAll() >> GPIO1_PIN_MAX);
    } else{
        return -ENODEV;
    }
	return 0;
}

static int gpio_ingchips_port_set_masked_raw(const struct device *dev,
					      uint32_t mask,
					      uint32_t value)
{
    const struct gpio_ingchips_config *cfg = dev->config;
    uint32_t base = cfg->base;
    uint64_t  pin_mask;
    if(base == GPIO0_BASE){
        pin_mask = mask;
    } else if(base == GPIO1_BASE){
        pin_mask = ((uint64_t)mask) << GPIO1_PIN_MAX;
    } else{
        return -ENODEV;
    }

    GIO_ToggleBits(pin_mask);
	return 0;
}

static int gpio_ingchips_port_set_bits_raw(const struct device *dev,
					    uint32_t mask)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;
    uint64_t  pin_mask;
    if(base == GPIO0_BASE){
        pin_mask = mask;
    } else if(base == GPIO1_BASE){
        pin_mask = ((uint64_t)mask) << GPIO1_PIN_MAX;
    } else{
        return -ENODEV;
    }

    GIO_SetBits(pin_mask);

	return 0;
}

static int gpio_ingchips_port_clear_bits_raw(const struct device *dev,
					      uint32_t mask)
{
    const struct gpio_ingchips_config *cfg = dev->config;
    uint32_t base = cfg->base;
    uint64_t  pin_mask;
    if(base == GPIO0_BASE){
        pin_mask = mask;
    } else if(base == GPIO1_BASE){
        pin_mask = ((uint64_t)mask) << GPIO1_PIN_MAX;
    } else{
        return -ENODEV;
    }

    GIO_ClearBits(pin_mask);

	return 0;
}

static int gpio_ingchips_port_toggle_bits(const struct device *dev,
					   uint32_t mask)
{
    const struct gpio_ingchips_config *cfg = dev->config;
    uint32_t base = cfg->base;
    uint64_t  pin_mask;
    if(base == GPIO0_BASE){
        pin_mask = mask;
    } else if(base == GPIO1_BASE){
        pin_mask = ((uint64_t)mask) << GPIO1_PIN_MAX;
    } else{
        return -ENODEV;
    }

    GIO_ToggleBits(pin_mask);

	return 0;
}

static int gpio_ingchips_pin_interrupt_configure(const struct device *dev,
						  gpio_pin_t pin,
						  enum gpio_int_mode mode,
						  enum gpio_int_trig trig)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;
    uint8_t  io_int_trigger_type = 0, io_int_enable = 0;

	/* Check if GPIO port needs interrupt support */
	if (mode == GPIO_INT_MODE_DISABLED) {
		/* Set the mask to disable the interrupt */
        GIO_ConfigIntSource((GIO_Index_t)pin,0,io_int_trigger_type);
	} else {
		if (mode == GPIO_INT_MODE_EDGE) {
            io_int_trigger_type |= GIO_INT_EDGE;
		} else {
            io_int_trigger_type |= GIO_INT_LOGIC;
		}

		if (trig == GPIO_INT_TRIG_BOTH) {
            io_int_enable |= (GIO_INT_EN_LOGIC_HIGH_OR_RISING_EDGE | GIO_INT_EN_LOGIC_LOW_OR_FALLING_EDGE);
		} else if (trig == GPIO_INT_TRIG_HIGH) {
            io_int_enable |= GIO_INT_EN_LOGIC_HIGH_OR_RISING_EDGE;
		} else {
            io_int_enable |= GIO_INT_EN_LOGIC_LOW_OR_FALLING_EDGE;
		}
		/* Clear the Mask to enable the interrupt */
        GIO_ConfigIntSource((GIO_Index_t)pin,io_int_enable,io_int_trigger_type);
	}
	return 0;
}

static int gpio_ingchips_init(const struct device *dev)
{
    const struct gpio_ingchips_config * const cfg = dev->config;

    cfg->config_func(dev);
	return 0;
}

static int gpio_ingchips_manage_callback(const struct device *dev,
					  struct gpio_callback *callback,
					  bool set)
{
	struct gpio_ingchips_runtime *context = dev->data;

	gpio_manage_callback(&context->cb, callback, set);

	return 0;
}

static const struct gpio_driver_api gpio_ingchips_driver_api = {
	.pin_configure = gpio_ingchips_configure,
	.port_get_raw = gpio_ingchips_port_get_raw,
	.port_set_masked_raw = gpio_ingchips_port_set_masked_raw,
	.port_set_bits_raw = gpio_ingchips_port_set_bits_raw,
	.port_clear_bits_raw = gpio_ingchips_port_clear_bits_raw,
	.port_toggle_bits = gpio_ingchips_port_toggle_bits,
	.pin_interrupt_configure = gpio_ingchips_pin_interrupt_configure,
	.manage_callback = gpio_ingchips_manage_callback,
};

#define INGCHIPS_GPIO_DEVICE(n)							\
	static void port_## n ##_ingchips_config_func(const struct device *dev);		\
											\
	static struct gpio_ingchips_runtime port_## n ##_ingchips_runtime;		\
											\
	static const struct gpio_ingchips_config gpio_ingchips_port_## n ##_config = {\
		.common = {								\
			.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n),		\
		},									\
            .base = DT_INST_REG_ADDR(n),			\
		.port_map = BIT_MASK(DT_INST_PROP(n, ngpios)),		\
		.config_func = port_## n ##_ingchips_config_func,                        \
	};										\
											\
	DEVICE_DT_INST_DEFINE(n,							\
			    gpio_ingchips_init,					\
			    NULL,							\
			    &port_## n ##_ingchips_runtime,				\
			    &gpio_ingchips_port_## n ##_config,			\
			    POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY,			\
			    &gpio_ingchips_driver_api);				\
											\
	static void port_## n ##_ingchips_config_func(const struct device *dev)		\
	{										\
		irq_connect_dynamic(DT_INST_IRQN(n),			\
			    DT_INST_IRQ(n, priority),		\
			    gpio_ingchips_isr,						\
			    DEVICE_DT_INST_GET(n), 0);					\
											\
		irq_enable(DT_INST_IRQN(n));			\
	}
DT_INST_FOREACH_STATUS_OKAY(INGCHIPS_GPIO_DEVICE)
