/* Host-side smoke test for the RX manual antenna-selection state. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "antenna_manager.h"

static bool test_bound;
static uint8_t test_antenna;
static unsigned test_switches;
static unsigned test_image_resets;
static unsigned test_queue_clears;

bool receiver_binding_is_bound(void) { return test_bound; }
bool receiver_binding_matches(const uint8_t sn[8]) { (void)sn; return test_bound; }
const uint8_t *receiver_binding_get(void)
{
    static const uint8_t sn[8] = {0u};
    return sn;
}
uint32_t receiver_timebase_now_ms(void) { return 0u; }
bool receiver_radio_send(const uint8_t *packet, uint16_t length,
                         uint8_t repeat_count)
{
    (void)packet; (void)length; (void)repeat_count;
    return true;
}
void receiver_radio_select_antenna(uint8_t antenna)
{
    test_antenna = antenna;
    ++test_switches;
}
void receiver_radio_clear_queue(void) { ++test_queue_clears; }
uint8_t rf1662_get_antenna(void) { return test_antenna; }
void receiver_image_reset(void) { ++test_image_resets; }

int main(void)
{
    uint8_t packet[1] = {0u};
    uint8_t rssi[RF1662_ANTENNA_COUNT] = {0u};
    unsigned i;
    unsigned switches;

    receiver_antenna_init();
    assert(test_antenna == 0u);
    switches = test_switches;
    assert(!receiver_antenna_manual_command(0x01u));
    assert(test_switches == switches);     /* 未收到00时不能直接切ANT1。 */
    assert(receiver_antenna_manual_command(0x00u));
    assert(test_switches == switches);     /* 开始不强制切换当前天线。 */
    assert(receiver_antenna_manual_command(0x0Cu));
    assert(test_antenna == 11u);           /* PC的0C对应软件索引11。 */
    assert(!receiver_antenna_on_radio_packet(packet, false, 0u));
    packet[0] = 0x05u;
    assert(!receiver_antenna_on_radio_packet(packet, true, 45u));
    receiver_antenna_copy_latest_rssi(rssi);
    assert(rssi[11] == 45u);
    for (i = 0u; i < 11u; ++i)
    {
        assert(rssi[i] == 0u);
    }
    assert(receiver_antenna_manual_command(0x01u));
    receiver_antenna_copy_latest_rssi(rssi);
    for (i = 0u; i < RF1662_ANTENNA_COUNT; ++i)
    {
        assert(rssi[i] == 0u);           /* 换路后不能带入ANT12旧值。 */
    }
    assert(!receiver_antenna_on_radio_packet(packet, true, 52u));
    receiver_antenna_copy_latest_rssi(rssi);
    assert(rssi[0] == 52u);
    for (i = 1u; i < RF1662_ANTENNA_COUNT; ++i)
    {
        assert(rssi[i] == 0u);
    }
    receiver_antenna_tick_1ms(100u);
    assert(!receiver_antenna_service_schedule());
    assert(test_antenna == 0u);            /* 自动扫描不能抢占手动选路。 */
    assert(!receiver_antenna_manual_command(0x0Eu));
    assert(test_antenna == 0u);
    assert(receiver_antenna_manual_command(0x0Du));
    assert(test_antenna == 0u);            /* 未绑定时恢复DISCOVERY。 */
    switches = test_switches;
    assert(!receiver_antenna_manual_command(0x03u));
    assert(test_switches == switches);     /* 停止后必须重新发送00。 */

    test_bound = true;
    assert(receiver_antenna_manual_command(0x00u));
    assert(receiver_antenna_manual_command(0x03u));
    assert(test_antenna == 2u);            /* 显式开始后才能选路。 */
    assert(receiver_antenna_manual_command(0x0Du));
    assert(test_antenna == 0u);            /* 已绑定时恢复SEEK_END。 */
    assert(test_image_resets >= 4u);
    assert(test_queue_clears >= 4u);
    return 0;
}
