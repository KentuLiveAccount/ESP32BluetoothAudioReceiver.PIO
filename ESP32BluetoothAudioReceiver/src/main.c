// ...existing code...
#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "esp_bluedroid_api.h"

static const char *TAG = "a2dp_sink";

/* Helpers: convert event enums to human-readable strings for logging */
static const char *a2dp_event_to_str(esp_a2d_cb_event_t ev)
{
    switch (ev) {
    case ESP_A2D_CONNECTION_STATE_EVT: return "ESP_A2D_CONNECTION_STATE_EVT";
    case ESP_A2D_AUDIO_STATE_EVT: return "ESP_A2D_AUDIO_STATE_EVT";
    case ESP_A2D_AUDIO_CFG_EVT: return "ESP_A2D_AUDIO_CFG_EVT";
    // case ESP_A2D_MEDIA_CTRL_EVT: return "ESP_A2D_MEDIA_CTRL_EVT";
    default: return "ESP_A2D_UNKNOWN_EVT";
    }
}

static const char *avrc_ct_event_to_str(esp_avrc_ct_cb_event_t ev)
{
    switch (ev) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: return "ESP_AVRC_CT_CONNECTION_STATE_EVT";
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: return "ESP_AVRC_CT_PASSTHROUGH_RSP_EVT";
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: return "ESP_AVRC_CT_REMOTE_FEATURES_EVT";
    case ESP_AVRC_CT_METADATA_RSP_EVT: return "ESP_AVRC_CT_METADATA_RSP_EVT";
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: return "ESP_AVRC_CT_CHANGE_NOTIFY_EVT";
    default: return "ESP_AVRC_CT_UNKNOWN_EVT";
    }
}

static const char *gap_event_to_str(esp_bt_gap_cb_event_t ev)
{
    switch (ev) {
    case ESP_BT_GAP_DISC_RES_EVT: return "ESP_BT_GAP_DISC_RES_EVT";
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: return "ESP_BT_GAP_DISC_STATE_CHANGED_EVT";
    case ESP_BT_GAP_RMT_SRVC_REC_EVT: return "ESP_BT_GAP_RMT_SRVC_REC_EVT";
    case ESP_BT_GAP_AUTH_CMPL_EVT: return "ESP_BT_GAP_AUTH_CMPL_EVT";
    // case ESP_BT_GAP_REMOVE_BOND_DEV_EVT: return "ESP_BT_GAP_REMOVE_BOND_DEV_EVT";
    case ESP_BT_GAP_PIN_REQ_EVT: return "ESP_BT_GAP_PIN_REQ_EVT";
    default: return "ESP_BT_GAP_UNKNOWN_EVT";
    }
}

/* A2DP control callback (events) */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    ESP_LOGI(TAG, "A2DP callback: %s (%d)", a2dp_event_to_str(event), event);
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "  conn state: val=%d", param->conn_stat.state);
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "  audio state: val=%d", param->audio_stat.state);
        break;
    case ESP_A2D_AUDIO_CFG_EVT:
        ESP_LOGI(TAG, "  audio cfg: codec type=%d, len=%d", param->audio_cfg.mcc.type, 0 /*param->audio_cfg.mcc.len*/);
        break;
    default:
        break;
    }
}

/* A2DP data callback — receives decoded PCM (or SBC frames depending on config).
   For this minimal example we just log the length. Replace with I2S output as needed. */
static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    ESP_LOGI(TAG, "A2DP data len=%u", len);
}

/* Minimal AVRCP controller callback to satisfy A2DP dependencies and log events */
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    ESP_LOGI(TAG, "AVRCP CT callback: %s (%d)", avrc_ct_event_to_str(event), event);
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "  conn state: connected=%d, handle=%d", param->conn_stat.connected, 0 /*param->conn_stat.handle*/);
        break;
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
        ESP_LOGI(TAG, "  passthrough rsp: key=%d, state=%d", param->psth_rsp.key_code, param->psth_rsp.key_state);
        break;
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        ESP_LOGI(TAG, "  remote features: 0x%04x", 0 /*param->rmt_feats.feat*/);
        break;
    default:
        ESP_LOGI(TAG, "  default case");
        break;
    }
}

