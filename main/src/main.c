#include <stdint.h>

#include "zephyr/logging/log.h"
#include "zephyr/net/dhcpv4_server.h"
#include "zephyr/net/http/server.h"
#include "zephyr/net/http/service.h"
#include "zephyr/net/net_if.h"
#include "zephyr/net/net_ip.h"
#include "zephyr/net/net_mgmt.h"
#include "zephyr/net/wifi.h"
#include "zephyr/net/wifi_mgmt.h"

#include "http_resources.h"

#include "dns.h"

LOG_MODULE_REGISTER(MAIN);

void str_replace(char* str, char old, char new) {
  int i = 0;
  while (str[i] != '\0') {
    if (str[i] == old) {
      str[i] = new;
    }
    i++;
  }
}

static uint16_t http_port = 80;
HTTP_SERVICE_DEFINE(http_service,
                    "192.168.10.1",
                    &http_port,
                    2,
                    3,
                    "Default http service on 80",
                    NULL);

int dyn_handler(struct http_client_ctx* client,
                enum http_data_status status,
                const struct http_request_ctx* request_ctx,
                struct http_response_ctx* response_ctx,
                void* user_data) {
  // static const char response[] = "Hello";  // Persistent storage

  // thread_analyzer_print(0);

  if (status == HTTP_SERVER_DATA_FINAL) {
    LOG_INF("Hello");
    response_ctx->status = 200;
    response_ctx->body = "Hello";  // Safe: static buffer
    response_ctx->body_len = strlen("Hello");
    response_ctx->final_chunk = true;
  }
  return 0;
}
struct http_resource_detail_dynamic dyn_resource_detail = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_DYNAMIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
            .content_type = "text/plain",
        },
    .cb = dyn_handler,
    .user_data = NULL,
};

HTTP_RESOURCE_DEFINE(dyn_resource,
                     http_service,
                     "/toggle-led",
                     &dyn_resource_detail);

REGISTER_STATIC_RESOURCES(http_server)

int main() {
  struct net_if* iface = net_if_get_wifi_sap();
  struct in_addr iface_address;
  struct in_addr subnet_mask;
  struct in_addr dhcp_base_address;

  net_addr_pton(AF_INET, "192.168.10.1", &iface_address);
  net_addr_pton(AF_INET, "255.255.255.0", &subnet_mask);
  net_addr_pton(AF_INET, "192.168.10.10", &dhcp_base_address);

  net_if_ipv4_addr_add(iface, &iface_address, NET_ADDR_MANUAL, 0);
  net_if_ipv4_set_netmask_by_addr(iface, &iface_address, &subnet_mask);
  net_if_ipv4_set_gw(iface, &iface_address);

  struct wifi_connect_req_params ap_config = {
      .ssid = "Gopmur ESP-32",
      .ssid_length = strlen("Gopmur ESP-32"),
      .psk = "12345678",
      .psk_length = strlen("12345678"),
      .band = WIFI_FREQ_BAND_2_4_GHZ,
      .channel = 6,
      .security = WIFI_SECURITY_TYPE_PSK,
  };

  net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_config,
           sizeof(struct wifi_connect_req_params));

  net_dhcpv4_server_start(iface, &dhcp_base_address);

  http_server_start();

  dns_service_start(iface_address);
  return 0;
}