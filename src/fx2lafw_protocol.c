/* fx2lafw commands and sample streaming through TinyUSB vendor bulk IN (0x82). */

#include "tusb.h"
#include "fx2lafw_capture.h"
#include "fx2lafw_protocol.h"
#include "board.h"

/* Command codes from libsigrok's protocol.h (fx2lafw driver). */
#define CMD_RESET                0xA0
#define CMD_GET_FW_VERSION       0xB0
#define CMD_START                0xB1
#define CMD_GET_REVID_VERSION    0xB2

#define CMD_START_FLAGS_SAMPLE_16BIT   (1U << 5)
#define CMD_START_FLAGS_CLK_48MHZ      (1U << 6)

/* libsigrok hard-checks major against FX2LAFW_REQUIRED_VERSION_MAJOR
 * (1) in fx2lafw_dev_open() and rejects the device on any other
 * value. */
#define FX2LAFW_FW_VERSION_MAJOR   1
#define FX2LAFW_FW_VERSION_MINOR   0
#define FX2LAFW_REVID              1

typedef struct TU_ATTR_PACKED {
    uint8_t flags;
    uint8_t sample_delay_h;
    uint8_t sample_delay_l;
} cmd_start_req_t;

_Static_assert(sizeof(cmd_start_req_t) == 3U, "START command is three bytes");
_Static_assert(CFG_TUD_VENDOR_TX_BUFSIZE >= FX2LAFW_CAPTURE_CHUNK_SIZE,
               "USB TX FIFO must hold a complete capture chunk");

static CFG_TUD_MEM_SECTION TU_ATTR_ALIGNED(4) cmd_start_req_t s_cmd_start_buf;
/* All protocol state and vendor callbacks run in the same USB task. */
static uint32_t s_pending_bytes;
static bool s_capture_running;

static void stop_capture(void)
{
    s_capture_running = false;
    fx2lafw_capture_stop();
    /* DWC2 reads this FIFO from its ISR for an already scheduled transfer.
     * Let queued bytes drain: clearing them leaves TXFE asserted while the
     * controller waits for the missing payload, starving the USB task. */
    board_usb_debug_copy(false);
    board_usb_debug_pending(s_pending_bytes != 0U);
    board_usb_debug_callback(false);
}

void fx2lafw_protocol_init(void)
{
    fx2lafw_capture_init();
}

static void send_capture_chunk(void)
{
    /* Check even when the USB FIFO is full or the host is suspended. */
    if (!s_capture_running) {
        return;
    }
    if (fx2lafw_capture_overrun()) {
        stop_capture();
        return;
    }
    if (!tud_ready() || !tud_vendor_mounted() ||
        tud_vendor_write_available() < FX2LAFW_CAPTURE_CHUNK_SIZE) {
        return;
    }

    const uint8_t *data = NULL;
    size_t length = 0;
    if (!fx2lafw_capture_poll_chunk(&data, &length)) {
        return;
    }

    board_usb_debug_copy(true);
    /* TinyUSB write() already starts or queues the full 1024-byte block. */
    uint32_t written = tud_vendor_write(data, (uint32_t)length);
    /* Account for accepted bytes before stopping on a short write or overrun:
     * the USB controller may already own these bytes. */
    s_pending_bytes += written;
    board_usb_debug_pending(s_pending_bytes != 0U);
    board_usb_debug_copy(false);
    if (written != length || fx2lafw_capture_overrun()) {
        stop_capture();
        return;
    }
    fx2lafw_capture_consume_chunk();
}

void fx2lafw_protocol_task(void)
{
    /* Bound the USB wait so capture data can be sent without a new USB event. */
    tud_task_ext(1, false);
    send_capture_chunk();
}

/* Same 30/48 MHz clock and divider as the host's fx2lafw driver. */
static uint32_t decode_sample_rate(uint8_t flags, uint16_t delay)
{
    uint32_t base_clk = (flags & CMD_START_FLAGS_CLK_48MHZ) ?
                        48000000UL : 30000000UL;
    return base_clk / ((uint32_t)delay + 1U);
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                               tusb_control_request_t const *request)
{
    /* Only START has work to do after the setup stage. */
    if (stage != CONTROL_STAGE_SETUP && request->bRequest != CMD_START) {
        return true;
    }
    switch (request->bRequest) {

    case CMD_RESET: /* Cypress EZ-USB CPUCS reset (Dummy ACK) */
        return tud_control_status(rhport, request);

    case CMD_GET_FW_VERSION: {
        static CFG_TUD_MEM_SECTION TU_ATTR_ALIGNED(4) const uint8_t vi[2] = {
            FX2LAFW_FW_VERSION_MAJOR,
            FX2LAFW_FW_VERSION_MINOR
        };
        return tud_control_xfer(rhport, request, (void *)vi, sizeof(vi));
    }

    case CMD_GET_REVID_VERSION: {
        static CFG_TUD_MEM_SECTION TU_ATTR_ALIGNED(4) const uint8_t revid = FX2LAFW_REVID;
        return tud_control_xfer(rhport, request, (void *)&revid, sizeof(revid));
    }

    case CMD_START:
        if (stage == CONTROL_STAGE_SETUP) {
            return tud_control_xfer(rhport, request, &s_cmd_start_buf,
                                    sizeof(s_cmd_start_buf));
        }

        if (stage == CONTROL_STAGE_ACK) {
            /* Decode only after the complete command has been received. */
            uint16_t delay = ((uint16_t)s_cmd_start_buf.sample_delay_h << 8) |
                              s_cmd_start_buf.sample_delay_l;
            uint32_t rate = decode_sample_rate(s_cmd_start_buf.flags, delay);
            bool wide = (s_cmd_start_buf.flags & CMD_START_FLAGS_SAMPLE_16BIT) != 0;

            fx2lafw_capture_start(rate, wide);
            s_capture_running = true;
            TU_LOG1("capture_start called, rate=%lu\r\n", rate);
        }
        return true;

    default:
        return false; /* STALL unknown requests. */
    }
}

void tud_umount_cb(void)
{
    stop_capture();
    /* TinyUSB has reset/closed the endpoints, cancelling queued transfers. */
    s_pending_bytes = 0;
    board_usb_debug_pending(false);
}

void tud_vendor_tx_cb(uint8_t idx, uint32_t sent_bytes)
{
    if (idx != 0U) {
        return;
    }

    board_usb_debug_callback(true);
    s_pending_bytes = (sent_bytes < s_pending_bytes) ?
                      s_pending_bytes - sent_bytes : 0U;
    board_usb_debug_pending(s_pending_bytes != 0U);
    send_capture_chunk();
    board_usb_debug_callback(false);
}
