#include "unity.h"

#include "app_config.h"
#include "app_comms.h"
#include "ruuvi_boards.h"
#include "ruuvi_endpoints.h"
#include "mock_app_heartbeat.h"
#include "mock_app_led.h"
#include "mock_app_sensor.h"
#include "mock_ruuvi_driver_error.h"
#include "mock_ruuvi_interface_communication_ble_advertising.h"
#include "mock_ruuvi_interface_communication_radio.h"
#include "mock_ruuvi_interface_scheduler.h"
#include "mock_ruuvi_interface_timer.h"
#include "mock_ruuvi_task_advertisement.h"
#include "mock_ruuvi_task_communication.h"
#include "mock_ruuvi_task_gatt.h"
#include "mock_ruuvi_task_nfc.h"
#include <string.h>

extern mode_changes_t m_mode_ops;
extern ri_timer_id_t m_comm_timer;
extern bool m_config_enabled_on_curr_conn;
extern bool m_config_enabled_on_next_conn;

void setUp (void)
{
    rd_error_check_Ignore();
    m_config_enabled_on_curr_conn = false;
    m_config_enabled_on_next_conn = false;
    m_mode_ops.switch_to_normal = false;
}

void tearDown (void)
{
}

static void RD_ERROR_CHECK_EXPECT (rd_status_t err_code, rd_status_t fatal)
{
    rd_error_check_Expect (err_code, fatal, "", 0);
    rd_error_check_IgnoreArg_p_file ();
    rd_error_check_IgnoreArg_line ();
}

static void test_dis_init (ri_comm_dis_init_t * const p_dis, const bool secure)
{
    size_t name_idx = 0;
    memset (p_dis, 0, sizeof (ri_comm_dis_init_t));
    rt_com_get_mac_str_ExpectAnyArgsAndReturn (RD_SUCCESS);
    rt_com_get_mac_str_ReturnArrayThruPtr_mac_str ("AA:BB:CC:DD:EE:FF", 18);
    snprintf (p_dis->deviceaddr, sizeof (p_dis->deviceaddr), "AA:BB:CC:DD:EE:FF");

    if (!secure)
    {
        rt_com_get_id_str_ExpectAnyArgsAndReturn (RD_SUCCESS);
        rt_com_get_id_str_ReturnArrayThruPtr_id_str ("00:11:22:33:44:55:66:77", 24);
        snprintf (p_dis->deviceid, sizeof (p_dis->deviceid), "00:11:22:33:44:55:66:77");
    }

    name_idx = snprintf (p_dis->fw_version, sizeof (p_dis->fw_version), APP_FW_NAME);
    snprintf (p_dis->fw_version + name_idx,
              sizeof (p_dis->fw_version) - name_idx,
              APP_FW_VERSION);
    snprintf (p_dis->hw_version, sizeof (p_dis->hw_version), "Check PCB");
    snprintf (p_dis->manufacturer, sizeof (p_dis->manufacturer), RB_MANUFACTURER_STRING);
    snprintf (p_dis->model, sizeof (p_dis->model), RB_MODEL_STRING);
}

/**
 * @brief Initialize communication methods supported by board
 *
 * After calling this function, NFC is ready to provide device information,
 * advertisement data can be sent and advertisements are connectable for enabling GATT
 * connection.
 *
 * Use ruuvi task functions, such as rt_gatt_send_asynchronous to send data out.
 *
 * @retval RD_SUCCESS on success.
 * @return error code from stack on error.
 */
static rt_adv_init_t adv_settings =
{
    .channels = {
        .channel_37 = 1,
        .channel_38 = 1,
        .channel_39 = 1
    },
    .adv_interval_ms = 100U,
    .adv_pwr_dbm = RB_TX_POWER_MAX,
    .manufacturer_id = RB_BLE_MANUFACTURER_ID
};

static void adv_init_Expect (void)
{
#if APP_ADV_ENABLED
    rt_adv_init_ExpectAndReturn (&adv_settings, RD_SUCCESS);
    ri_adv_type_set_ExpectAndReturn (NONCONNECTABLE_NONSCANNABLE, RD_SUCCESS);
    ri_timer_stop_ExpectAndReturn (m_comm_timer, RD_SUCCESS);
    ri_timer_start_ExpectAndReturn (m_comm_timer, APP_FAST_ADV_TIME_MS, &m_mode_ops,
                                    RD_SUCCESS);
#endif
}

