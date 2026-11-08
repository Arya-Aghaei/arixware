idf_component_register(
    SRCS "net_wifi.c" "net_time.c" "net_timezone.c" "net_profile.c" "net_http.c" "net_remote.c" "net_dns.c" "net_dns_packet.c" "net_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server esp_timer json esp_http_client mbedtls esp_https_ota app_update esp_app_format esp_system
    PRIV_REQUIRES eyes_renderer
    EMBED_TXTFILES "setup.html" "remote/app.html" "remote/manifest.json"
    EMBED_FILES "remote/icon.png" "remote/icon-192.png" "remote/eyes.png"
)
