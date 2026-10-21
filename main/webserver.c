#include "webserver.h"
#include <esp_http_server.h>
#include "api.h"

static httpd_handle_t server = NULL;

////////////////////////////////////////////////////////////////////////////////
/// Init

static esp_err_t init()
{
    // API handlers
    return api_init(server);
}

////////////////////////////////////////////////////////////////////////////////
/// Public

esp_err_t webserver_restart()
{
    if (server)
    {
        ESP_LOGI(TAG, "HTTPD started, trying to stop...");
        httpd_stop(server);
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    //config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 100;
    config.stack_size = HTTPD_STACK_SIZE;
    config.lru_purge_enable = true;

    CHECK(httpd_start(&server, &config));
    CHECK(init());

    ESP_LOGI(TAG, "HTTPD started on port %d, free mem: %lu bytes", config.server_port, esp_get_free_heap_size());

    return ESP_OK;
}

void webserver_stop()
{
    if (!server)
        return;
    httpd_stop(server);
    server = NULL;
    ESP_LOGI(TAG, "HTTPD stopped");
}
