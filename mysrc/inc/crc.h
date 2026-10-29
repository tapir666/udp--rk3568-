#ifndef CRC_H
#define CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t crc32_compute(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
