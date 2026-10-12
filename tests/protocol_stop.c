/* Host regression tests for stopping capture while DWC2 owns TX FIFO data.
 * Uses the real protocol and TinyUSB FIFO; USB scheduling is a bounded model.
 * Run with: python tests/run_protocol_stop.py
 */
#include <stdio.h>
#include <stdlib.h>
#include "tusb.h"
#include "common/tusb_fifo.h"
#include "fx2lafw_protocol.h"
#include "fx2lafw_capture.h"
#include "board.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static tu_fifo_t tx_fifo;
static uint8_t tx_storage[1024];
static uint8_t samples[1024];
static uint32_t scheduled_bytes;
static uint32_t remaining_bytes;
static uint32_t write_limit;
static uint32_t consumed_chunks;
static bool capture_running;
static bool overrun;
static bool overrun_on_write;
static bool pending_pin;
static bool suspended;
static bool chunk_ready;
static uint8_t start_command[3] = {0x40, 0, 47};
static uint32_t capture_rate;
static bool capture_wide;
static unsigned control_replies;
static unsigned capture_starts;

bool tud_mounted(void) { return true; }
bool tud_suspended(void) { return suspended; }
bool tud_vendor_n_mounted(uint8_t idx) { CHECK(idx == 0); return true; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr)
{
    CHECK(timeout_ms == 1 && !in_isr);
}

uint32_t tud_vendor_n_write_available(uint8_t idx)
{
    CHECK(idx == 0);
    return tu_fifo_remaining(&tx_fifo);
}

uint32_t tud_vendor_n_write_flush(uint8_t idx)
{
    CHECK(idx == 0);
    if (scheduled_bytes != 0)
        return 0;
    scheduled_bytes = tu_fifo_count(&tx_fifo);
    remaining_bytes = scheduled_bytes;
    return scheduled_bytes;
}

uint32_t tud_vendor_n_write(uint8_t idx, const void *data, uint32_t length)
{
    CHECK(idx == 0 && length <= sizeof(tx_storage));
    uint16_t written = tu_fifo_write_n(&tx_fifo, data,
                                      (uint16_t)tu_min32(length, write_limit));
    tud_vendor_n_write_flush(idx); /* TinyUSB write() automatically flushes. */
    if (overrun_on_write)
        overrun = true;
    return written;
}

bool tud_vendor_n_write_clear(uint8_t idx)
{
    CHECK(idx == 0);
    tu_fifo_clear(&tx_fifo);
    return true;
}

bool tud_control_status(uint8_t rhport, const tusb_control_request_t *request)
{
    CHECK(rhport == 0 && request != NULL);
    control_replies++;
    return true;
}

bool tud_control_xfer(uint8_t rhport, const tusb_control_request_t *request,
                      void *buffer, uint16_t length)
{
    CHECK(rhport == 0);
    control_replies++;
    if (request->bRequest == 0xB1) {
        CHECK(length == sizeof(start_command));
        memcpy(buffer, start_command, sizeof(start_command));
    } else if (request->bRequest == 0xB2) {
        CHECK(length == 1 && *(const uint8_t *)buffer == 1);
    } else {
        CHECK(request->bRequest == 0xB0 && length == 2);
        CHECK(((const uint8_t *)buffer)[0] == 1 &&
              ((const uint8_t *)buffer)[1] == 0);
    }
    return true;
}

void fx2lafw_capture_init(void) {}
void fx2lafw_capture_start(uint32_t rate, bool wide)
{
    capture_rate = rate;
    capture_wide = wide;
    capture_starts++;
    capture_running = true;
}
void fx2lafw_capture_stop(void) { capture_running = false; }
bool fx2lafw_capture_overrun(void) { return overrun; }
bool fx2lafw_capture_poll_chunk(const uint8_t **data, size_t *length)
{
    if (!chunk_ready)
        return false;
    *data = samples;
    *length = sizeof(samples);
    return true;
}
void fx2lafw_capture_consume_chunk(void)
{
    consumed_chunks++;
    chunk_ready = false;
}
void board_usb_debug_copy(bool active) { (void)active; }
void board_usb_debug_callback(bool active) { (void)active; }
void board_usb_debug_pending(bool active) { pending_pin = active; }

static void start_capture(void)
{
    const tusb_control_request_t start = {.bRequest = 0xB1, .wLength = 3};
    CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &start));
    CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_ACK, &start));
    CHECK(capture_running && capture_rate == 1000000 && !capture_wide);
}

static void check_version(void)
{
    const tusb_control_request_t version = {.bRequest = 0xB0, .wLength = 2};
    CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &version));
}

static void reset_test(void)
{
    /* TinyUSB resets its endpoints and FIFO before invoking umount_cb(). */
    CHECK(tu_fifo_config(&tx_fifo, tx_storage, sizeof(tx_storage), false));
    scheduled_bytes = remaining_bytes = 0;
    tud_umount_cb();
    write_limit = sizeof(tx_storage);
    consumed_chunks = 0;
    overrun = overrun_on_write = suspended = chunk_ready = false;
    start_capture();
}