/* Simple GAP callback to log discovery and auth events for debugging */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    ESP_LOGI(TAG, "GAP callback: %s (%d)", gap_event_to_str(event), event);
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        ESP_LOGI(TAG, "  discovery result: num_prop=%d", param->disc_res.num_prop);
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        ESP_LOGI(TAG, "  discovery state changed: %d", param->disc_st_chg.state);
        break;
    case ESP_BT_GAP_RMT_SRVC_REC_EVT:
        ESP_LOGI(TAG, "  remote service record event");
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "  auth complete: status=%d, device=%02x:%02x:%02x:%02x:%02x:%02x",
                 param->auth_cmpl.stat,
                 0, 0);
                //  param->auth_cmpl.bd_addr[0], param->auth_cmpl.bd_addr[1], param->auth_cmpl.bd_addr[2],
                //  param->auth_cmpl.bd_addr[3], param->auth_cmpl.bd_addr[4], param->auth_cmpl.bd_addr[5]);
        break;
    default:
        ESP_LOGI(TAG, "  default case");
        break;
    }
}

void app_main(void)
{
    esp_err_t ret;

    // NVS init required by BT stack
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Increase Bluetooth-related log verbosity for debugging visibility */
    ESP_LOGI(TAG, "Setting verbose log levels for Bluetooth components");
    esp_log_level_set("BT_BTC", ESP_LOG_DEBUG);
    esp_log_level_set("BT_AV", ESP_LOG_DEBUG);
    esp_log_level_set("BT_BLUEDROID", ESP_LOG_DEBUG);
    esp_log_level_set("BT_GAP", ESP_LOG_DEBUG);
    esp_log_level_set("A2DP", ESP_LOG_DEBUG);
    esp_log_level_set("AVRCP", ESP_LOG_DEBUG);
    esp_log_level_set("BT", ESP_LOG_DEBUG);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    // Initialize and enable the BT controller (Classic BT)
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "bt controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Choose controller mode according to sdkconfig to avoid invalid-arg errors */
    esp_bt_mode_t bt_mode = ESP_BT_MODE_CLASSIC_BT;
#if CONFIG_BTDM_CTRL_MODE_BLE_ONLY
    bt_mode = ESP_BT_MODE_BLE;
#elif CONFIG_BTDM_CTRL_MODE_BTDM
    bt_mode = ESP_BT_MODE_BTDM;
#elif CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY
    bt_mode = ESP_BT_MODE_CLASSIC_BT;
#endif

    ret = esp_bt_controller_enable(bt_mode);
    if (ret) {
        ESP_LOGE(TAG, "bt controller enable failed: %s (mode=%d)", esp_err_to_name(ret), bt_mode);
        return;
    }

    // Initialize Bluedroid stack
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Set device name shown to remote phones
    esp_bt_gap_set_device_name("ESP_A2DP_SINK");

    // Make the device discoverable/connectable
    esp_bt_gap_set_scan_mode(ESP_BT_GENERAL_DISCOVERABLE, ESP_BT_CONNECTABLE);

    /* Register GAP callback so we can see discovery/pairing activity in the logs */
    esp_bt_gap_register_callback(&bt_app_gap_cb);

    // Register A2DP callbacks and initialize sink
    esp_a2d_register_callback(&bt_app_a2d_cb);
    esp_a2d_sink_register_data_callback(&bt_app_a2d_data_cb);
    /* Register and enable AVRCP controller so A2DP has AVRC available */
    esp_avrc_ct_register_callback(&bt_app_rc_ct_cb);
    esp_avrc_ct_init();
    esp_a2d_sink_init();

     /* Re-set discoverable/connectable after A2DP/AVRCP are initialized so SDP
         records (A2DP/AVRCP) are available when a phone scans. Some phones only
         list devices that advertise audio services. */
     esp_bt_gap_set_device_name("ESP_A2DP_SINK");
     esp_bt_gap_set_scan_mode(ESP_BT_GENERAL_DISCOVERABLE, ESP_BT_CONNECTABLE);

     /* Set Class of Device (CoD) to Audio/Sink so phones more readily list this
         device as an audio sink. The numeric CoD below encodes Major Service and
         Major/Minor device classes for AV devices. If your IDF version exposes
         symbolic macros for CoD they can be used instead. */
     uint32_t cod = 0x240404; /* Major service + Major/Minor class for Audio */
     esp_bt_gap_set_cod(*(esp_bt_cod_t*)(&cod), ESP_BT_SET_COD_MAJOR_MINOR);

    ESP_LOGI(TAG, "A2DP sink initialized, waiting for connection");

    /* Keep app_main alive for debugging; BT stack runs in its own tasks but
       keeping this task alive makes logs and state easier to inspect. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
// ...existing code...