#include "zephyr/net/dhcpv4_server.h"
#include "zephyr/net/http/server.h"
#include "zephyr/net/http/service.h"
#include "zephyr/net/net_if.h"
#include "zephyr/net/net_ip.h"

#include <stdio.h>
#include "http_resources.h"

#define BYTE_INVERSE_16(n) (n >> 8) | (n << 8)

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

typedef union _DNSFlags {
  struct {
    uint16_t QR : 1;
    uint16_t OPCODE : 4;
    uint16_t AA : 1;
    uint16_t TC : 1;
    uint16_t RD : 1;
    uint16_t RA : 1;
    uint16_t Z : 1;
    uint16_t AD : 1;
    uint16_t CD : 1;
    uint16_t RCODE : 4;
  } b;
  uint16_t u16;
} DNSFlags;

typedef struct _DNSHeader {
  uint16_t transaction_id;
  DNSFlags flags;
  uint16_t number_of_questions;
  uint16_t number_of_answers;
  uint16_t number_of_authority_rrs;
  uint16_t number_of_additional_rrs;
} DNSHeader;

typedef struct _DNSTypeClass {
  uint16_t type;
  uint16_t class;
} DNSTypeClass;

typedef enum _DNSParserState {
  DNS_PARSER_STATE_HEADER,
  DNS_PARSER_STATE_HOSTNAME,
  DNS_PARSER_STATE_TYPE_CLASS,
} DNSParserState;

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

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  struct sockaddr_in sock_addres = {
      .sin_addr = interface_address,
      .sin_port = htons(53),
      .sin_family = AF_INET,
  };

  int ret =
      bind(sock, (struct sockaddr*)&sock_addres, sizeof(struct sockaddr_in));
  if (ret != 0) {
    printk("FUCK\n");
  }

  int packet_received = 0;
  int domain_index = 0;
  static uint8_t domain_buffer[64];
  static DNSTypeClass dns_type_class;
  static DNSHeader dns_header;
  static uint8_t buffer[512];
  int buffer_index = 0;

  DNSParserState dns_parser_state = DNS_PARSER_STATE_HEADER;
  while (true) {
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(struct sockaddr_in);
    int received_bytes =
        recvfrom(sock, buffer, 512, 0, (struct sockaddr*)&client_address,
                 &client_address_len);

    buffer_index = 0;
    while (buffer_index < received_bytes) {
      switch (dns_parser_state) {
        case DNS_PARSER_STATE_HEADER:
          if (buffer_index + sizeof(DNSHeader) > received_bytes) {
            memcpy(&((uint8_t*)(&dns_header))[packet_received],
                   &buffer[buffer_index], received_bytes - buffer_index);
            packet_received += received_bytes - buffer_index;
            buffer_index += received_bytes - buffer_index;
          } else {
            memcpy(&((uint8_t*)(&dns_header))[packet_received],
                   &buffer[buffer_index], sizeof(DNSHeader));
            packet_received += sizeof(DNSHeader);
            buffer_index += sizeof(DNSHeader);
            domain_index = 0;
            dns_parser_state = DNS_PARSER_STATE_HOSTNAME;
            dns_header.flags.u16 = BYTE_INVERSE_16(dns_header.flags.u16);
            dns_header.number_of_additional_rrs =
                BYTE_INVERSE_16(dns_header.number_of_additional_rrs);
            dns_header.number_of_answers =
                BYTE_INVERSE_16(dns_header.number_of_answers);
            dns_header.number_of_authority_rrs =
                BYTE_INVERSE_16(dns_header.number_of_authority_rrs);
            dns_header.number_of_questions =
                BYTE_INVERSE_16(dns_header.number_of_questions);
            dns_header.transaction_id =
                BYTE_INVERSE_16(dns_header.transaction_id);
          }
          break;
        case DNS_PARSER_STATE_HOSTNAME:
          while (buffer_index < received_bytes &&
                 buffer[buffer_index] != '\0') {
            domain_buffer[domain_index] = buffer[buffer_index];
            domain_index++;
            buffer_index++;
            packet_received++;
          }
          if (buffer_index < received_bytes) {
            domain_buffer[domain_index] = '\0';
            dns_parser_state = DNS_PARSER_STATE_TYPE_CLASS;
          }
          buffer_index++;
          break;
        case DNS_PARSER_STATE_TYPE_CLASS:
          if (buffer_index + sizeof(DNSTypeClass) > received_bytes) {
            memcpy(
                &((uint8_t*)(&dns_type_class))[packet_received - domain_index -
                                               sizeof(DNSHeader)],
                &buffer[buffer_index], received_bytes - buffer_index);
            packet_received += received_bytes - buffer_index;
            buffer_index += received_bytes - buffer_index;
          } else {
            memcpy(
                &((uint8_t*)(&dns_type_class))[packet_received - domain_index -
                                               sizeof(DNSHeader)],
                &buffer[buffer_index], sizeof(DNSTypeClass));
            dns_type_class.class = BYTE_INVERSE_16(dns_type_class.class);
            dns_type_class.type = BYTE_INVERSE_16(dns_type_class.type);
            packet_received = 0;
            buffer_index += sizeof(DNSTypeClass);
            dns_parser_state = DNS_PARSER_STATE_HEADER;
            str_replace(domain_buffer, 3, '.');
            printf("DNS transaction id:           %x\n",
                   dns_header.transaction_id);
            printf("DNS flags:                    %x\n", dns_header.flags.u16);
            printf("DNS number of questions:      %d\n",
                   dns_header.number_of_questions);
            printf("DNS number of answers id:     %d\n",
                   dns_header.number_of_answers);
            printf("DNS number of authority RRs:  %d\n",
                   dns_header.number_of_authority_rrs);
            printf("DNS number of additional RRs: %d\n",
                   dns_header.number_of_additional_rrs);
            printf("DNS name:                     %s\n", domain_buffer);
            printf("DNS type:                     %x\n", dns_type_class.type);
            printf("DNS class id:                 %x\n", dns_type_class.class);
            printf("======================================\n");
            fflush(stdout);
          }
          break;
      }
    }
  }
  return 0;
}