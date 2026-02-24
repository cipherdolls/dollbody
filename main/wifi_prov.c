#include "wifi_prov.h"
#include "display.h"
#include "wifi_mgr.h"
#include "config.h"
#include "config_store.h"
#include "events.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_prov";

// Single persistent provisioning screen — avoids screen-switch white flash
static lv_obj_t *s_prov_scr = NULL;

// Keyboard result
static volatile bool s_kb_done = false;
static char s_kb_result[128];

// Network list result
static volatile bool s_net_done = false;
static char s_net_ssid[64];

// ─── LVGL event callbacks (run inside lv_timer_handler, mutex held) ──────────
static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);

    if (code == LV_EVENT_READY) {
        strlcpy(s_kb_result, lv_textarea_get_text(ta), sizeof(s_kb_result));
        s_kb_done = true;
    } else if (code == LV_EVENT_CANCEL) {
        s_kb_result[0] = '\0';
        s_kb_done = true;
    }
}

static void net_btn_cb(lv_event_t *e)
{
    char *ssid = (char *)lv_event_get_user_data(e);
    if (ssid) {
        strlcpy(s_net_ssid, ssid, sizeof(s_net_ssid));
        s_net_done = true;
    }
}

static void rescan_btn_cb(lv_event_t *e)
{
    s_net_ssid[0] = '\0';
    s_net_done = true;  // empty = trigger rescan
}

// ─── Reset the provisioning screen (inside lock) ─────────────────────────────
static void prov_reset(lv_color_t bg)
{
    if (!s_prov_scr) {
        s_prov_scr = lv_obj_create(NULL);
    }
    lv_obj_clean(s_prov_scr);
    lv_obj_set_style_bg_color(s_prov_scr, bg, 0);
    lv_obj_set_style_bg_opa(s_prov_scr, LV_OPA_COVER, 0);
    lv_scr_load(s_prov_scr);
}

static lv_obj_t *prov_label(const char *txt, lv_coord_t y_ofs)
{
    lv_obj_t *l = lv_label_create(s_prov_scr);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LCD_H_RES - 60);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y_ofs);
    return l;
}

// ─── Show status message ──────────────────────────────────────────────────────
static void prov_status(const char *msg, lv_color_t bg)
{
    if (!display_lvgl_lock(-1)) return;
    prov_reset(bg);
    prov_label(msg, 0);
    display_lvgl_unlock();
}

// ─── Show keyboard and block until user submits ───────────────────────────────
static bool prov_keyboard(const char *title, const char *placeholder,
                           bool password_mode, char *out, size_t out_sz)
{
    s_kb_done    = false;
    s_kb_result[0] = '\0';

    if (!display_lvgl_lock(-1)) return false;

    prov_reset(lv_color_make(0x10, 0x10, 0x20));

    lv_obj_t *title_lbl = lv_label_create(s_prov_scr);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title_lbl, LCD_H_RES - 20);
    lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *ta = lv_textarea_create(s_prov_scr);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_password_mode(ta, password_mode);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, LCD_H_RES - 60);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *kb = lv_keyboard_create(s_prov_scr);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY,  ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, ta);

    display_lvgl_unlock();

    while (!s_kb_done) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    bool ok = (s_kb_result[0] != '\0');
    if (out && out_sz > 0) {
        strlcpy(out, s_kb_result, out_sz);
    }
    return ok;
}

// ─── Signal strength indicator ────────────────────────────────────────────────
static const char *rssi_bar(int8_t rssi)
{
    if (rssi >= -60) return "||||";
    if (rssi >= -70) return "|||";
    if (rssi >= -80) return "||";
    return "|";
}

// ─── Show network list, block until selection. Returns false = rescan. ────────
#define MAX_NETS 20
static char s_ssid_store[MAX_NETS][33];