static void gatt_init_Expect (ri_comm_dis_init_t * p_dis, const bool secure)
{
#if APP_GATT_ENABLED
    static uint64_t address = 0xAABBCCDDEEFF01A0;
    ri_radio_address_get_ExpectAnyArgsAndReturn (RD_SUCCESS);
    ri_radio_address_get_ReturnThruPtr_address (&address);
    rt_gatt_init_ExpectAndReturn ("Ruuvi 01A0", RD_SUCCESS);

    if (!secure)
    {
        rt_gatt_dfu_init_ExpectAndReturn (RD_SUCCESS);
    }

    rt_gatt_dis_init_ExpectWithArrayAndReturn (p_dis, 1, RD_SUCCESS);
    rt_gatt_nus_init_ExpectAndReturn (RD_SUCCESS);
    rt_gatt_set_on_connected_isr_Expect (&on_gatt_connected_isr);
    rt_gatt_set_on_disconn_isr_Expect (&on_gatt_disconnected_isr);
    rt_gatt_set_on_received_isr_Expect (&on_gatt_data_isr);
    rt_gatt_enable_ExpectAndReturn (RD_SUCCESS);
#endif
}

static void nfc_init_Expect (ri_comm_dis_init_t * p_dis)
{
#if APP_NFC_ENABLED
    rt_nfc_init_ExpectWithArrayAndReturn (p_dis, 1, RD_SUCCESS);
    rt_nfc_set_on_connected_isr_Expect (&on_nfc_connected_isr);
    rt_nfc_set_on_disconn_isr_Expect (&on_nfc_disconnected_isr);
#endif
}

static void app_comms_ble_uninit_Expect (void)
{
    rt_adv_uninit_ExpectAndReturn (RD_SUCCESS);
    rt_gatt_uninit_ExpectAndReturn (RD_SUCCESS);
}

static void app_comms_ble_init_Expect (const bool secure)
{
    static ri_comm_dis_init_t ble_dis = {0};
    memset (&ble_dis, 0, sizeof (ble_dis));
    test_dis_init (&ble_dis, secure);
    adv_init_Expect();
    gatt_init_Expect (&ble_dis, secure);
}

void test_app_comms_init_ok (void)
{
    ri_comm_dis_init_t ble_dis = {0};
    ri_comm_dis_init_t nfc_dis = {0};
    // Allow switchover to extended / 2 MBPS comms.
    ri_radio_init_ExpectAndReturn (APP_MODULATION, RD_SUCCESS);
    ri_timer_create_ExpectAndReturn (&m_comm_timer, RI_TIMER_MODE_SINGLE_SHOT,
                                     &comm_mode_change_isr, RD_SUCCESS);
    test_dis_init (&nfc_dis, false);
    nfc_init_Expect (&nfc_dis);
    adv_init_Expect();
    test_dis_init (&ble_dis, true);
    gatt_init_Expect (&ble_dis, true);
    rd_status_t err_code = app_comms_init (true);
    TEST_ASSERT (RD_SUCCESS == err_code);
}

static void app_comms_configure_next_enable_Expect (void)
{
    app_comms_ble_uninit_Expect();
    app_comms_ble_init_Expect (false);
    ri_timer_stop_ExpectAndReturn (m_comm_timer, RD_SUCCESS);
    ri_timer_start_ExpectAndReturn (m_comm_timer, APP_CONFIG_ENABLED_TIME_MS, &m_mode_ops,
                                    RD_SUCCESS);
    app_led_activity_set_ExpectAndReturn (RB_LED_CONFIG_ENABLED, RD_SUCCESS);
}

void test_app_comms_configure_next_enable_ok (void)
{
    app_comms_configure_next_enable_Expect();
    app_comms_configure_next_enable();
    TEST_ASSERT (1 == m_mode_ops.disable_config);
    TEST_ASSERT (true == m_config_enabled_on_next_conn);
}

void test_app_comms_init_timer_fail (void)
{
    ri_radio_init_ExpectAndReturn (APP_MODULATION, RD_SUCCESS);
    ri_timer_create_ExpectAndReturn (&m_comm_timer, RI_TIMER_MODE_SINGLE_SHOT,
                                     &comm_mode_change_isr, RD_ERROR_RESOURCES);
    rd_status_t err_code = app_comms_init (true);
    TEST_ASSERT (RD_ERROR_RESOURCES == err_code);
}

void test_handle_gatt_connected (void)
{
    rt_gatt_disable_ExpectAndReturn (RD_SUCCESS);
    handle_gatt_connected (NULL, 0);
    TEST_ASSERT (!m_config_enabled_on_next_conn);
    TEST_ASSERT (!m_mode_ops.switch_to_normal);
}

void test_on_gatt_connected_isr (void)
{
    ri_scheduler_event_put_ExpectAndReturn (NULL, 0, &handle_gatt_connected, RD_SUCCESS);
    on_gatt_connected_isr (NULL, 0);
}

void test_handle_gatt_disconnected (void)
{
    app_led_activity_set_ExpectAndReturn (RB_LED_ACTIVITY, RD_SUCCESS);
    app_comms_ble_uninit_Expect();
    app_comms_ble_init_Expect (true);
    handle_gatt_disconnected (NULL, 0);
    TEST_ASSERT (!m_config_enabled_on_curr_conn);
}

