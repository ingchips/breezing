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

#define GPIO_REG_ADDR(base, offset) (base + offset)

#define GPIO_RW_ADDR(base, offset, p)			 \
	(GPIO_REG_ADDR(base, offset) | (1 << (p + 2)))

#define GPIO_RW_MASK_ADDR(base, offset, mask)		 \
	(GPIO_REG_ADDR(base, offset) | (mask << 2))

enum gpio_regs {
	GPIO_DATA_OFFSET = 0x000,
	GPIO_DIR_OFFSET = 0x400,
	GPIO_DEN_OFFSET = 0x51C,
	GPIO_IS_OFFSET = 0x404,
	GPIO_IBE_OFFSET = 0x408,
	GPIO_IEV_OFFSET = 0x40C,
	GPIO_IM_OFFSET = 0x410,
	GPIO_MIS_OFFSET = 0x418,
	GPIO_ICR_OFFSET = 0x41C,
};

static void gpio_ingchips_isr(const struct device *dev)
{
	const struct gpio_ingchips_config * const cfg = dev->config;
	struct gpio_ingchips_runtime *context = dev->data;
	uint32_t base = cfg->base;
	uint32_t int_stat = sys_read32(GPIO_REG_ADDR(base, GPIO_MIS_OFFSET));

	gpio_fire_callbacks(&context->cb, dev, int_stat);

	sys_write32(int_stat, GPIO_REG_ADDR(base, GPIO_ICR_OFFSET));
}

static int gpio_ingchips_configure(const struct device *dev,
				    gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;

	
	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_SINGLE_ENDED) != 0) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_OUTPUT) != 0) {
		mm_reg_t mask_addr;

		mask_addr = GPIO_RW_MASK_ADDR(base, GPIO_DATA_OFFSET, BIT(pin));
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			GIO_WriteValue(pin, 1);
			
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			GIO_WriteValue(pin, 0);
		}
		GIO_SetDirection(pin, 1);
	} else if ((flags & GPIO_INPUT) != 0) {
		GIO_SetDirection(pin, 0);
	} else {
		/* Pin digital disable */
		
	}

	return 0;
}

static int gpio_ingchips_port_get_raw(const struct device *dev,
				       uint32_t *value)
{
	*value = (uint32_t)GIO_ReadAll();//GIO_ReadAll return uint64

	return 0;
}

static int gpio_ingchips_port_set_masked_raw(const struct device *dev,
					      uint32_t mask,
					      uint32_t value)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;

	sys_write32(value, GPIO_RW_MASK_ADDR(base, GPIO_DATA_OFFSET, mask));

	return 0;
}

static int gpio_ingchips_port_set_bits_raw(const struct device *dev,
					    uint32_t mask)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;

	sys_write32(mask, GPIO_RW_MASK_ADDR(base, GPIO_DATA_OFFSET, mask));

	return 0;
}

static int gpio_ingchips_port_clear_bits_raw(const struct device *dev,
					      uint32_t mask)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;

	sys_write32(0, GPIO_RW_MASK_ADDR(base, GPIO_DATA_OFFSET, mask));

	return 0;
}

static int gpio_ingchips_port_toggle_bits(const struct device *dev,
					   uint32_t mask)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;
	uint32_t value;

	value = sys_read32(GPIO_RW_MASK_ADDR(base, GPIO_DATA_OFFSET, 0xff));
	value ^= mask;
	sys_write32(value, GPIO_RW_MASK_ADDR(base, GPIO_DATA_OFFSET, 0xff));

	return 0;
}

static int gpio_ingchips_pin_interrupt_configure(const struct device *dev,
						  gpio_pin_t pin,
						  enum gpio_int_mode mode,
						  enum gpio_int_trig trig)
{
	const struct gpio_ingchips_config *cfg = dev->config;
	uint32_t base = cfg->base;

	/* Check if GPIO port needs interrupt support */
	if (mode == GPIO_INT_MODE_DISABLED) {
		/* Set the mask to disable the interrupt */
		sys_set_bit(GPIO_REG_ADDR(base, GPIO_IM_OFFSET), pin);
	} else {
		if (mode == GPIO_INT_MODE_EDGE) {
			sys_clear_bit(GPIO_REG_ADDR(base, GPIO_IS_OFFSET), pin);
		} else {
			sys_set_bit(GPIO_REG_ADDR(base, GPIO_IS_OFFSET), pin);
		}

		if (trig == GPIO_INT_TRIG_BOTH) {
			sys_set_bit(GPIO_REG_ADDR(base, GPIO_IBE_OFFSET), pin);
		} else if (trig == GPIO_INT_TRIG_HIGH) {
			sys_set_bit(GPIO_REG_ADDR(base, GPIO_IEV_OFFSET), pin);
		} else {
			sys_clear_bit(GPIO_REG_ADDR(base,
						    GPIO_IEV_OFFSET), pin);
		}
		/* Clear the Mask to enable the interrupt */
		sys_clear_bit(GPIO_REG_ADDR(base, GPIO_IM_OFFSET), pin);
	}

	return 0;
}

static int gpio_ingchips_init(const struct device *dev)
{

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
		.config_func = port_## n ##_ingchips_config_func,			\
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
		IRQ_CONNECT(DT_INST_IRQN(n),			\
			    DT_INST_IRQ(n, priority),		\
			    gpio_ingchips_isr,						\
			    DEVICE_DT_INST_GET(n), 0);					\
											\
		irq_enable(DT_INST_IRQN(n));			\
	}

DT_INST_FOREACH_STATUS_OKAY(INGCHIPS_GPIO_DEVICE)
