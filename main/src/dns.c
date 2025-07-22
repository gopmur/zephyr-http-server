#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys/socket.h"
#include "zephyr/logging/log.h"
#include "zephyr/net/net_ip.h"

#include "dns.h"
#include "helper.h"

LOG_MODULE_REGISTER(DNS);

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

  static uint8_t send_buffer[64];

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
  const int buffer_size = 64;
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  struct sockaddr_in sock_addres = {
      .sin_addr = interface_address,
      .sin_port = htons(53),
      .sin_family = AF_INET,
  };

  int ret =
      bind(sock, (struct sockaddr*)&sock_addres, sizeof(struct sockaddr_in));
  if (ret != 0) {
    LOG_ERR("Binding to ");
  }

  DNSPacket packet;
  uint8_t buffer[buffer_size];
  int i = 0;

  DNSParserState dns_parser_state = DNS_PARSER_STATE_HEADER;
  while (true) {
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(struct sockaddr_in);
    int bytes_received =
        recvfrom(sock, buffer, buffer_size, 0,
                 (struct sockaddr*)&client_address, &client_address_len);

    i = 0;
    dns_parser_state = DNS_PARSER_STATE_HEADER;
    packet = new_dns_packet();
    while (i < bytes_received) {
      if (dns_parser_state == DNS_PARSER_STATE_HEADER) {
        if (i + sizeof(DNSHeader) > bytes_received) {
          break;
        }
        memcpy(&packet.header, &buffer[i], sizeof(DNSHeader));
        i += sizeof(DNSHeader);
        dns_parser_state = DNS_PARSER_STATE_HOSTNAME;
        dns_header_byte_inverse(&packet);
      } else if (dns_parser_state == DNS_PARSER_STATE_HOSTNAME) {
        int label_len = buffer[i];
        if (i + label_len > bytes_received) {
          break;
        }
        if (label_len == 0 && packet.question.name != NULL) {
          packet.question.name[packet.question.name_len - 1] = '\0';
          // here name_len will represent the string len not array len
          packet.question.name_len--;
          dns_parser_state = DNS_PARSER_STATE_TYPE_CLASS;
          i++;
          continue;
        }
        i++;
        // len here represent the array len not string len
        int name_len = packet.question.name_len;
        int new_name_len = name_len + label_len + 1;
        packet.question.name = realloc(packet.question.name, new_name_len);
        memcpy(&packet.question.name[name_len], &buffer[i], label_len);
        packet.question.name[new_name_len - 1] = '.';
        packet.question.name_len = new_name_len;
        i += label_len;
      } else if (dns_parser_state == DNS_PARSER_STATE_TYPE_CLASS) {
        if (i + 2 * sizeof(uint16_t) > bytes_received) {
          break;
        }
        memcpy(&packet.question.type, &buffer[i], sizeof(uint16_t));
        i += sizeof(uint16_t);
        memcpy(&packet.question.class, &buffer[i], sizeof(uint16_t));
        i += sizeof(uint16_t);
        dns_question_byte_inverse(&packet);
        dns_answer_byte_inverse(&packet);
        dns_packet_received_callback(sock, client_address, client_address_len,
                                     &packet);
        free_dns_packet(&packet);
      }
    }
  }
}