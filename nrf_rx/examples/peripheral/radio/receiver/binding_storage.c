#include "binding_storage.h"
#include <string.h>

static uint8_t m_bound_sn[8];
static bool m_is_bound;

void receiver_binding_init(void)
{
    memset(m_bound_sn, 0, sizeof(m_bound_sn));
    m_is_bound = false;
}

bool receiver_binding_is_bound(void) { return m_is_bound; }
const uint8_t *receiver_binding_get(void) { return m_bound_sn; }

bool receiver_binding_set(const uint8_t sn[8])
{
    memcpy(m_bound_sn, sn, sizeof(m_bound_sn));
    m_is_bound = true;
    return true;
}

bool receiver_binding_clear(void)
{
    memset(m_bound_sn, 0, sizeof(m_bound_sn));
    m_is_bound = false;
    return true;
}

bool receiver_binding_matches(const uint8_t sn[8])
{
    return m_is_bound && (memcmp(m_bound_sn, sn, sizeof(m_bound_sn)) == 0);
}
