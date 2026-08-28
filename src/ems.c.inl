#if EMULATE_LTEMS
#include <stdint.h>
#include "ems.h"

static inline uint8_t ems_read(const uint32_t address) {
    return ems_mem_read8(EMS_START + address);
}

static inline uint16_t ems_readw(const uint32_t address) {
    return ems_mem_read16(EMS_START + address);
}

static inline uint32_t ems_readdw(const uint32_t address) {
    return ems_mem_read32(EMS_START + address);
}

static inline void ems_write(const uint32_t address, const uint8_t data) {
    ems_mem_write8(EMS_START + address, data);
}

static inline void ems_writew(const uint32_t address, const uint16_t data) {
    ems_mem_write16(EMS_START + address, data);
}

static inline void ems_writedw(const uint32_t address, const uint32_t data) {
    ems_mem_write32(EMS_START + address, data);
}
#endif
