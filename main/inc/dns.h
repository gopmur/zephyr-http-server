#pragma once

#include <stdint.h>

#include "zephyr/net/net_ip.h"

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

void dns_service_start(struct in_addr interface_address);