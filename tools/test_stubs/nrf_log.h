/* Host test stub: suppress firmware logging in state-machine tests. */
#ifndef RX_TEST_STUB_NRF_LOG_H
#define RX_TEST_STUB_NRF_LOG_H
#define NRF_LOG_INFO(...) ((void)0)
#define NRF_LOG_WARNING(...) ((void)0)
#endif
