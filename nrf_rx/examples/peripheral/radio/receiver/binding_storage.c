#include "binding_storage.h"
#include <string.h>

static uint8_t m_bound_sn[8];
static bool m_is_bound;

/** @brief 初始化RX会话绑定状态；每次上电都清空SN并恢复未绑定。 */
void receiver_binding_init(void)
{
    memset(m_bound_sn, 0, sizeof(m_bound_sn));
    m_is_bound = false;
}

/** @brief 查询本次上电会话是否已经绑定胶囊。 */
bool receiver_binding_is_bound(void) { return m_is_bound; }

/** @brief 获取当前RAM绑定SN；仅在receiver_binding_is_bound为true时有效。 */
const uint8_t *receiver_binding_get(void) { return m_bound_sn; }

/** @brief 处理0x24命令：把8字节SN保存到RAM并立即生效。 */
bool receiver_binding_set(const uint8_t sn[8])
{
    memcpy(m_bound_sn, sn, sizeof(m_bound_sn)); // ① 保存PC指定的8字节SN
    m_is_bound = true;                          // ② 开启图片SN过滤
    return true;
}

/** @brief 处理0x22命令：清除RAM绑定，恢复只发现设备、不接收图片。 */
bool receiver_binding_clear(void)
{
    memset(m_bound_sn, 0, sizeof(m_bound_sn)); // ① 清除旧SN，避免误匹配
    m_is_bound = false;                        // ② 标记当前会话未绑定
    return true;
}

/** @brief 判断无线包携带的SN是否等于当前绑定SN，供广播和图片过滤调用。 */
bool receiver_binding_matches(const uint8_t sn[8])
{
    return m_is_bound && (memcmp(m_bound_sn, sn, sizeof(m_bound_sn)) == 0);
}
