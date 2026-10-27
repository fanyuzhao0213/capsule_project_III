#include "binding_storage.h"
#include <string.h>

typedef struct
{
    uint8_t sn[8];                                     /** 当前会话绑定的8字节胶囊SN。 */
    bool bound;                                        /** 当前会话是否已经绑定胶囊。 */
} receiver_binding_state_t;

static receiver_binding_state_t m_binding;

/** @brief 初始化RX会话绑定状态；每次上电都清空SN并恢复未绑定。 */
void receiver_binding_init(void)
{
    memset(&m_binding, 0, sizeof(m_binding));
}

/** @brief 查询本次上电会话是否已经绑定胶囊。 */
bool receiver_binding_is_bound(void) { return m_binding.bound; }

/** @brief 获取当前RAM绑定SN；仅在receiver_binding_is_bound为true时有效。 */
const uint8_t *receiver_binding_get(void) { return m_binding.sn; }

/** @brief 处理0x24命令：把8字节SN保存到RAM并立即生效。 */
bool receiver_binding_set(const uint8_t sn[8])
{
    memcpy(m_binding.sn, sn, sizeof(m_binding.sn));
    m_binding.bound = true;
    return true;
}

/** @brief 处理0x22命令：清除RAM绑定，恢复只发现设备、不接收图片。 */
bool receiver_binding_clear(void)
{
    memset(&m_binding, 0, sizeof(m_binding));
    return true;
}

/** @brief 判断无线包携带的SN是否等于当前绑定SN，供广播和图片过滤调用。 */
bool receiver_binding_matches(const uint8_t sn[8])
{
    return m_binding.bound &&
           (memcmp(m_binding.sn, sn, sizeof(m_binding.sn)) == 0);
}
