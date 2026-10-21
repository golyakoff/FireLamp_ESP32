#include "api.h"
#include <string.h>
#include <lwip/ip_addr.h>
#include <esp_ota_ops.h>
#include "settings.h"
#include "effect.h"
#include "surface.h"
#include "systime.h"
#include "scheduler.h"
#include "demo.h"

static esp_err_t respond_json(httpd_req_t *req, cJSON *resp)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char *txt = cJSON_Print(resp);
    esp_err_t res = httpd_resp_sendstr(req, txt);
    free(txt);

    cJSON_Delete(resp);

    return res;
}

static esp_err_t respond_api(httpd_req_t *req, esp_err_t err, const char *message)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "result", err);
    cJSON_AddStringToObject(resp, "name", esp_err_to_name(err));
    cJSON_AddStringToObject(resp, "message", message ? message : "");

    return respond_json(req, resp);
}

static cJSON *effect_params_json(size_t effect)
{
    cJSON *params = cJSON_CreateArray();
    for (size_t c = 0; c < effects[effect].params_count; c++)
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddItemToArray(params, p);
        cJSON_AddStringToObject(p, "code", effects[effect].params[c].code);
        cJSON_AddStringToObject(p, "name", effects[effect].params[c].name);
        cJSON_AddNumberToObject(p, "min", effects[effect].params[c].min);
        cJSON_AddNumberToObject(p, "max", effects[effect].params[c].max);
        cJSON_AddNumberToObject(p, "default", effects[effect].params[c].def);
        cJSON_AddNumberToObject(p, "value", effects[effect].params[c].value);
        cJSON_AddNumberToObject(p, "type", effects[effect].params[c].type);
    }
    return params;
}

static esp_err_t parse_post_json(httpd_req_t *req, const char **msg, cJSON **json)
{
    *msg = NULL;
    *json = NULL;
    esp_err_t err = ESP_OK;

    char *buf = NULL;

    if (!req->content_len)
    {
        err = ESP_ERR_INVALID_ARG;
        *msg = "No POST data";
        goto exit;
    }

    if (req->content_len >= MAX_POST_SIZE)
    {
        err = ESP_ERR_NO_MEM;
        *msg = "POST data too big";
        goto exit;
    }

    buf = malloc(req->content_len + 1);
    if (!buf)
    {
        err = ESP_ERR_NO_MEM;
        *msg = "Out of memory";
        goto exit;
    }

    int res = httpd_req_recv(req, buf, req->content_len);
    if (res != req->content_len)
    {
        err = ESP_FAIL;
        *msg = res ? "Socket error" : "Connection closed by peer";
        goto exit;
    }
    buf[req->content_len] = 0;

    *json = cJSON_Parse(buf);
    if (!*json)
    {
        err = ESP_ERR_INVALID_ARG;
        *msg = "Invalid JSON";
    }

exit:
    if (buf)
        free(buf);
    return err;
}

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_info(httpd_req_t *req)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "app_name", app_desc->project_name);
    cJSON_AddStringToObject(res, "app_version", app_desc->version);
    cJSON_AddStringToObject(res, "build_date", app_desc->date);
    cJSON_AddStringToObject(res, "idf_ver", app_desc->idf_ver);

    return respond_json(req, res);
}

static const httpd_uri_t route_get_info = {
    .uri = "/api/info",
    .method = HTTP_GET,
    .handler = get_info
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_settings_reset(httpd_req_t *req)
{
    esp_err_t res = sys_settings_reset();
    if (res == ESP_OK)
        res = vol_settings_reset();
    return respond_api(req, res, res == ESP_OK ? "Reboot to apply" : NULL);
}

static const httpd_uri_t route_get_settings_reset = {
    .uri = "/api/settings/reset",
    .method = HTTP_GET,
    .handler = get_settings_reset
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_settings_wifi(httpd_req_t *req)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "mode", sys_settings.wifi.mode);

    cJSON *ip = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "ip", ip);
    cJSON_AddBoolToObject(ip, "dhcp", sys_settings.wifi.ip.dhcp);
    cJSON_AddStringToObject(ip, "ip", sys_settings.wifi.ip.ip);
    cJSON_AddStringToObject(ip, "netmask", sys_settings.wifi.ip.netmask);
    cJSON_AddStringToObject(ip, "gateway", sys_settings.wifi.ip.gateway);
    cJSON_AddStringToObject(ip, "dns", sys_settings.wifi.ip.dns);

    cJSON *ap = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "ap", ap);
    cJSON_AddStringToObject(ap, "ssid", (char *)sys_settings.wifi.ap.ssid);
    cJSON_AddNumberToObject(ap, "channel", sys_settings.wifi.ap.channel);
    cJSON_AddStringToObject(ap, "password", (char *)sys_settings.wifi.ap.password);

    cJSON *sta = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "sta", sta);
    cJSON_AddStringToObject(sta, "ssid", (char *)sys_settings.wifi.sta.ssid);
    cJSON_AddStringToObject(sta, "password", (char *)sys_settings.wifi.sta.password);

    return respond_json(req, res);
}

