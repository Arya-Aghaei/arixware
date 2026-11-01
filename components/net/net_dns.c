/* Minimal captive-portal DNS: answer A queries from the setup subnet only. */
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "esp_log.h"
#include "net_wifi.h"
#include "net_dns_packet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "setup_dns";

static void dns_task(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ESP_LOGE(TAG, "DNS socket/bind failed: errno=%d", errno);
        if (fd >= 0) close(fd);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Listening on UDP port 53");
    uint8_t packet[512];
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int len = recvfrom(fd, packet, sizeof(packet) - 16, 0, (struct sockaddr *)&peer, &peer_len);
        net_wifi_status_t status;
        net_wifi_get_status(&status);
        if (len < 17 || !status.setup_active || (ntohl(peer.sin_addr.s_addr) & 0xffffff00U) != 0xc0a80400U ||
            packet[4] != 0 || packet[5] != 1 || (packet[2] & 0xf8)) continue;
        ESP_LOGI(TAG, "Setup client DNS query (%d bytes)", len);
        size_t end = net_dns_reply(packet, len, sizeof(packet));
        if (!end) continue;
        if (sendto(fd, packet, end, 0, (struct sockaddr *)&peer, peer_len) < 0) {
            ESP_LOGW(TAG, "DNS reply failed: errno=%d", errno);
        }
    }
}

void net_dns_start(void)
{
    ESP_ERROR_CHECK(xTaskCreate(dns_task, "setup_dns", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
