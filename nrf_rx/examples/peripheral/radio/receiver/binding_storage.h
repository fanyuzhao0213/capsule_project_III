#ifndef RECEIVER_BINDING_STORAGE_H
#define RECEIVER_BINDING_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

void receiver_binding_init(void);
bool receiver_binding_is_bound(void);
const uint8_t *receiver_binding_get(void);
bool receiver_binding_set(const uint8_t sn[8]);
bool receiver_binding_clear(void);
bool receiver_binding_matches(const uint8_t sn[8]);

#endif