static const httpd_uri_t route_get_settings_wifi = {
    .uri = "/api/settings/wifi",
    .method = HTTP_GET,
    .handler = get_settings_wifi
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t post_settings_wifi(httpd_req_t *req)
{
    const char *msg = NULL;
    cJSON *json = NULL;

    esp_err_t err = parse_post_json(req, &msg, &json);
    if (err != ESP_OK)
        goto exit;

    cJSON *mode_item = cJSON_GetObjectItem(json, "mode");
    if (!cJSON_IsNumber(mode_item))
    {
        msg = "Item `mode` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    cJSON *ip = cJSON_GetObjectItem(json, "ip");
    if (!ip)
    {
        msg = "Object `ip` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ip_dhcp_item = cJSON_GetObjectItem(ip, "dhcp");
    if (!cJSON_IsBool(ip_dhcp_item))
    {
        msg = "Object `ip.dhcp` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ip_ip_item = cJSON_GetObjectItem(ip, "ip");
    if (!cJSON_IsString(ip_ip_item) || ipaddr_addr(cJSON_GetStringValue(ip_ip_item)) == IPADDR_NONE)
    {
        msg = "Item `ip.ip` not found, invalid or not an IP address";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ip_netmask_item = cJSON_GetObjectItem(ip, "netmask");
    if (!cJSON_IsString(ip_netmask_item) || ipaddr_addr(cJSON_GetStringValue(ip_netmask_item)) == IPADDR_NONE)
    {
        msg = "Item `ip.netmask` not found, invalid or not an IP address";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ip_gateway_item = cJSON_GetObjectItem(ip, "gateway");
    if (!cJSON_IsString(ip_gateway_item) || ipaddr_addr(cJSON_GetStringValue(ip_gateway_item)) == IPADDR_NONE)
    {
        msg = "Item `ip.gateway` not found, invalid or not an IP address";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ip_dns_item = cJSON_GetObjectItem(ip, "dns");
    if (!cJSON_IsString(ip_dns_item) || ipaddr_addr(cJSON_GetStringValue(ip_dns_item)) == IPADDR_NONE)
    {
        msg = "Item `ip.dns` not found, invalid or not an IP address";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    cJSON *ap = cJSON_GetObjectItem(json, "ap");
    if (!ap)
    {
        msg = "Object `ap` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ap_ssid_item = cJSON_GetObjectItem(ap, "ssid");
    if (!cJSON_IsString(ap_ssid_item))
    {
        msg = "Item `ap.ssid` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ap_channel_item = cJSON_GetObjectItem(ap, "channel");
    if (!cJSON_IsNumber(ap_channel_item))
    {
        msg = "Item `ap.channel` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *ap_password_item = cJSON_GetObjectItem(ap, "password");
    if (!cJSON_IsString(ap_password_item))
    {
        msg = "Item `ap.password` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    cJSON *sta = cJSON_GetObjectItem(json, "sta");
    if (!sta)
    {
        msg = "Object `sta` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *sta_ssid_item = cJSON_GetObjectItem(sta, "ssid");
    if (!cJSON_IsString(sta_ssid_item))
    {
        msg = "Item `sta.ssid` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *sta_password_item = cJSON_GetObjectItem(sta, "password");
    if (!cJSON_IsString(sta_password_item))
    {
        msg = "Item `sta.password` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    uint8_t mode = (uint8_t)cJSON_GetNumberValue(mode_item);
    if (mode != WIFI_MODE_AP && mode != WIFI_MODE_STA)
    {
        msg = "Invalid mode value";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    const char *ap_ssid = cJSON_GetStringValue(ap_ssid_item);
    size_t len = strlen(ap_ssid);
    if (!len || len >= sizeof(sys_settings.wifi.ap.ssid))
    {
        msg = "Invalid ap.ssid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    uint8_t ap_channel = (uint8_t)cJSON_GetNumberValue(ap_channel_item);
    if (ap_channel > 32)
    {
        msg = "Invalid ap.channel";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    const char *ap_password = cJSON_GetStringValue(ap_password_item);
    len = strlen(ap_password);
    if (len >= sizeof(sys_settings.wifi.ap.password))
    {
        msg = "ap.password too long";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    const char *sta_ssid = cJSON_GetStringValue(sta_ssid_item);
    len = strlen(sta_ssid);
    if (!len || len >= sizeof(sys_settings.wifi.sta.ssid))
    {
        msg = "Invalid sta.ssid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    const char *sta_password = cJSON_GetStringValue(sta_password_item);
    len = strlen(sta_password);
    if (len >= sizeof(sys_settings.wifi.sta.password))
    {
        msg = "sta.password too long";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    sys_settings.wifi.mode = mode;
    memset(&sys_settings.wifi.ip, 0, sizeof(sys_settings.wifi.ip));
    sys_settings.wifi.ip.dhcp = cJSON_IsTrue(ip_dhcp_item);
    strncpy(sys_settings.wifi.ip.ip, cJSON_GetStringValue(ip_ip_item), sizeof(sys_settings.wifi.ip.ip) - 1);
    strncpy(sys_settings.wifi.ip.netmask, cJSON_GetStringValue(ip_netmask_item), sizeof(sys_settings.wifi.ip.netmask) - 1);
    strncpy(sys_settings.wifi.ip.gateway, cJSON_GetStringValue(ip_gateway_item), sizeof(sys_settings.wifi.ip.gateway) - 1);
    strncpy(sys_settings.wifi.ip.dns, cJSON_GetStringValue(ip_dns_item), sizeof(sys_settings.wifi.ip.dns) - 1);
    sys_settings.wifi.ap.channel = ap_channel;
    memset(sys_settings.wifi.ap.ssid, 0, sizeof(sys_settings.wifi.ap.ssid));
    memset(sys_settings.wifi.ap.password, 0, sizeof(sys_settings.wifi.ap.password));
    memset(sys_settings.wifi.sta.ssid, 0, sizeof(sys_settings.wifi.sta.ssid));
    memset(sys_settings.wifi.sta.password, 0, sizeof(sys_settings.wifi.sta.password));
    strncpy((char *)sys_settings.wifi.ap.ssid, ap_ssid, sizeof(sys_settings.wifi.ap.ssid) - 1);
    strncpy((char *)sys_settings.wifi.ap.password, ap_password, sizeof(sys_settings.wifi.ap.password) - 1);
    strncpy((char *)sys_settings.wifi.sta.ssid, sta_ssid, sizeof(sys_settings.wifi.sta.ssid) - 1);
    strncpy((char *)sys_settings.wifi.sta.password, sta_password, sizeof(sys_settings.wifi.sta.password) - 1);

    err = sys_settings_save();
    msg = err != ESP_OK
            ? "Error saving system settings"
            : "Settings saved, reboot to apply";

exit:
    if (json) cJSON_Delete(json);
    return respond_api(req, err, msg);
}

static const httpd_uri_t route_post_settings_wifi = {
    .uri = "/api/settings/wifi",
    .method = HTTP_POST,
    .handler = post_settings_wifi
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_settings_leds(httpd_req_t *req)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "width", sys_settings.leds.width);
    cJSON_AddNumberToObject(res, "height", sys_settings.leds.height);
    cJSON_AddNumberToObject(res, "type", sys_settings.leds.type);
    cJSON_AddNumberToObject(res, "color_order", sys_settings.leds.color_order);
    cJSON_AddNumberToObject(res, "rotation", sys_settings.leds.rotation);
    cJSON_AddBoolToObject(res, "mirror", sys_settings.leds.mirror);
    cJSON_AddNumberToObject(res, "current_limit", sys_settings.leds.current_limit);

    return respond_json(req, res);
}

static const httpd_uri_t route_get_settings_leds = {
    .uri = "/api/settings/leds",
    .method = HTTP_GET,
    .handler = get_settings_leds
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t post_settings_leds(httpd_req_t *req)
{
    const char *msg = NULL;
    cJSON *json = NULL;

    esp_err_t err = parse_post_json(req, &msg, &json);
    if (err != ESP_OK)
        goto exit;

    cJSON *width_item = cJSON_GetObjectItem(json, "width");
    if (!cJSON_IsNumber(width_item))
    {
        msg = "Field `width` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *height_item = cJSON_GetObjectItem(json, "height");
    if (!cJSON_IsNumber(height_item))
    {
        msg = "Field `height` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    if (!cJSON_IsNumber(type_item))
    {
        msg = "Field `type` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *rotation_item = cJSON_GetObjectItem(json, "rotation");
    if (!cJSON_IsNumber(rotation_item))
    {
        msg = "Field `rotation` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *limit_item = cJSON_GetObjectItem(json, "current_limit");
    if (!cJSON_IsNumber(limit_item))
    {
        msg = "Field `current_limit` not found or invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    // optional fields
    cJSON *color_order_item = cJSON_GetObjectItem(json, "color_order");
    if (color_order_item && !cJSON_IsNumber(color_order_item))
    {
        msg = "Field `color_order` is invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    cJSON *mirror_item = cJSON_GetObjectItem(json, "mirror");
    if (mirror_item && !cJSON_IsBool(mirror_item))
    {
        msg = "Field `mirror` is invalid";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    size_t width = (size_t)cJSON_GetNumberValue(width_item);
    size_t height = (size_t)cJSON_GetNumberValue(height_item);
    if (width < MIN_MATRIX_SIZE || width > MAX_MATRIX_SIZE
        || height < MIN_MATRIX_SIZE || height > MAX_MATRIX_SIZE)
    {
        msg = "Invalid matrix dimensions";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }
    if (height * width > MAX_MATRIX_LEDS)
    {
        msg = "Too much LEDs in matrix";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    uint8_t rotation = (uint8_t)cJSON_GetNumberValue(rotation_item);
    if (rotation > MATRIX_ROTATION_270)
    {
        msg = "Invalid matrix rotation value";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    uint8_t type = (uint8_t)cJSON_GetNumberValue(type_item);
    if (type >= LED_TYPE_MAX)
    {
        msg = "Invalid LED type";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    uint8_t color_order = color_order_item
            ? (uint8_t)cJSON_GetNumberValue(color_order_item)
            : sys_settings.leds.color_order;
    if (color_order >= LED_COLOR_ORDER_MAX)
    {
        msg = "Invalid LED color order";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    size_t leds = width * height;
    uint32_t limit = (uint32_t)cJSON_GetNumberValue(limit_item);
    if (limit < leds * CONFIG_FL_SINGLE_LED_CURRENT_MA / 50)
    {
        msg = "Current limit too low";
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    sys_settings.leds.width = width;
    sys_settings.leds.height = height;
    sys_settings.leds.type = type;
    sys_settings.leds.color_order = color_order;
    sys_settings.leds.rotation = rotation;
    if (mirror_item)
        sys_settings.leds.mirror = cJSON_IsTrue(mirror_item);
    sys_settings.leds.current_limit = limit;

    err = sys_settings_save();
    msg = err != ESP_OK
            ? "Error saving system settings"
            : "Settings saved, reboot to apply";

exit:
    if (json) cJSON_Delete(json);
    return respond_api(req, err, msg);
}

static const httpd_uri_t route_post_settings_leds = {
    .uri = "/api/settings/leds",
    .method = HTTP_POST,
    .handler = post_settings_leds
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_reboot(httpd_req_t *req)
{
    (void)req;
    esp_restart();

    return ESP_OK; // dummy
}

static const httpd_uri_t route_get_reboot = {
    .uri = "/api/reboot",
    .method = HTTP_GET,
    .handler = get_reboot
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_effects(httpd_req_t *req)
{
    cJSON *res = cJSON_CreateArray();
    for (size_t i = 0; i < effects_count; i++)
    {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddItemToArray(res, e);
        cJSON_AddStringToObject(e, "code", effects[i].code);
        cJSON_AddStringToObject(e, "name", effects[i].name);
        cJSON_AddBoolToObject(e, "demo", demo_is_included(i));
        cJSON_AddItemToObject(e, "params", effect_params_json(i));
    }

    return respond_json(req, res);
}

static const httpd_uri_t route_get_effects = {
    .uri = "/api/effects",
    .method = HTTP_GET,
    .handler = get_effects
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_effects_reset(httpd_req_t *req)
{
    esp_err_t res = effects_reset();
    return respond_api(req, res, res == ESP_OK ?
            "Effect settings reset" : "Error resetting effects");
}

static const httpd_uri_t route_get_effects_reset = {
    .uri = "/api/effects/reset",
    .method = HTTP_GET,
    .handler = get_effects_reset
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_lamp_state(httpd_req_t *req)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "on", surface_is_playing());
    cJSON_AddStringToObject(res, "effect", effects[vol_settings.effect].code);
    cJSON_AddNumberToObject(res, "brightness", vol_settings.brightness);
    cJSON_AddNumberToObject(res, "fps", vol_settings.fps);
    cJSON_AddBoolToObject(res, "demo", vol_settings.demo);

    return respond_json(req, res);
}

static const httpd_uri_t route_get_lamp_state = {
    .uri = "/api/lamp/state",
    .method = HTTP_GET,
    .handler = get_lamp_state
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t post_lamp_state(httpd_req_t *req)
{
    const char *msg = NULL;
    cJSON *json = NULL;

    esp_err_t err = parse_post_json(req, &msg, &json);
    if (err != ESP_OK)
        goto exit;

    cJSON *on_item = cJSON_GetObjectItem(json, "on");
    if (on_item && !cJSON_IsBool(on_item))
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid `on` item";
        goto exit;
    }
    cJSON *effect_item = cJSON_GetObjectItem(json, "effect");
    int effect = effect_item ? effect_find(cJSON_GetStringValue(effect_item)) : -1;
    if (effect_item && effect < 0)
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid `effect` item: unknown effect code";
        goto exit;
    }
    cJSON *demo_item = cJSON_GetObjectItem(json, "demo");
    if (demo_item && !cJSON_IsBool(demo_item))
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid `demo` item";
        goto exit;
    }
    cJSON *brightness_item = cJSON_GetObjectItem(json, "brightness");
    if (brightness_item && !cJSON_IsNumber(brightness_item))
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid `brightness` item";
        goto exit;
    }
    cJSON *fps_item = cJSON_GetObjectItem(json, "fps");
    if (fps_item && !cJSON_IsNumber(fps_item))
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid `fps` item";
        goto exit;
    }

    // manual control stops the dawn
    scheduler_cancel_dawn();

    if (effect_item)
    {
        // selecting an effect turns the demo mode off unless it is turned on in the same request
        if (!cJSON_IsTrue(demo_item))
            demo_stop();
        err = surface_set_effect(effect);
        if (err != ESP_OK)
        {
            msg = "Effect setting error";
            goto exit;
        }
    }
    if (demo_item)
    {
        if (cJSON_IsTrue(demo_item))
            demo_start();
        else
            demo_stop();
    }
    if (on_item)
    {
        err = cJSON_IsTrue(on_item) ? surface_play() : surface_stop();
        if (err != ESP_OK)
        {
            msg = "On/off lamp error";
            goto exit;
        }
    }
    if (brightness_item)
    {
        err = surface_set_brightness((uint8_t)cJSON_GetNumberValue(brightness_item));
        if (err != ESP_OK)
        {
            msg = "Brightness setting error";
            goto exit;
        }
    }
    if (fps_item)
    {
        err = surface_set_fps((uint8_t)cJSON_GetNumberValue(fps_item));
        if (err != ESP_OK)
        {
            msg = "FPS setting error";
            goto exit;
        }
    }

exit:
    if (json) cJSON_Delete(json);
    return respond_api(req, err, msg);
}

static const httpd_uri_t route_post_lamp_state = {
    .uri = "/api/lamp/state",
    .method = HTTP_POST,
    .handler = post_lamp_state
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_lamp_effect(httpd_req_t *req)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "effect", effects[vol_settings.effect].code);
    cJSON_AddItemToObject(res, "params", effect_params_json(vol_settings.effect));
    return respond_json(req, res);
}

static const httpd_uri_t route_get_lamp_effect = {
    .uri = "/api/lamp/effect",
    .method = HTTP_GET,
    .handler = get_lamp_effect
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t post_lamp_effect(httpd_req_t *req)
{
    const char *msg = NULL;
    cJSON *json = NULL;

    esp_err_t err = parse_post_json(req, &msg, &json);
    if (err != ESP_OK)
        goto exit;

    size_t effect = vol_settings.effect;

    cJSON *effect_item = cJSON_GetObjectItem(json, "effect");
    if (effect_item)
    {
        int e = effect_find(cJSON_GetStringValue(effect_item));
        if (e < 0)
        {
            err = ESP_ERR_INVALID_ARG;
            msg = "Invalid `effect` value: unknown effect code";
            goto exit;
        }
        effect = e;
    }

    // {"param_code": value, ...}, missing parameters keep their values
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (!cJSON_IsObject(params))
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid JSON, `params` must be object {code: value}";
        goto exit;
    }

    // validate everything first, then apply
    cJSON *item;
    cJSON_ArrayForEach(item, params)
    {
        int p = effect_param_find(effect, item->string);
        if (p < 0 || !cJSON_IsNumber(item)
                || cJSON_GetNumberValue(item) < effects[effect].params[p].min
                || cJSON_GetNumberValue(item) > effects[effect].params[p].max)
        {
            err = ESP_ERR_INVALID_ARG;
            msg = "Unknown parameter code or invalid value";
            goto exit;
        }
    }
    cJSON_ArrayForEach(item, params)
    {
        err = effect_param_set(effect, effect_param_find(effect, item->string), (uint8_t)cJSON_GetNumberValue(item));
        if (err != ESP_OK)
        {
            msg = "Error set parameter";
            goto exit;
        }
    }

exit:
    if (json) cJSON_Delete(json);
    return respond_api(req, err, msg);
}

static const httpd_uri_t route_post_lamp_effect = {
    .uri = "/api/lamp/effect",
    .method = HTTP_POST,
    .handler = post_lamp_effect
};

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_time(httpd_req_t *req)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char local[32];
    strftime(local, sizeof(local), "%Y-%m-%d %H:%M:%S", &t);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "time", (double)now);
    cJSON_AddStringToObject(res, "local", local);
    cJSON_AddStringToObject(res, "tz", systime_get_tz());
    cJSON_AddBoolToObject(res, "valid", systime_is_valid());
    cJSON_AddStringToObject(res, "source", systime_source_name(systime_source()));
    cJSON_AddBoolToObject(res, "rtc", systime_has_rtc());

    return respond_json(req, res);
}

static const httpd_uri_t route_get_time = {
    .uri = "/api/time",
    .method = HTTP_GET,
    .handler = get_time
};

static esp_err_t post_time(httpd_req_t *req)
{
    const char *msg = NULL;
    cJSON *json = NULL;

    esp_err_t err = parse_post_json(req, &msg, &json);
    if (err != ESP_OK)
        goto exit;

    cJSON *time_item = cJSON_GetObjectItem(json, "time");
    if (time_item && !cJSON_IsNumber(time_item))
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid `time` item";
        goto exit;
    }
    cJSON *tz_item = cJSON_GetObjectItem(json, "tz");
    if (tz_item && (!cJSON_IsString(tz_item) || !strlen(cJSON_GetStringValue(tz_item))
            || strlen(cJSON_GetStringValue(tz_item)) >= TZ_MAX_LEN))
    {
        err = ESP_ERR_INVALID_ARG;
        msg = "Invalid `tz` item";
        goto exit;
    }

    if (tz_item)
    {
        err = systime_set_tz(cJSON_GetStringValue(tz_item));
        if (err != ESP_OK)
        {
            msg = "Error setting time zone";
            goto exit;
        }
    }
    if (time_item)
    {
        err = systime_set((time_t)cJSON_GetNumberValue(time_item), TIME_SOURCE_API);
        if (err != ESP_OK)
        {
            msg = "Invalid time";
            goto exit;
        }
    }

exit:
    if (json) cJSON_Delete(json);
    return respond_api(req, err, msg);
}

static const httpd_uri_t route_post_time = {
    .uri = "/api/time",
    .method = HTTP_POST,
    .handler = post_time
};

////////////////////////////////////////////////////////////////////////////////

static cJSON *schedule_time_json(const schedule_time_t *t)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "enabled", t->enabled);
    cJSON_AddNumberToObject(res, "days", t->days);
    cJSON_AddNumberToObject(res, "hour", t->hour);
    cJSON_AddNumberToObject(res, "minute", t->minute);
    return res;
}

// Missing fields keep current values
static bool schedule_time_parse(cJSON *json, schedule_time_t *t)
{
    if (!json)
        return true;
    if (!cJSON_IsObject(json))
        return false;

    cJSON *item = cJSON_GetObjectItem(json, "enabled");
    if (item)
    {
        if (!cJSON_IsBool(item)) return false;
        t->enabled = cJSON_IsTrue(item);
    }
    const char *names[] = { "days", "hour", "minute" };
    uint8_t *fields[] = { &t->days, &t->hour, &t->minute };
    for (size_t i = 0; i < 3; i++)
    {
        item = cJSON_GetObjectItem(json, names[i]);
        if (!item) continue;
        if (!cJSON_IsNumber(item) || cJSON_GetNumberValue(item) < 0 || cJSON_GetNumberValue(item) > 255)
            return false;
        *fields[i] = (uint8_t)cJSON_GetNumberValue(item);
    }
    return true;
}

static bool number_parse(cJSON *json, const char *name, uint8_t *val)
{
    cJSON *item = cJSON_GetObjectItem(json, name);
    if (!item)
        return true;
    if (!cJSON_IsNumber(item) || cJSON_GetNumberValue(item) < 0 || cJSON_GetNumberValue(item) > 255)
        return false;
    *val = (uint8_t)cJSON_GetNumberValue(item);
    return true;
}

static esp_err_t get_schedule(httpd_req_t *req)
{
    schedule_t s;
    scheduler_get(&s);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "power_on", schedule_time_json(&s.power_on));
    cJSON_AddItemToObject(res, "power_off", schedule_time_json(&s.power_off));

    cJSON *dawn = schedule_time_json(&s.dawn.at);
    cJSON_AddNumberToObject(dawn, "duration", s.dawn.duration);
    cJSON_AddStringToObject(dawn, "effect", s.dawn.effect);
    cJSON_AddNumberToObject(dawn, "brightness", s.dawn.brightness);
    cJSON_AddItemToObject(res, "dawn", dawn);

    cJSON *hourly = cJSON_CreateObject();
    cJSON_AddBoolToObject(hourly, "enabled", s.hourly.enabled);
    cJSON *values = cJSON_CreateArray();
    for (size_t h = 0; h < 24; h++)
        cJSON_AddItemToArray(values, cJSON_CreateNumber(s.hourly.values[h]));
    cJSON_AddItemToObject(hourly, "values", values);
    cJSON_AddItemToObject(res, "hourly_brightness", hourly);

    return respond_json(req, res);
}

static const httpd_uri_t route_get_schedule = {
    .uri = "/api/schedule",
    .method = HTTP_GET,
    .handler = get_schedule
};

static esp_err_t post_schedule(httpd_req_t *req)
{
    const char *msg = NULL;
    cJSON *json = NULL;
    schedule_t s;
    scheduler_get(&s);

    esp_err_t err = parse_post_json(req, &msg, &json);
    if (err != ESP_OK)
        goto exit;

    err = ESP_ERR_INVALID_ARG;
    if (!schedule_time_parse(cJSON_GetObjectItem(json, "power_on"), &s.power_on))
    {
        msg = "Invalid `power_on` item";
        goto exit;
    }
    if (!schedule_time_parse(cJSON_GetObjectItem(json, "power_off"), &s.power_off))
    {
        msg = "Invalid `power_off` item";
        goto exit;
    }
    cJSON *dawn = cJSON_GetObjectItem(json, "dawn");
    if (!schedule_time_parse(dawn, &s.dawn.at)
            || (dawn && (!number_parse(dawn, "duration", &s.dawn.duration)
                || !number_parse(dawn, "brightness", &s.dawn.brightness))))
    {
        msg = "Invalid `dawn` item";
        goto exit;
    }
    cJSON *dawn_effect = dawn ? cJSON_GetObjectItem(dawn, "effect") : NULL;
    if (dawn_effect)
    {
        if (effect_find(cJSON_GetStringValue(dawn_effect)) < 0)
        {
            msg = "Invalid dawn effect code";
            goto exit;
        }
        strncpy(s.dawn.effect, cJSON_GetStringValue(dawn_effect), SCHEDULE_EFFECT_CODE_LEN);
        s.dawn.effect[SCHEDULE_EFFECT_CODE_LEN] = 0;
    }
    cJSON *hourly = cJSON_GetObjectItem(json, "hourly_brightness");
    if (hourly)
    {
        cJSON *enabled = cJSON_GetObjectItem(hourly, "enabled");
        cJSON *values = cJSON_GetObjectItem(hourly, "values");
        if (!cJSON_IsObject(hourly) || (enabled && !cJSON_IsBool(enabled))
                || (values && (!cJSON_IsArray(values) || cJSON_GetArraySize(values) != 24)))
        {
            msg = "Invalid `hourly_brightness` item";
            goto exit;
        }
        if (enabled)
            s.hourly.enabled = cJSON_IsTrue(enabled);
        for (size_t h = 0; values && h < 24; h++)
        {
            cJSON *v = cJSON_GetArrayItem(values, h);
            if (!cJSON_IsNumber(v) || cJSON_GetNumberValue(v) < 1 || cJSON_GetNumberValue(v) > 255)
            {
                msg = "Invalid hourly brightness value";
                goto exit;
            }
            s.hourly.values[h] = (uint8_t)cJSON_GetNumberValue(v);
        }
    }
    if (!schedule_validate(&s))
    {
        msg = "Invalid schedule values";
        goto exit;
    }

    err = scheduler_set(&s);
    if (err != ESP_OK)
        msg = "Error saving schedule";

exit:
    if (json) cJSON_Delete(json);
    return respond_api(req, err, msg);
}

static const httpd_uri_t route_post_schedule = {
    .uri = "/api/schedule",
    .method = HTTP_POST,
    .handler = post_schedule
};

////////////////////////////////////////////////////////////////////////////////

static esp_err_t get_demo(httpd_req_t *req)
{
    demo_settings_t d;
    demo_get_settings(&d);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "active", demo_is_active());
    cJSON_AddNumberToObject(res, "order", d.order);
    cJSON_AddNumberToObject(res, "duration", d.duration);
    cJSON_AddNumberToObject(res, "random", d.random);
    cJSON *excluded = cJSON_CreateArray();
    for (size_t e = 0; e < effects_count; e++)
        if (!demo_is_included(e))
            cJSON_AddItemToArray(excluded, cJSON_CreateString(effects[e].code));
    cJSON_AddItemToObject(res, "excluded", excluded);

    return respond_json(req, res);
}

static const httpd_uri_t route_get_demo = {
    .uri = "/api/demo",
    .method = HTTP_GET,
    .handler = get_demo
};

static esp_err_t post_demo(httpd_req_t *req)
{
    const char *msg = NULL;
    cJSON *json = NULL;
    demo_settings_t d;
    demo_get_settings(&d);

    esp_err_t err = parse_post_json(req, &msg, &json);
    if (err != ESP_OK)
        goto exit;

    err = ESP_ERR_INVALID_ARG;
    const char *names[] = { "order", "duration", "random" };
    for (size_t i = 0; i < 3; i++)
    {
        cJSON *item = cJSON_GetObjectItem(json, names[i]);
        if (!item) continue;
        if (!cJSON_IsNumber(item) || cJSON_GetNumberValue(item) < 0 || cJSON_GetNumberValue(item) > 65535)
        {
            msg = "Invalid demo settings";
            goto exit;
        }
        uint16_t v = (uint16_t)cJSON_GetNumberValue(item);
        if (i == 0) d.order = v; else if (i == 1) d.duration = v; else d.random = v;
    }
    // {"include": {"code": true/false, ...}}
    cJSON *include = cJSON_GetObjectItem(json, "include");
    cJSON *item;
    if (include)
    {
        if (!cJSON_IsObject(include))
        {
            msg = "Invalid `include` item";
            goto exit;
        }
        cJSON_ArrayForEach(item, include)
            if (effect_find(item->string) < 0 || !cJSON_IsBool(item))
            {
                msg = "Unknown effect code in `include`";
                goto exit;
            }
    }

    err = demo_set_settings(&d);
    if (err != ESP_OK)
    {
        msg = "Invalid demo settings";
        goto exit;
    }
    if (include)
        cJSON_ArrayForEach(item, include)
        {
            err = demo_set_included(effect_find(item->string), cJSON_IsTrue(item));
            if (err != ESP_OK)
            {
                msg = "Could not exclude the last included effect";
                goto exit;
            }
        }

exit:
    if (json) cJSON_Delete(json);
    return respond_api(req, err, msg);
}

static const httpd_uri_t route_post_demo = {
    .uri = "/api/demo",
    .method = HTTP_POST,
    .handler = post_demo
};

////////////////////////////////////////////////////////////////////////////////

esp_err_t api_init(httpd_handle_t server)
{
    CHECK(httpd_register_uri_handler(server, &route_get_info));
    CHECK(httpd_register_uri_handler(server, &route_get_settings_reset));
    CHECK(httpd_register_uri_handler(server, &route_get_settings_wifi));
    CHECK(httpd_register_uri_handler(server, &route_post_settings_wifi));
    CHECK(httpd_register_uri_handler(server, &route_get_settings_leds));
    CHECK(httpd_register_uri_handler(server, &route_post_settings_leds));
    CHECK(httpd_register_uri_handler(server, &route_get_reboot));
    CHECK(httpd_register_uri_handler(server, &route_get_effects));
    CHECK(httpd_register_uri_handler(server, &route_get_effects_reset));
    CHECK(httpd_register_uri_handler(server, &route_get_lamp_state));
    CHECK(httpd_register_uri_handler(server, &route_post_lamp_state));
    CHECK(httpd_register_uri_handler(server, &route_get_lamp_effect));
    CHECK(httpd_register_uri_handler(server, &route_post_lamp_effect));
    CHECK(httpd_register_uri_handler(server, &route_get_time));
    CHECK(httpd_register_uri_handler(server, &route_post_time));
    CHECK(httpd_register_uri_handler(server, &route_get_schedule));
    CHECK(httpd_register_uri_handler(server, &route_post_schedule));
    CHECK(httpd_register_uri_handler(server, &route_get_demo));
    CHECK(httpd_register_uri_handler(server, &route_post_demo));

    return ESP_OK;
}
