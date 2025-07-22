#include <stdint.h>

#include "zephyr/logging/log.h"
#include "zephyr/net/dhcpv4_server.h"
#include "zephyr/net/http/server.h"
#include "zephyr/net/http/service.h"
#include "zephyr/net/net_if.h"
#include "zephyr/net/net_ip.h"

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

REGISTER_STATIC_RESOURCES(http_server)

int main() {
  struct net_if* ethernet_interface = net_if_get_default();
  struct in_addr interface_address;
  struct in_addr subnet_mask;
  struct in_addr dhcp_base_address;

  net_addr_pton(AF_INET, "192.168.10.1", &interface_address);
  net_addr_pton(AF_INET, "255.255.255.0", &subnet_mask);
  net_addr_pton(AF_INET, "192.168.10.10", &dhcp_base_address);

  net_if_ipv4_addr_add(ethernet_interface, &interface_address, NET_ADDR_MANUAL,
                       0);
  net_if_ipv4_set_netmask_by_addr(ethernet_interface, &interface_address,
                                  &subnet_mask);
  net_if_ipv4_set_gw(ethernet_interface, &interface_address);
  net_dhcpv4_server_start(ethernet_interface, &dhcp_base_address);

  http_server_start();

  dns_service_start(interface_address);
  return 0;
}