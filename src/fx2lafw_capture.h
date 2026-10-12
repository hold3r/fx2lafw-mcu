/*
 * fx2lafw_capture.h
 *
 * Contract for the capture backend. Each selected src/boards/<board>
 * implements these functions and adds its sources in its CMakeLists.txt.
 * The protocol calls this interface without MCU-specific dependencies.
 */

#ifndef FX2LAFW_CAPTURE_H
#define FX2LAFW_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Fixed block size shared by the DMA backend and USB protocol. */
#define FX2LAFW_CAPTURE_CHUNK_SIZE 1024U

/* One-time hardware init. Call once at startup. */
void fx2lafw_capture_init(void);

/* Arms capture at the given rate; calling again restarts capture.
 * sample_wide requests 16-bit samples; the STM32F411 backend currently
 * captures only the low 8 GPIO bits, regardless of this flag. */
void fx2lafw_capture_start(uint32_t sample_rate_hz, bool sample_wide);

/* Disarms capture. Safe to call even if not running. */
void fx2lafw_capture_stop(void);

/* True if DMA has filled the ring with samples not yet consumed. */
bool fx2lafw_capture_overrun(void);

/* Non-blocking poll. Returns true and fills out_data/out_length if a
 * finished chunk is ready to send over USB. Returns false otherwise.
 * Call consume_chunk only after the whole chunk has been copied.
 * DMA can overwrite the returned data if capture_overrun becomes true. */
bool fx2lafw_capture_poll_chunk(const uint8_t **out_data, size_t *out_length);

/* Releases the chunk returned by the last successful poll. */
void fx2lafw_capture_consume_chunk(void);

#endif /* FX2LAFW_CAPTURE_H */
