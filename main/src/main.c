#include "sys/socket.h"
#include "zephyr/logging/log.h"
#include "zephyr/net/dhcpv4_server.h"
#include "zephyr/net/http/server.h"
#include "zephyr/net/http/service.h"
#include "zephyr/net/net_if.h"
#include "zephyr/net/net_ip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_resources.h"

LOG_MODULE_REGISTER(MAIN);

#define BYTE_INVERSE_16(n) (n >> 8) | (n << 8)
#define BYTE_INVERSE_32(n)                                                  \
  BYTE_INVERSE_16((uint16_t)(n >> 16)) >> 16 | BYTE_INVERSE_16((uint16_t)n) \
                                                   << 16

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
    uint16_t RCODE : 4;
    uint16_t CD : 1;
    uint16_t AD : 1;
    uint16_t Z : 1;
    uint16_t RA : 1;
    uint16_t RD : 1;
    uint16_t TC : 1;
    uint16_t AA : 1;
    uint16_t OPCODE : 4;
    uint16_t QR : 1;
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

typedef struct _DNSQuestion {
  char* name;
  int name_len;
  uint16_t type;
  uint16_t class;
} DNSQuestion;

typedef struct _DNSAnswers {
  char* name;
  uint16_t type;
  uint16_t class;
  uint32_t ttl;
  uint16_t rdlength;
  uint32_t rdata;
} DNSAnswers;

typedef struct _DNSPacket {
  DNSHeader header;
  DNSQuestion question;
  DNSAnswers answer;
} DNSPacket;

typedef enum _DNSParserState {
  DNS_PARSER_STATE_HEADER,
  DNS_PARSER_STATE_HOSTNAME,
  DNS_PARSER_STATE_TYPE_CLASS,
} DNSParserState;

void dns_header_byte_inverse(DNSPacket* packet) {
  packet->header.flags.u16 = BYTE_INVERSE_16(packet->header.flags.u16);
  packet->header.number_of_additional_rrs =
      BYTE_INVERSE_16(packet->header.number_of_additional_rrs);
  packet->header.number_of_answers =
      BYTE_INVERSE_16(packet->header.number_of_answers);
  packet->header.number_of_authority_rrs =
      BYTE_INVERSE_16(packet->header.number_of_authority_rrs);
  packet->header.number_of_questions =
      BYTE_INVERSE_16(packet->header.number_of_questions);
  packet->header.transaction_id =
      BYTE_INVERSE_16(packet->header.transaction_id);
}

void dns_question_byte_inverse(DNSPacket* packet) {
  packet->question.type = BYTE_INVERSE_16(packet->question.type);
  packet->question.class = BYTE_INVERSE_16(packet->question.class);
}

void dns_answer_byte_inverse(DNSPacket* packet) {
  packet->answer.class = BYTE_INVERSE_16(packet->answer.class);
  packet->answer.type = BYTE_INVERSE_16(packet->answer.type);
  packet->answer.rdlength = BYTE_INVERSE_16(packet->answer.rdlength);
  packet->answer.ttl = BYTE_INVERSE_32(packet->answer.ttl);
}

void print_dns_packet(DNSPacket* packet) {
  printf("transaction id: 0x%x\n", packet->header.transaction_id);
  printf("flags:          0x%x\n", packet->header.flags.u16);
  printf("questions:      %d\n", packet->header.number_of_questions);
  printf("answers:        %d\n", packet->header.number_of_answers);
  printf("authority RRs:  %d\n", packet->header.number_of_authority_rrs);
  printf("additional RRs: %d\n", packet->header.number_of_additional_rrs);
  printf("-------------------------------------\n");
  printf("name:           %s\n", packet->question.name);
  printf("type:           0x%x\n", packet->question.type);
  printf("class:          0x%x\n", packet->question.class);
  printf("======================================\n");
  printf("\n");
}

