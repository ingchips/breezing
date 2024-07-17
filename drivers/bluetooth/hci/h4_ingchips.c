/* h4.c - H:4 UART based Bluetooth driver */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>

#include <zephyr/init.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include "platform_api.h"

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_driver);

#include "common/bt_str.h"
#include "bluetooth.h"

#define H4_NONE 0x00
#define H4_CMD  0x01
#define H4_ACL  0x02
#define H4_SCO  0x03
#define H4_EVT  0x04
#define H4_ISO  0x05

#define HCI_BT_HCI_SEND_TIMEOUT K_MSEC(2000)

const platform_hci_link_layer_interf_t *hci_interf = NULL;

#pragma pack (push, 1)
struct hci_command_packet_header
{
    uint16_t opcode;
    uint8_t param_len;
};

struct hci_acl_packet_header
{
    uint16_t conn_handle_flags;
    uint16_t data_length;
};

struct hci_event_packet_header
{
    uint8_t code;
    uint8_t param_len;
};
#pragma pack (pop)

K_SEM_DEFINE(recv_sem, 0, 1);

static struct {
	struct net_buf *buf;
	struct k_fifo   fifo;

	uint16_t    remaining;
	uint16_t    discard;

	bool     have_hdr;
	bool     discardable;

	uint8_t     hdr_len;

	uint8_t     type;
	union {
		struct bt_hci_evt_hdr evt;
		struct bt_hci_acl_hdr acl;
		struct bt_hci_iso_hdr iso;
		uint8_t hdr[4];
	};
} rx = {
	.fifo = Z_FIFO_INITIALIZER(rx.fifo),
};

static struct {
	uint8_t type;
	struct net_buf *buf;
	struct k_fifo   fifo;
} tx = {
	.fifo = Z_FIFO_INITIALIZER(tx.fifo),
};

static const struct device *const h4_dev = NULL;

static inline void h4_get_type(void)
{
	/* Get packet type */
	if (hci_rec_buf_read(h4_dev, &rx.type, 1) != 1) {
		LOG_WRN("Unable to read H:4 packet type");
		rx.type = H4_NONE;
		return;
	}

	switch (rx.type) {
	case H4_EVT:
		rx.remaining = sizeof(rx.evt);
		rx.hdr_len = rx.remaining;
		break;
	case H4_ACL:
		rx.remaining = sizeof(rx.acl);
		rx.hdr_len = rx.remaining;
		break;
	case H4_ISO:
		if (IS_ENABLED(CONFIG_BT_ISO)) {
			rx.remaining = sizeof(rx.iso);
			rx.hdr_len = rx.remaining;
			break;
		}
		__fallthrough;
	default:
		LOG_ERR("Unknown H:4 type 0x%02x", rx.type);
		rx.type = H4_NONE;
	}
	LOG_DBG("rx.remaining is[%d]", rx.remaining);
}

static bool is_hci_event_discardable(const uint8_t *evt_data)
{
	uint8_t evt_type = evt_data[0];

	switch (evt_type) {
#if defined(CONFIG_BT_BREDR)
	case BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI:
	case BT_HCI_EVT_EXTENDED_INQUIRY_RESULT:
		return true;
#endif
	case BT_HCI_EVT_LE_META_EVENT: {
		uint8_t subevt_type = evt_data[sizeof(struct bt_hci_evt_hdr)];

		switch (subevt_type) {
		case BT_HCI_EVT_LE_ADVERTISING_REPORT:
		case BT_HCI_EVT_LE_EXT_ADVERTISING_REPORT:
		case BT_HCI_EVT_LE_PER_ADVERTISING_REPORT:
		case BT_HCI_EVT_LE_PER_ADVERTISING_REPORT_V2:
			return true;
		default:
			return false;
		}
	}
	default:
		return false;
	}
}

static struct net_buf *bt_esp_evt_recv(uint8_t *data, size_t remaining)
{
	bool discardable = false;
	struct bt_hci_evt_hdr hdr;
	struct net_buf *buf;
	size_t buf_tailroom;

	if (remaining < sizeof(hdr)) {
		LOG_ERR("Not enough data for event header");
		return NULL;
	}

	discardable = is_hci_event_discardable(data);

	memcpy((void *)&hdr, data, sizeof(hdr));
	data += sizeof(hdr);
	remaining -= sizeof(hdr);

	if (remaining != hdr.len) {
		LOG_ERR("Event payload length is not correct");
		return NULL;
	}
	LOG_DBG("len %u", hdr.len);

	buf = bt_buf_get_evt(hdr.evt, discardable, K_NO_WAIT);
	if (!buf) {
		if (discardable) {
			LOG_DBG("Discardable buffer pool full, ignoring event");
		} else {
			LOG_ERR("No available event buffers!");
		}
		return buf;
	}

	net_buf_add_mem(buf, &hdr, sizeof(hdr));

	buf_tailroom = net_buf_tailroom(buf);
	if (buf_tailroom < remaining) {
		LOG_ERR("Not enough space in buffer %zu/%zu", remaining, buf_tailroom);
		net_buf_unref(buf);
		return NULL;
	}

	net_buf_add_mem(buf, data, remaining);

	return buf;
}

static struct net_buf *bt_esp_acl_recv(uint8_t *data, size_t remaining)
{
	struct bt_hci_acl_hdr hdr;
	struct net_buf *buf;
	size_t buf_tailroom;

	if (remaining < sizeof(hdr)) {
		LOG_ERR("Not enough data for ACL header");
		return NULL;
	}

	buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_NO_WAIT);
	if (buf) {
		memcpy((void *)&hdr, data, sizeof(hdr));
		data += sizeof(hdr);
		remaining -= sizeof(hdr);

		net_buf_add_mem(buf, &hdr, sizeof(hdr));
	} else {
		LOG_ERR("No available ACL buffers!");
		return NULL;
	}

	if (remaining != sys_le16_to_cpu(hdr.len)) {
		LOG_ERR("ACL payload length is not correct");
		net_buf_unref(buf);
		return NULL;
	}

	buf_tailroom = net_buf_tailroom(buf);
	if (buf_tailroom < remaining) {
		LOG_ERR("Not enough space in buffer %zu/%zu", remaining, buf_tailroom);
		net_buf_unref(buf);
		return NULL;
	}

	LOG_DBG("len %u", remaining);
	net_buf_add_mem(buf, data, remaining);

	return buf;
}

uint32_t cb_hci_recv(const platform_hci_recv_t *msg, void *_)
{
	uint8_t pkt_indicator;
	struct net_buf *buf = NULL;
	size_t remaining = msg->len_of_hci;
	uint8_t *data = msg->buff;
	pkt_indicator = msg->hci_type;
	LOG_HEXDUMP_DBG(data, remaining, "host packet data:");

	static bool first_event = true;
	if (first_event)
	{
		// skip RESET event
		first_event = false;
		LOG_DBG("drop hci rec data");
		goto end;
	}

	switch (pkt_indicator)
	{
	case H4_EVT:
		buf = bt_esp_evt_recv(data, remaining);
		break;

	case H4_ACL:
		buf = bt_esp_acl_recv(data, remaining);
		break;

	default:
		LOG_ERR("Unknown HCI type %u", pkt_indicator);
		return -1;
	}

	if (buf)
	{
		LOG_DBG("Calling bt_recv(%p)", buf);
		bt_recv(buf);
	}

end:
    switch (msg->hci_type)
    {
    case HCI_EVENT_PACKET:
        hci_interf->hci_event_processed(msg->handle);
        break;
    case HCI_ACL_DATA_PACKET:
        hci_interf->acl_data_processed(msg->conn_handle, msg->handle);
        break;
    }
    return 0;
}

static void host_send_packet_to_controller(uint8_t *buffer, uint16_t rx_len)
{
	LOG_HEXDUMP_DBG(buffer, rx_len, "to controller");
	switch (buffer[0])
    {
    case HCI_COMMAND_DATA_PACKET:
        if (rx_len >= 1 + sizeof(struct hci_command_packet_header))
        {
            struct hci_command_packet_header *header = (struct hci_command_packet_header *)(buffer + 1);
            uint8_t total_len = 1 + sizeof(struct hci_command_packet_header) + header->param_len;
            if (rx_len == total_len)
            {
                uint16_t opcode = header->opcode;
                hci_interf->send_hci_command(opcode,
                    buffer + 1 + sizeof(struct hci_command_packet_header),
                    header->param_len);
                rx_len = 0;
				LOG_DBG("CMD SEND");
            }
        }
        break;
    case HCI_ACL_DATA_PACKET:
        if (rx_len >= 1 + sizeof(struct hci_acl_packet_header))
        {
            struct hci_acl_packet_header *header = (struct hci_acl_packet_header *)(buffer + 1);
            uint16_t data_len = header->data_length;
            uint16_t total_len = 1 + sizeof(struct hci_acl_packet_header) + data_len;
            if (rx_len == total_len)
            {
                uint16_t conn_handle = header->conn_handle_flags & 0x0fff;
                uint8_t pbcb = header->conn_handle_flags >> 12;
                hci_interf->send_acl_data(conn_handle, pbcb, data_len,
                    buffer + 1 + sizeof(struct hci_acl_packet_header),
                    data_len);
                rx_len = 0;
				LOG_DBG("ACL DATA send");
            }
        }
        break;
    default:
        platform_reset();
        break;
    }
}
static int h4_send(struct net_buf *buf)
{
	int err = 0;
	uint8_t pkt_indicator;

	LOG_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_ACL_OUT:
		pkt_indicator = H4_ACL;
		break;
	case BT_BUF_CMD:
		pkt_indicator = H4_CMD;
		break;
	default:
		LOG_ERR("Unknown type %u", bt_buf_get_type(buf));
		goto done;
	}
	net_buf_push_u8(buf, pkt_indicator);

	host_send_packet_to_controller(buf->data, buf->len);

done:
	net_buf_unref(buf);

	return err;
}

static int h4_open(void)
{
	int ret;
    hci_interf = (const platform_hci_link_layer_interf_t *)platform_get_link_layer_interf();
	return 0;
}

#if defined(CONFIG_BT_HCI_SETUP)
static int h4_setup(void)
{
	/* Extern bt_h4_vnd_setup function.
	 * This function executes vendor-specific commands sequence to
	 * initialize BT Controller before BT Host executes Reset sequence.
	 * bt_h4_vnd_setup function must be implemented in vendor-specific HCI
	 * extansion module if CONFIG_BT_HCI_SETUP is enabled.
	 */
	extern int bt_h4_vnd_setup(const struct device *dev);

	return bt_h4_vnd_setup(h4_dev);
}
#endif

static const struct bt_hci_driver drv = {
	.name		= "H:4",
	.bus		= BT_HCI_DRIVER_BUS_UART,
	.open		= h4_open,
	.send		= h4_send,
#if defined(CONFIG_BT_HCI_SETUP)
	.setup		= h4_setup
#endif
};

static int bt_ingchips_init(void)
{
    LOG_DBG("run @ %s %d\r\n", __FILE__, __LINE__);
	bt_hci_driver_register(&drv);
	if (hci_interf == NULL) {
		hci_interf = (const platform_hci_link_layer_interf_t *)platform_get_link_layer_interf();
	}
	return 0;
}

SYS_INIT(bt_ingchips_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