static bool prov_network_list(const wifi_ap_info_t *aps, int count,
                              char *out_ssid, size_t out_sz)
{
    s_net_done    = false;
    s_net_ssid[0] = '\0';

    if (!display_lvgl_lock(-1)) return false;

    prov_reset(lv_color_make(0x08, 0x08, 0x20));

    // Title
    lv_obj_t *title = lv_label_create(s_prov_scr);
    lv_label_set_text(title, "Select Network");
    lv_obj_set_style_text_color(title, lv_color_make(0xCC, 0xDD, 0xFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Scrollable list — centred, leaving room for title
    lv_obj_t *list = lv_list_create(s_prov_scr);
    lv_obj_set_size(list, LCD_H_RES - 50, LCD_V_RES - 70);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 14);
    lv_obj_set_style_bg_color(list, lv_color_make(0x10, 0x10, 0x28), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_row(list, 2, 0);

    int shown = (count < MAX_NETS) ? count : MAX_NETS;
    for (int i = 0; i < shown; i++) {
        strlcpy(s_ssid_store[i], aps[i].ssid, 33);

        // Label: SSID (truncated) + signal bars + lock indicator
        char label[52];
        const char *lock = (aps[i].authmode != WIFI_AUTH_OPEN) ? "*" : " ";
        snprintf(label, sizeof(label), "%-20.20s %s%s",
                 aps[i].ssid, rssi_bar(aps[i].rssi), lock);

        lv_obj_t *btn = lv_list_add_btn(list, NULL, label);
        lv_obj_set_style_text_color(btn, lv_color_make(0xFF, 0xFF, 0xFF), 0);
        lv_obj_set_style_bg_color(btn, lv_color_make(0x18, 0x18, 0x35), 0);
        lv_obj_set_style_bg_color(btn, lv_color_make(0x30, 0x50, 0x90), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(btn, net_btn_cb, LV_EVENT_CLICKED, s_ssid_store[i]);
    }

    // Rescan at bottom of list
    lv_obj_t *rscan = lv_list_add_btn(list, NULL, "  [ Rescan ]");
    lv_obj_set_style_text_color(rscan, lv_color_make(0x80, 0xB0, 0xFF), 0);
    lv_obj_set_style_bg_color(rscan, lv_color_make(0x10, 0x10, 0x28), 0);
    lv_obj_set_style_bg_color(rscan, lv_color_make(0x20, 0x30, 0x60), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(rscan, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(rscan, rescan_btn_cb, LV_EVENT_CLICKED, NULL);

    display_lvgl_unlock();

    while (!s_net_done) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (s_net_ssid[0] != '\0') {
        strlcpy(out_ssid, s_net_ssid, out_sz);
        return true;
    }
    return false;  // rescan
}

// ─── Main provisioning task ───────────────────────────────────────────────────
void wifi_prov_task_fn(void *pvParameter)
{
    ESP_LOGI(TAG, "WiFi provisioning start");

    // ── Step 1: Scan and select network ──────────────────────────────────────
    char ssid[64] = "";
    while (strlen(ssid) == 0) {
        prov_status("Scanning for\nWiFi networks...", lv_color_make(0x10, 0x10, 0x40));

        wifi_ap_info_t *aps = NULL;
        int count = wifi_mgr_scan(&aps);

        if (count == 0) {
            if (aps) free(aps);
            prov_status("No networks found\nRetrying...", lv_color_make(0x30, 0x10, 0x10));
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        bool selected = prov_network_list(aps, count, ssid, sizeof(ssid));
        free(aps);

        if (!selected) ssid[0] = '\0';  // Rescan
    }
    strlcpy(g_config.ssid, ssid, sizeof(g_config.ssid));

    // ── Step 2: Password ──────────────────────────────────────────────────────
    char pass[64] = "";
    char pw_title[80];
    snprintf(pw_title, sizeof(pw_title), "Password for:\n%.32s", ssid);
    prov_keyboard(pw_title, "Leave blank if open", true, pass, sizeof(pass));
    strlcpy(g_config.password, pass, sizeof(g_config.password));

    // ── Step 3: Connect ───────────────────────────────────────────────────────
    prov_status("Connecting to WiFi...", lv_color_make(0x10, 0x20, 0x40));

    wifi_mgr_connect(g_config.ssid, g_config.password);

    EventBits_t bits = xEventGroupWaitBits(g_events,
        EVT_WIFI_GOT_IP | EVT_WIFI_DISCONNECTED, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    if (!(bits & EVT_WIFI_GOT_IP)) {
        ESP_LOGW(TAG, "WiFi connect failed");
        prov_status("WiFi Failed\nCheck credentials", lv_color_make(0x40, 0x10, 0x10));
        vTaskDelay(pdMS_TO_TICKS(2000));
        memset(g_config.ssid,     0, sizeof(g_config.ssid));
        memset(g_config.password, 0, sizeof(g_config.password));
        wifi_prov_task_fn(pvParameter);  // retry
        vTaskDelete(NULL);
        return;
    }

    // ── Step 4: API Key ───────────────────────────────────────────────────────
    if (strlen(g_config.apikey) == 0) {
        char apikey[64] = "";
        while (strlen(apikey) == 0) {
            if (!prov_keyboard("API Key", "Paste your API key", false,
                               apikey, sizeof(apikey))) {
                prov_status("API key required", lv_color_make(0x30, 0x20, 0x00));
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        strlcpy(g_config.apikey, apikey, sizeof(g_config.apikey));
    }

    // ── Step 5: Save ─────────────────────────────────────────────────────────
    g_config.provisioned = true;
    config_store_save();

    prov_status("Connected!\nSetup complete.", lv_color_make(0x00, 0x40, 0x10));
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Provisioning complete, ssid='%s'", g_config.ssid);
    xEventGroupSetBits(g_events, EVT_PROV_DONE);
    vTaskDelete(NULL);
}