void dns_packet_received_callback(int sock,
                                  struct sockaddr_in client_address,
                                  socklen_t client_address_len,
                                  DNSPacket* packet) {
  if (packet->header.flags.b.QR == 1 ||
      packet->header.number_of_questions == 0 ||
      strcmp(packet->question.name, "zephyr.local")) {
    return;
  }

  print_dns_packet(packet);

  packet->header.flags.b.QR = 1;
  packet->header.number_of_answers = 1;
  // packet->header.flags.b.RA = 0;
  packet->answer.name = malloc(2);
  packet->answer.name[0] = 0xc0;
  packet->answer.name[1] = 0x0c;
  packet->answer.class = 1;
  packet->answer.type = 1;
  packet->answer.ttl = 1;
  packet->answer.rdlength = 4;
  net_addr_pton(AF_INET, "192.168.10.1", &packet->answer.rdata);

  dns_header_byte_inverse(packet);
  dns_question_byte_inverse(packet);
  dns_answer_byte_inverse(packet);

  static uint8_t send_buffer[128];

  int buffer_index = 0;
  memcpy(&send_buffer[buffer_index], &packet->header, sizeof(DNSHeader));
  buffer_index += sizeof(DNSHeader);
  int label_len_index = buffer_index;
  buffer_index++;
  int label_len = 0;
  int name_index = 0;
  char c;
  while ((c = packet->question.name[name_index++])) {
    if (c == '.') {
      send_buffer[label_len_index] = label_len;
      label_len = 0;
      label_len_index = buffer_index++;
      continue;
    }
    send_buffer[buffer_index] = c;
    buffer_index++;
    label_len++;
  }
  send_buffer[label_len_index] = label_len;
  send_buffer[buffer_index] = '\0';
  buffer_index++;
  memcpy(&send_buffer[buffer_index], &packet->question.type, sizeof(uint16_t));
  buffer_index += 2;
  memcpy(&send_buffer[buffer_index], &packet->question.class, sizeof(uint16_t));
  buffer_index += 2;
  memcpy(&send_buffer[buffer_index], packet->answer.name, 2);
  buffer_index += 2;
  memcpy(&send_buffer[buffer_index], &packet->answer.type, sizeof(uint16_t));
  buffer_index += 2;
  memcpy(&send_buffer[buffer_index], &packet->answer.class, sizeof(uint16_t));
  buffer_index += 2;
  memcpy(&send_buffer[buffer_index], &packet->answer.ttl, sizeof(uint32_t));
  buffer_index += 4;
  memcpy(&send_buffer[buffer_index], &packet->answer.rdlength,
         sizeof(uint16_t));
  buffer_index += 2;
  memcpy(&send_buffer[buffer_index], &packet->answer.rdata, sizeof(uint32_t));
  buffer_index += 4;

  sendto(sock, send_buffer, buffer_index, 0, (struct sockaddr*)&client_address,
         client_address_len);

  dns_header_byte_inverse(packet);
  dns_question_byte_inverse(packet);
  dns_answer_byte_inverse(packet);
}

void free_dns_packet(DNSPacket* packet) {
  if (packet->question.name) {
    free(packet->question.name);
  }
  if (packet->answer.name) {
    free(packet->answer.name);
  }
}

DNSPacket new_dns_packet() {
  DNSPacket packet = {
      .answer =
          {
              .name = NULL,
          },
      .question =
          {
              .name = NULL,
              .name_len = 0,
          },
  };
  return packet;
}

void dns_service_start(struct in_addr interface_address) {
#define BUFFER_SIZE 128
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  struct sockaddr_in sock_addres = {
      .sin_addr = interface_address,
      .sin_port = htons(53),
      .sin_family = AF_INET,
  };

  int ret =
      bind(sock, (struct sockaddr*)&sock_addres, sizeof(struct sockaddr_in));
  if (ret != 0) {
    LOG_ERR("Bind failed");
  }

  DNSPacket packet;
  static uint8_t buffer[BUFFER_SIZE];
  int buffer_index = 0;

  DNSParserState dns_parser_state = DNS_PARSER_STATE_HEADER;
  while (true) {
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(struct sockaddr_in);
    int received_bytes =
        recvfrom(sock, buffer, BUFFER_SIZE, 0,
                 (struct sockaddr*)&client_address, &client_address_len);

    buffer_index = 0;
    dns_parser_state = DNS_PARSER_STATE_HEADER;
    packet = new_dns_packet();
    while (buffer_index < received_bytes) {
      if (dns_parser_state == DNS_PARSER_STATE_HEADER) {
        if (buffer_index + sizeof(DNSHeader) > received_bytes) {
          break;
        }
        memcpy(&packet.header, &buffer[buffer_index], sizeof(DNSHeader));
        buffer_index += sizeof(DNSHeader);
        dns_parser_state = DNS_PARSER_STATE_HOSTNAME;
        dns_header_byte_inverse(&packet);
      } else if (dns_parser_state == DNS_PARSER_STATE_HOSTNAME) {
        int label_len = buffer[buffer_index];
        if (buffer_index + label_len > received_bytes) {
          break;
        }
        if (label_len == 0 && packet.question.name != NULL) {
          packet.question.name[packet.question.name_len - 1] = '\0';
          // here name_len will represent the string len not array len
          packet.question.name_len--;
          dns_parser_state = DNS_PARSER_STATE_TYPE_CLASS;
          buffer_index++;
          continue;
        }
        buffer_index++;
        // len here represent the array len not string len
        int name_len = packet.question.name_len;
        int new_name_len = name_len + label_len + 1;
        packet.question.name = realloc(packet.question.name, new_name_len);
        memcpy(&packet.question.name[name_len], &buffer[buffer_index],
               label_len);
        packet.question.name[new_name_len - 1] = '.';
        packet.question.name_len = new_name_len;
        buffer_index += label_len;
      } else if (dns_parser_state == DNS_PARSER_STATE_TYPE_CLASS) {
        if (buffer_index + 2 * sizeof(uint16_t) > received_bytes) {
          break;
        }
        memcpy(&packet.question.type, &buffer[buffer_index], sizeof(uint16_t));
        buffer_index += sizeof(uint16_t);
        memcpy(&packet.question.class, &buffer[buffer_index], sizeof(uint16_t));
        buffer_index += sizeof(uint16_t);
        dns_question_byte_inverse(&packet);
        dns_answer_byte_inverse(&packet);
        dns_packet_received_callback(sock, client_address, client_address_len,
                                     &packet);
        free_dns_packet(&packet);
      }
    }
  }
}

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