static void queue_chunk(void)
{
    chunk_ready = true;
    fx2lafw_protocol_task();
    CHECK(scheduled_bytes != 0);
}

static void read_packet(void)
{
    uint8_t packet[64];
    uint16_t length = (uint16_t)tu_min32(remaining_bytes, sizeof(packet));
    /* An empty FIFO here leaves the real controller's TXFE asserted. */
    CHECK(tu_fifo_read_n(&tx_fifo, packet, length) == length);
    for (uint16_t i = 0; i < length; i++)
        CHECK(packet[i] == 0xA5);
    remaining_bytes -= length;
}

static void complete_transfer(void)
{
    CHECK(remaining_bytes == 0);
    uint32_t sent = scheduled_bytes;
    scheduled_bytes = 0;
    tud_vendor_tx_cb(0, sent);
    /* Mirrors vendord_xfer_cb() scheduling the next FIFO transfer. */
    tud_vendor_n_write_flush(0);
}

static void drain(void)
{
    for (unsigned packets = 0; scheduled_bytes != 0; packets++) {
        CHECK(packets < 64);
        if (remaining_bytes != 0)
            read_packet();
        if (remaining_bytes == 0)
            complete_transfer();
    }
    CHECK(tu_fifo_empty(&tx_fifo) && !pending_pin);
    check_version();
}

int main(void)
{
    memset(samples, 0xA5, sizeof(samples));
    fx2lafw_protocol_init();

    reset_test();
    queue_chunk();
    read_packet();
    overrun = true;
    fx2lafw_protocol_task();
    CHECK(!capture_running);
    drain();
    puts("PASS: overrun during active transfer retains scheduled payload");

    reset_test();
    queue_chunk();
    while (remaining_bytes != 0)
        read_packet();
    queue_chunk(); /* First completion event has not been processed yet. */
    overrun = true;
    fx2lafw_protocol_task();
    CHECK(!capture_running);
    complete_transfer();
    drain();
    puts("PASS: queued transfer drains after delayed completion");

    reset_test();
    overrun_on_write = true;
    queue_chunk();
    CHECK(!capture_running && consumed_chunks == 0 && pending_pin);
    drain();
    puts("PASS: overrun inside write retains USB ownership");

    reset_test();
    write_limit = 512;
    queue_chunk();
    CHECK(!capture_running && consumed_chunks == 0 && pending_pin);
    drain();
    puts("PASS: short write stops capture and drains accepted bytes");

    reset_test();
    queue_chunk();
    suspended = overrun = true;
    fx2lafw_protocol_task();
    CHECK(!capture_running && pending_pin);
    suspended = false;
    drain();
    puts("PASS: suspended host resumes pending transfer after overrun");

    reset_test();
    overrun = true;
    fx2lafw_protocol_task();
    CHECK(!capture_running && !pending_pin);
    overrun = false;
    start_capture();
    queue_chunk();
    drain();
    puts("PASS: empty overrun allows control request and restart");

    reset_test();
    queue_chunk();
    read_packet();
    reset_test();
    CHECK(!pending_pin);
    queue_chunk();
    drain();
    puts("PASS: unmount cancels pending accounting before next capture");

    reset_test();
    for (unsigned i = 0; i < 256; i++) {
        queue_chunk();
        drain();
        CHECK(capture_running);
    }
    CHECK(consumed_chunks == 256);
    puts("PASS: 256 KiB stream with repeated FIFO index wraps");

    const uint8_t requests[] = {0xA0, 0xB0, 0xB2, 0xFF};
    for (size_t i = 0; i < sizeof(requests); i++) {
        const tusb_control_request_t request = {.bRequest = requests[i]};
        unsigned before = control_replies;
        bool supported = requests[i] != 0xFF;
        CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request) == supported);
        CHECK(control_replies == before + (supported ? 1U : 0U));
        before = control_replies;
        CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_DATA, &request));
        CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_ACK, &request));
        CHECK(control_replies == before);
    }
    puts("PASS: RESET, version, revision and unknown command stages are unchanged");

    const struct {
        uint8_t flags;
        uint16_t delay;
        uint32_t rate;
    } rates[] = {
        {0x00, 29, 1000000}, {0x40, 95, 500000},
        {0x60, 47, 1000000}, {0x40, 0, 48000000},
        {0x00, UINT16_MAX, 457}, {0x40, UINT16_MAX, 732}
    };
    const tusb_control_request_t start = {.bRequest = 0xB1, .wLength = 3};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        tud_umount_cb();
        start_command[0] = rates[i].flags;
        start_command[1] = (uint8_t)(rates[i].delay >> 8);
        start_command[2] = (uint8_t)rates[i].delay;
        unsigned before = capture_starts;
        CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &start));
        CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_DATA, &start));
        CHECK(!capture_running && capture_starts == before);
        CHECK(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_ACK, &start));
        CHECK(capture_running && capture_starts == before + 1U);
        CHECK(capture_rate == rates[i].rate);
        CHECK(capture_wide == ((rates[i].flags & 0x20) != 0));
    }
    puts("PASS: START waits for ACK and preserves clock, divider and width flags");
    return 0;
}
