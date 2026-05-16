#include "rtl8139.h"

#include "debug.h"
#include "io.h"
#include "net.h"
#include "pci.h"

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RTL8139_IDR0      0x00
#define RTL8139_TSD0      0x10
#define RTL8139_TSAD0     0x20
#define RTL8139_RBSTART   0x30
#define RTL8139_CAPR      0x38
#define RTL8139_CBR       0x3A
#define RTL8139_IMR       0x3C
#define RTL8139_ISR       0x3E
#define RTL8139_TCR       0x40
#define RTL8139_RCR       0x44
#define RTL8139_CONFIG1   0x52
#define RTL8139_CR        0x37

#define RTL8139_CMD_BUFE  0x01
#define RTL8139_CMD_TE    0x04
#define RTL8139_CMD_RE    0x08
#define RTL8139_CMD_RST   0x10

#define RTL8139_ISR_TOK   0x0004
#define RTL8139_ISR_ROK   0x0001

#define RTL8139_RX_OK     0x0001
#define RTL8139_TX_OK     (1U << 15)
#define RTL8139_TX_ERR    (1U << 30)

#define RTL8139_RX_BUFFER_SIZE 8192
#define RTL8139_RX_BUFFER_PAD  16
#define RTL8139_RX_OVERFLOW    1500
#define RTL8139_TX_BUFFER_SIZE 2048

static uint16_t rtl8139_io_base = 0;
static uint8_t rtl8139_mac[6];
static uint8_t rtl8139_irq_line = 0xFF;
static uint32_t rtl8139_rx_offset = 0;
static uint32_t rtl8139_current_tx = 0;
static int rtl8139_ready = 0;

static uint8_t rtl8139_rx_buffer[RTL8139_RX_BUFFER_SIZE + RTL8139_RX_BUFFER_PAD + RTL8139_RX_OVERFLOW]
    __attribute__((aligned(16)));
static uint8_t rtl8139_tx_buffers[4][RTL8139_TX_BUFFER_SIZE] __attribute__((aligned(16)));

static void rtl8139_memcpy(uint8_t *dest, const uint8_t *src, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }
}

static uint8_t rtl8139_read8(uint16_t reg) {
    return inb((uint16_t)(rtl8139_io_base + reg));
}

static uint16_t rtl8139_read16(uint16_t reg) {
    return inw((uint16_t)(rtl8139_io_base + reg));
}

static uint32_t rtl8139_read32(uint16_t reg) {
    return inl((uint16_t)(rtl8139_io_base + reg));
}

static void rtl8139_write8(uint16_t reg, uint8_t value) {
    outb((uint16_t)(rtl8139_io_base + reg), value);
}

static void rtl8139_write16(uint16_t reg, uint16_t value) {
    outw((uint16_t)(rtl8139_io_base + reg), value);
}

static void rtl8139_write32(uint16_t reg, uint32_t value) {
    outl((uint16_t)(rtl8139_io_base + reg), value);
}

int rtl8139_init(void) {
    pci_device_info_t nic_info;
    if (!pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &nic_info)) {
        kdebug_write_line("[net] rtl8139 PCI device not found.");
        return 0;
    }

    kdebug_write("[net] rtl8139 located at PCI ");
    kdebug_write_dec(nic_info.bus);
    kdebug_put(':');
    kdebug_write_dec(nic_info.slot);
    kdebug_put('.');
    kdebug_write_dec(nic_info.function);
    kdebug_put('\n');

    rtl8139_irq_line = nic_info.irq_line;
    rtl8139_io_base = (uint16_t)(nic_info.bar0 & 0xFFFC);

    if ((nic_info.bar0 & 0x1) == 0 || rtl8139_io_base == 0) {
        kdebug_write("[net] rtl8139 BAR0 is not an I/O BAR: ");
        kdebug_write_hex(nic_info.bar0);
        kdebug_put('\n');
        return 0;
    }

    uint32_t pci_command = pci_config_read_dword(nic_info.bus, nic_info.slot, nic_info.function, 0x04);
    pci_command |= 0x00000005;
    pci_config_write_dword(nic_info.bus, nic_info.slot, nic_info.function, 0x04, pci_command);

    kdebug_write("[net] rtl8139 I/O base: ");
    kdebug_write_hex(rtl8139_io_base);
    kdebug_put('\n');
    kdebug_write("[net] rtl8139 IRQ line: ");
    kdebug_write_dec(rtl8139_irq_line);
    kdebug_put('\n');

    rtl8139_write8(RTL8139_CONFIG1, 0x00);
    rtl8139_write8(RTL8139_CR, RTL8139_CMD_RST);
    for (uint32_t i = 0; i < 100000; i++) {
        if ((rtl8139_read8(RTL8139_CR) & RTL8139_CMD_RST) == 0) {
            break;
        }
        if (i == 99999) {
            kdebug_write_line("[net] rtl8139 reset timed out.");
            return 0;
        }
    }

    rtl8139_write32(RTL8139_RBSTART, (uint32_t)(uintptr_t)rtl8139_rx_buffer);
    for (uint32_t i = 0; i < 4; i++) {
        rtl8139_write32((uint16_t)(RTL8139_TSAD0 + (i * 4)), (uint32_t)(uintptr_t)rtl8139_tx_buffers[i]);
    }

    rtl8139_write16(RTL8139_IMR, 0x0000);
    rtl8139_write16(RTL8139_ISR, 0xFFFF);
    rtl8139_write32(RTL8139_TCR, (6U << 8));
    rtl8139_write32(RTL8139_RCR, 0x0000068F);
    rtl8139_write16(RTL8139_CAPR, 0x0000);
    rtl8139_write8(RTL8139_CR, (uint8_t)(RTL8139_CMD_TE | RTL8139_CMD_RE));

    for (uint32_t i = 0; i < 6; i++) {
        rtl8139_mac[i] = rtl8139_read8((uint16_t)(RTL8139_IDR0 + i));
    }

    rtl8139_rx_offset = 0;
    rtl8139_current_tx = 0;
    rtl8139_ready = 1;

    kdebug_write("[net] rtl8139 MAC address: ");
    for (uint32_t i = 0; i < 6; i++) {
        uint8_t byte = rtl8139_mac[i];
        char high = (char)((byte >> 4) & 0x0F);
        char low = (char)(byte & 0x0F);
        kdebug_put((char)(high < 10 ? ('0' + high) : ('A' + (high - 10))));
        kdebug_put((char)(low < 10 ? ('0' + low) : ('A' + (low - 10))));
        if (i != 5) {
            kdebug_put(':');
        }
    }
    kdebug_put('\n');
    kdebug_write_line("[net] rtl8139 RX/TX engines enabled.");
    return 1;
}