void test_on_gatt_disconnected_isr (void)
{
    ri_scheduler_event_put_ExpectAndReturn (NULL, 0, &handle_gatt_disconnected, RD_SUCCESS);
    on_gatt_disconnected_isr (NULL, 0);
}

void test_on_gatt_received_isr (void)
{
    uint8_t data[RI_SCHEDULER_SIZE];
    ri_scheduler_event_put_ExpectAndReturn (data, sizeof (data), &handle_gatt_data,
                                            RD_SUCCESS);
    RD_ERROR_CHECK_EXPECT (RD_SUCCESS, RD_SUCCESS);
    on_gatt_data_isr (data, sizeof (data));
}

void test_on_gatt_received_toobig (void)
{
    uint8_t toobig[RI_SCHEDULER_SIZE + 1];
    ri_scheduler_event_put_ExpectAndReturn (toobig, sizeof (toobig), &handle_gatt_data,
                                            RD_ERROR_INVALID_LENGTH);
    RD_ERROR_CHECK_EXPECT (RD_ERROR_INVALID_LENGTH, RD_SUCCESS);
    on_gatt_data_isr (toobig, sizeof (toobig));
}

void test_handle_gatt_sensor_op_acc (void)
{
    uint8_t mock_data[RE_STANDARD_MESSAGE_LENGTH] = {0};
    mock_data[RE_STANDARD_DESTINATION_INDEX] = RE_STANDARD_DESTINATION_ACCELERATION;
    app_heartbeat_stop_ExpectAndReturn (RD_SUCCESS);
    app_sensor_handle_ExpectAndReturn (&rt_gatt_send_asynchronous, mock_data,
                                       sizeof (mock_data), RD_SUCCESS);
    app_heartbeat_start_ExpectAndReturn (RD_SUCCESS);
    RD_ERROR_CHECK_EXPECT (RD_SUCCESS, ~RD_ERROR_FATAL);
    handle_gatt_data (mock_data, sizeof (mock_data));
}

void test_handle_gatt_null_data (void)
{
    RD_ERROR_CHECK_EXPECT (RD_ERROR_NULL, ~RD_ERROR_FATAL);
    handle_gatt_data (NULL, 0);
}

void test_handle_gatt_short_data (void)
{
    uint8_t mock_data[RE_STANDARD_HEADER_LENGTH] = {0};
    mock_data[RE_STANDARD_DESTINATION_INDEX] = RE_STANDARD_DESTINATION_ACCELERATION;
    RD_ERROR_CHECK_EXPECT (RD_ERROR_INVALID_PARAM, ~RD_ERROR_FATAL);
    handle_gatt_data (mock_data, sizeof (mock_data));
}

void test_bleadv_repeat_count_set_get (void)
{
    const uint8_t count = 5;
    app_comms_bleadv_send_count_set (count);
    uint8_t check = app_comms_bleadv_send_count_get();
    TEST_ASSERT (count == check);
}

void test_comm_mode_change_isr_switch_to_normal (void)
{
    mode_changes_t mode = {0};
    mode.switch_to_normal = 1;
    app_comms_bleadv_send_count_set (5);
    comm_mode_change_isr (&mode);
    uint8_t adv_repeat = app_comms_bleadv_send_count_get();
    TEST_ASSERT (0 == mode.switch_to_normal);
    TEST_ASSERT (1 == adv_repeat);
}

void test_comm_mode_change_isr_disable_config (void)
{
    mode_changes_t mode = {0};
    mode.disable_config = 1;
    app_led_activity_set_ExpectAndReturn (RB_LED_ACTIVITY, RD_SUCCESS);
    comm_mode_change_isr (&mode);
    TEST_ASSERT (0 == mode.disable_config);
}

void test_handle_nfc_connected (void)
{
}

void test_handle_nfc_disconnected (void)
{
    app_comms_configure_next_enable_Expect();
    handle_nfc_disconnected (NULL, 0);
}

void test_app_comm_configurable_gatt_after_nfc (void)
{
    ri_scheduler_event_put_ExpectAndReturn (NULL, 0, &handle_nfc_connected, RD_SUCCESS);
    on_nfc_connected_isr (NULL, 0);
    test_handle_nfc_connected ();
    ri_scheduler_event_put_ExpectAndReturn (NULL, 0, &handle_nfc_disconnected, RD_SUCCESS);
    on_nfc_disconnected_isr (NULL, 0);
    test_handle_nfc_disconnected ();
    TEST_ASSERT (m_mode_ops.switch_to_normal);
    ri_scheduler_event_put_ExpectAndReturn (NULL, 0, &handle_gatt_connected, RD_SUCCESS);
    on_gatt_connected_isr (NULL, 0);
    test_handle_gatt_connected ();
    TEST_ASSERT (m_config_enabled_on_curr_conn);
    TEST_ASSERT (!m_mode_ops.switch_to_normal);
}