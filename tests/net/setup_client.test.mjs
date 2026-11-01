import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {execFileSync} from 'node:child_process';

const source=readFileSync(new URL('../../components/net/net_http.c',import.meta.url),'utf8');
const functions=source.slice(source.indexOf('static bool setup_ipv4_address('),source.indexOf('static esp_err_t page_handler('));
const harness=`
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define CONFIG_LWIP_IPV6 1
#define ESP_OK 0
#define HTTP_POST 1
#define ESP_LOGW(...) ((void)0)
typedef struct { bool setup_active; } net_wifi_status_t;
typedef struct { int method; } httpd_req_t;
static bool active = true, peer_fail, local_fail;
static struct sockaddr_storage peer_address, local_address;
static socklen_t peer_length, local_length;
static const char *origin = "";
static bool forbidden;
static void net_wifi_get_status(net_wifi_status_t *status) { status->setup_active = active; }
static int httpd_req_to_sockfd(httpd_req_t *req) { (void)req; return 1; }
static void httpd_resp_set_status(httpd_req_t *req, const char *status) {
    (void)req; forbidden = !strcmp(status, "403 Forbidden");
}
static void httpd_resp_sendstr(httpd_req_t *req, const char *body) { (void)req; (void)body; }
static size_t httpd_req_get_hdr_value_len(httpd_req_t *req, const char *header) {
    (void)req; (void)header; return strlen(origin);
}
static int httpd_req_get_hdr_value_str(httpd_req_t *req, const char *header, char *out, size_t size) {
    (void)req; (void)header;
    if (strlen(origin) >= size) return -1;
    strcpy(out, origin); return ESP_OK;
}
static int mock_peer(int fd, struct sockaddr *out, socklen_t *size) {
    (void)fd;
    if (peer_fail) return -1;
    assert(*size >= peer_length);
    memcpy(out, &peer_address, peer_length); *size = peer_length; return 0;
}
static int mock_local(int fd, struct sockaddr *out, socklen_t *size) {
    (void)fd;
    if (local_fail) return -1;
    assert(*size >= local_length);
    memcpy(out, &local_address, local_length); *size = local_length; return 0;
}
#define getpeername mock_peer
#define getsockname mock_local
${functions}
static socklen_t address(struct sockaddr_storage *out, int family, const char *ip) {
    memset(out, 0, sizeof(*out));
    if (family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)out;
        a->sin_family = family;
        assert(inet_pton(family, ip, &a->sin_addr) == 1);
        return sizeof(*a);
    }
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)out;
    a->sin6_family = family;
    assert(inet_pton(family, ip, &a->sin6_addr) == 1);
    return sizeof(*a);
}
static void check(int method, bool allowed) {
    httpd_req_t req = { .method = method };
    forbidden = false;
    assert(setup_client(&req) == allowed);
    assert(forbidden == !allowed);
}
int main(void) {
    for (int mapped = 0; mapped < 2; mapped++) {
        int family = mapped ? AF_INET6 : AF_INET;
        peer_length = address(&peer_address, family, mapped ? "::ffff:192.168.4.2" : "192.168.4.2");
        local_length = address(&local_address, family, mapped ? "::ffff:192.168.4.1" : "192.168.4.1");
        check(0, true); check(HTTP_POST, true);
        origin = "http://192.168.4.1"; check(HTTP_POST, true);
        origin = "http://other.example"; check(HTTP_POST, false); check(0, true);
        origin = "";
        active = false; check(0, false); active = true;
        peer_fail = true; check(0, false); peer_fail = false;
        local_fail = true; check(0, false); local_fail = false;
        socklen_t saved = peer_length;
        peer_length = 2; check(0, false); peer_length = saved;
        saved = local_length;
        local_length = 2; check(0, false); local_length = saved;
        local_length = address(&local_address, family, mapped ? "::ffff:192.168.1.9" : "192.168.1.9");
        check(0, false);
        local_length = address(&local_address, family, mapped ? "::ffff:192.168.4.1" : "192.168.4.1");
        peer_length = address(&peer_address, family, mapped ? "::ffff:192.168.1.2" : "192.168.1.2");
        check(0, false);
    }
    peer_length = address(&peer_address, AF_INET6, "::1"); check(0, false);
    peer_address.ss_family = AF_UNSPEC; check(0, false);
    puts("Setup access: IPv4/mapped IPv6, subnet, local interface, origin and socket failures passed");
}
`;
const directory=mkdtempSync(join(tmpdir(),'arixware-setup-client-'));
try {
  const file=join(directory,'test.c'), binary=join(directory,'test');
  writeFileSync(file,harness);
  execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',file,'-o',binary],{stdio:'inherit'});
  execFileSync(binary,[],{stdio:'inherit'});
} finally {
  rmSync(directory,{recursive:true,force:true});
}