void rtl8139_poll(void) {
    if (!rtl8139_ready) {
        return;
    }

    uint16_t isr = rtl8139_read16(RTL8139_ISR);
    if (isr != 0) {
        rtl8139_write16(RTL8139_ISR, isr);
    }

    while ((rtl8139_read8(RTL8139_CR) & RTL8139_CMD_BUFE) == 0) {
        uint8_t *packet = rtl8139_rx_buffer + rtl8139_rx_offset;
        uint16_t status = (uint16_t)(packet[0] | (packet[1] << 8));
        uint16_t length = (uint16_t)(packet[2] | (packet[3] << 8));

        if ((status & RTL8139_RX_OK) == 0 || length < 4 || length > 1792) {
            kdebug_write("[net] rtl8139 dropped malformed RX frame. status=");
            kdebug_write_hex(status);
            kdebug_write(" length=");
            kdebug_write_dec(length);
            kdebug_put('\n');
            rtl8139_rx_offset = 0;
            rtl8139_write16(RTL8139_CAPR, 0xFFF0);
            break;
        }

        uint32_t frame_length = (uint32_t)(length - 4);
        net_handle_frame(packet + 4, frame_length);

        rtl8139_rx_offset = (rtl8139_rx_offset + length + 4 + 3) & ~3U;
        rtl8139_rx_offset %= RTL8139_RX_BUFFER_SIZE;
        rtl8139_write16(RTL8139_CAPR, (uint16_t)((rtl8139_rx_offset - 16) & 0xFFFF));
    }

    if (isr & RTL8139_ISR_ROK) {
        uint16_t cbr = rtl8139_read16(RTL8139_CBR);
        kdebug_write("[net] rtl8139 RX poll complete. CBR=");
        kdebug_write_hex(cbr);
        kdebug_put('\n');
    }
}

int rtl8139_send(const void *data, uint32_t length) {
    if (!rtl8139_ready || data == 0 || length == 0 || length > RTL8139_TX_BUFFER_SIZE) {
        return 0;
    }

    uint32_t slot = rtl8139_current_tx;
    rtl8139_current_tx = (rtl8139_current_tx + 1) % 4;

    rtl8139_memcpy(rtl8139_tx_buffers[slot], (const uint8_t *)data, length);
    rtl8139_write32((uint16_t)(RTL8139_TSD0 + (slot * 4)), length);

    for (uint32_t spin = 0; spin < 1000000; spin++) {
        uint32_t status = rtl8139_read32((uint16_t)(RTL8139_TSD0 + (slot * 4)));
        if (status & RTL8139_TX_OK) {
            kdebug_write("[net] rtl8139 transmitted frame from slot ");
            kdebug_write_dec(slot);
            kdebug_write(" length ");
            kdebug_write_dec(length);
            kdebug_put('\n');
            return 1;
        }
        if (status & RTL8139_TX_ERR) {
            kdebug_write("[net] rtl8139 transmit error in slot ");
            kdebug_write_dec(slot);
            kdebug_write(" status=");
            kdebug_write_hex(status);
            kdebug_put('\n');
            return 0;
        }
    }

    kdebug_write_line("[net] rtl8139 transmit timed out.");
    return 0;
}

const uint8_t* rtl8139_get_mac(void) {
    return rtl8139_mac;
}

int rtl8139_is_ready(void) {
    return rtl8139_ready;
}
