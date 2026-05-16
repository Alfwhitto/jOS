#include "net.h"

#include "debug.h"
#include "rtl8139.h"
#include "timer.h"

#define NET_ETHERTYPE_ARP 0x0806
#define NET_ETHERTYPE_IPV4 0x0800
#define NET_ETHERTYPE_IPV6 0x86DD
#define NET_ARP_HTYPE_ETHERNET 1
#define NET_ARP_OPERATION_REQUEST 1
#define NET_ARP_OPERATION_REPLY 2
#define NET_IP_PROTOCOL_ICMP 1
#define NET_IP_PROTOCOL_ICMPV6 58
#define NET_ICMP_TYPE_ECHO_REPLY 0
#define NET_ICMP_TYPE_ECHO_REQUEST 8
#define NET_ICMPV6_TYPE_ECHO_REQUEST 128
#define NET_ICMPV6_TYPE_ECHO_REPLY 129
#define NET_ICMPV6_TYPE_NEIGHBOR_SOLICITATION 135
#define NET_ICMPV6_TYPE_NEIGHBOR_ADVERTISEMENT 136
#define NET_ETHERNET_HEADER_SIZE 14
#define NET_IP_HEADER_SIZE 20
#define NET_IPV6_HEADER_SIZE 40
#define NET_ICMP_HEADER_SIZE 8
#define NET_ICMPV6_ND_OPTION_SOURCE_LLA 1
#define NET_ICMPV6_ND_OPTION_TARGET_LLA 2
#define NET_PING_TIMEOUT_TICKS 300

typedef struct ethernet_header {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t ethertype;
} __attribute__((packed)) ethernet_header_t;

typedef struct arp_packet {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_size;
    uint8_t protocol_size;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} __attribute__((packed)) arp_packet_t;

typedef struct ipv4_header {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint8_t source_ip[4];
    uint8_t destination_ip[4];
} __attribute__((packed)) ipv4_header_t;

typedef struct icmp_echo_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_echo_header_t;

typedef struct ipv6_header {
    uint32_t version_traffic_flow;
    uint16_t payload_length;
    uint8_t next_header;
    uint8_t hop_limit;
    uint8_t source_ip[16];
    uint8_t destination_ip[16];
} __attribute__((packed)) ipv6_header_t;

typedef struct icmpv6_neighbor_message {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint32_t flags_or_reserved;
    uint8_t target_address[16];
} __attribute__((packed)) icmpv6_neighbor_message_t;

static uint8_t net_local_mac[6];
static uint8_t net_local_ip[4] = {10, 0, 2, 15};
static uint8_t net_netmask[4] = {255, 255, 255, 0};
static uint8_t net_ipv4_gateway[4] = {10, 0, 2, 2};
static uint8_t net_local_ipv6[16] = {0xFE, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0F};
static uint8_t net_ipv6_prefix[16] = {0xFE, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t net_ipv6_gateway[16] = {0xFE, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02};
static uint8_t net_ipv6_dns[16] = {0xFE, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03};
static uint8_t net_cached_arp_ip[4];
static uint8_t net_cached_arp_mac[6];
static int net_have_arp_cache = 0;
static uint8_t net_cached_nd_ip[16];
static uint8_t net_cached_nd_mac[6];
static int net_have_nd_cache = 0;
static uint16_t net_next_ip_identification = 1;
static uint16_t net_next_ping_sequence = 1;
static uint16_t net_next_ping6_sequence = 1;
static uint16_t net_active_ping_identifier = 0x4B52;
static uint16_t net_active_ping_sequence = 0;
static int net_waiting_for_ping_reply = 0;
static int net_ping_reply_received = 0;
static uint16_t net_active_ping6_identifier = 0x3652;
static uint16_t net_active_ping6_sequence = 0;
static int net_waiting_for_ping6_reply = 0;
static int net_ping6_reply_received = 0;
static uint8_t net_active_ping6_target[16];
static uint32_t net_ping_start_tick = 0;

static uint16_t net_swap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static void net_memcpy(uint8_t *dest, const uint8_t *src, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }
}

static void net_memset(uint8_t *dest, uint8_t value, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        dest[i] = value;
    }
}

static int net_memcmp(const uint8_t *left, const uint8_t *right, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        if (left[i] != right[i]) {
            return (int)left[i] - (int)right[i];
        }
    }
    return 0;
}

static int net_strlen(const char *text) {
    int length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static void net_debug_write_ip(const uint8_t *ip) {
    for (uint32_t i = 0; i < 4; i++) {
        kdebug_write_dec(ip[i]);
        if (i != 3) {
            kdebug_put('.');
        }
    }
}

static int net_ipv4_is_on_link(const uint8_t ip[4]) {
    for (uint32_t i = 0; i < 4; i++) {
        if ((ip[i] & net_netmask[i]) != (net_local_ip[i] & net_netmask[i])) {
            return 0;
        }
    }
    return 1;
}

static void net_debug_write_ipv6(const uint8_t *ip) {
    for (uint32_t i = 0; i < 8; i++) {
        uint16_t word = (uint16_t)((ip[i * 2] << 8) | ip[i * 2 + 1]);
        uint8_t started = 0;
        for (int nibble = 12; nibble >= 0; nibble -= 4) {
            uint8_t value = (uint8_t)((word >> nibble) & 0x0F);
            if (value != 0 || started || nibble == 0) {
                started = 1;
                kdebug_put((char)(value < 10 ? ('0' + value) : ('a' + (value - 10))));
            }
        }
        if (i != 7) {
            kdebug_put(':');
        }
    }
}

static void net_debug_write_mac(const uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) {
        uint8_t byte = mac[i];
        char high = (char)((byte >> 4) & 0x0F);
        char low = (char)(byte & 0x0F);
        kdebug_put((char)(high < 10 ? ('0' + high) : ('A' + (high - 10))));
        kdebug_put((char)(low < 10 ? ('0' + low) : ('A' + (low - 10))));
        if (i != 5) {
            kdebug_put(':');
        }
    }
}

static int net_parse_hex_group(const char *start, int length, uint16_t *value) {
    uint16_t result = 0;
    if (length <= 0 || length > 4) {
        return 0;
    }

    for (int i = 0; i < length; i++) {
        char c = start[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = (uint8_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = (uint8_t)(10 + (c - 'a'));
        } else if (c >= 'A' && c <= 'F') {
            nibble = (uint8_t)(10 + (c - 'A'));
        } else {
            return 0;
        }
        result = (uint16_t)((result << 4) | nibble);
    }

    *value = result;
    return 1;
}

static int net_parse_ip(const char *text, uint8_t out_ip[4]) {
    uint32_t octet = 0;
    uint32_t octet_index = 0;

    if (text == 0 || text[0] == '\0') {
        return 0;
    }

    for (uint32_t i = 0;; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            octet = (octet * 10) + (uint32_t)(c - '0');
            if (octet > 255) {
                return 0;
            }
        } else if (c == '.' || c == '\0') {
            if (octet_index >= 4) {
                return 0;
            }
            out_ip[octet_index++] = (uint8_t)octet;
            octet = 0;
            if (c == '\0') {
                break;
            }
        } else {
            return 0;
        }
    }

    return octet_index == 4;
}

static int net_parse_ipv6(const char *text, uint8_t out_ip[16]) {
    uint16_t groups[8];
    int group_count = 0;
    int compress_index = -1;
    int length;
    int cursor = 0;

    if (text == 0 || text[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        groups[i] = 0;
    }

    length = net_strlen(text);
    while (cursor < length) {
        if (text[cursor] == ':') {
            if (cursor + 1 < length && text[cursor + 1] == ':') {
                if (compress_index != -1) {
                    return 0;
                }
                compress_index = group_count;
                cursor += 2;
                if (cursor >= length) {
                    break;
                }
                continue;
            }
            return 0;
        }

        int start = cursor;
        while (cursor < length && text[cursor] != ':') {
            cursor++;
        }

        if (group_count >= 8 || !net_parse_hex_group(text + start, cursor - start, &groups[group_count])) {
            return 0;
        }
        group_count++;

        if (cursor < length && text[cursor] == ':' &&
            !(cursor + 1 < length && text[cursor + 1] == ':')) {
            cursor++;
        }
    }

    if (compress_index != -1) {
        int zeros_to_insert = 8 - group_count;
        for (int i = group_count - 1; i >= compress_index; i--) {
            groups[i + zeros_to_insert] = groups[i];
        }
        for (int i = 0; i < zeros_to_insert; i++) {
            groups[compress_index + i] = 0;
        }
        group_count = 8;
    }

    if (group_count != 8) {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        out_ip[i * 2] = (uint8_t)(groups[i] >> 8);
        out_ip[i * 2 + 1] = (uint8_t)(groups[i] & 0xFF);
    }
    return 1;
}

static uint16_t net_checksum(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) {
        sum += (uint32_t)((data[i] << 8) | data[i + 1]);
    }
    if (length & 1U) {
        sum += (uint32_t)(data[length - 1] << 8);
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static uint16_t net_checksum_words(uint32_t sum) {
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static uint16_t net_checksum_ipv6_pseudo(const uint8_t source[16], const uint8_t destination[16],
                                         uint8_t next_header, const uint8_t *payload, uint32_t payload_length) {
    uint32_t sum = 0;

    for (int i = 0; i < 16; i += 2) {
        sum += (uint32_t)((source[i] << 8) | source[i + 1]);
        sum += (uint32_t)((destination[i] << 8) | destination[i + 1]);
    }

    sum += (payload_length >> 16) & 0xFFFF;
    sum += payload_length & 0xFFFF;
    sum += next_header;

    for (uint32_t i = 0; i + 1 < payload_length; i += 2) {
        sum += (uint32_t)((payload[i] << 8) | payload[i + 1]);
    }
    if (payload_length & 1U) {
        sum += (uint32_t)(payload[payload_length - 1] << 8);
    }

    return net_checksum_words(sum);
}

static void net_cache_arp_entry(const uint8_t ip[4], const uint8_t mac[6]) {
    net_memcpy(net_cached_arp_ip, ip, 4);
    net_memcpy(net_cached_arp_mac, mac, 6);
    net_have_arp_cache = 1;
    kdebug_write("[net] ARP cache updated: ");
    net_debug_write_ip(ip);
    kdebug_write(" -> ");
    net_debug_write_mac(mac);
    kdebug_put('\n');
}

static void net_cache_nd_entry(const uint8_t ip[16], const uint8_t mac[6]) {
    net_memcpy(net_cached_nd_ip, ip, 16);
    net_memcpy(net_cached_nd_mac, mac, 6);
    net_have_nd_cache = 1;
    kdebug_write("[net6] ND cache updated: ");
    net_debug_write_ipv6(ip);
    kdebug_write(" -> ");
    net_debug_write_mac(mac);
    kdebug_put('\n');
}

static int net_ipv6_is_unspecified(const uint8_t ip[16]) {
    static const uint8_t zero[16] = {0};
    return net_memcmp(ip, zero, 16) == 0;
}

static int net_ipv6_is_local(const uint8_t ip[16]) {
    return net_memcmp(ip, net_local_ipv6, 16) == 0;
}

static int net_ipv6_is_on_link(const uint8_t ip[16]) {
    return net_memcmp(ip, net_ipv6_prefix, 8) == 0;
}

static void net_ipv6_multicast_mac_from_solicited(const uint8_t target_ip[16], uint8_t multicast_mac[6]) {
    multicast_mac[0] = 0x33;
    multicast_mac[1] = 0x33;
    multicast_mac[2] = 0xFF;
    multicast_mac[3] = target_ip[13];
    multicast_mac[4] = target_ip[14];
    multicast_mac[5] = target_ip[15];
}

static void net_ipv6_solicited_node_multicast(const uint8_t target_ip[16], uint8_t multicast_ip[16]) {
    multicast_ip[0] = 0xFF;
    multicast_ip[1] = 0x02;
    for (int i = 2; i < 11; i++) {
        multicast_ip[i] = 0;
    }
    multicast_ip[11] = 0x01;
    multicast_ip[12] = 0xFF;
    multicast_ip[13] = target_ip[13];
    multicast_ip[14] = target_ip[14];
    multicast_ip[15] = target_ip[15];
}

static void net_send_ethernet_frame(const uint8_t destination[6], uint16_t ethertype, const uint8_t *payload, uint32_t payload_length) {
    uint8_t frame[1514];
    ethernet_header_t *header = (ethernet_header_t *)frame;
    uint32_t frame_length;

    if (payload_length > sizeof(frame) - sizeof(ethernet_header_t)) {
        kdebug_write_line("[net] Refused to send oversize Ethernet frame.");
        return;
    }

    net_memcpy(header->destination, destination, 6);
    net_memcpy(header->source, net_local_mac, 6);
    header->ethertype = net_swap16(ethertype);
    net_memcpy(frame + sizeof(ethernet_header_t), payload, payload_length);
    frame_length = payload_length + sizeof(ethernet_header_t);
    if (frame_length < 60) {
        net_memset(frame + frame_length, 0, 60 - frame_length);
        frame_length = 60;
    }

    kdebug_write("[net] Sending Ethernet frame type ");
    kdebug_write_hex(ethertype);
    kdebug_write(" length ");
    kdebug_write_dec(frame_length);
    kdebug_put('\n');
    rtl8139_send(frame, frame_length);
}

static void net_send_arp_request(const uint8_t target_ip[4]) {
    uint8_t packet_buffer[sizeof(arp_packet_t)];
    arp_packet_t *packet = (arp_packet_t *)packet_buffer;
    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    packet->hardware_type = net_swap16(NET_ARP_HTYPE_ETHERNET);
    packet->protocol_type = net_swap16(NET_ETHERTYPE_IPV4);
    packet->hardware_size = 6;
    packet->protocol_size = 4;
    packet->operation = net_swap16(NET_ARP_OPERATION_REQUEST);
    net_memcpy(packet->sender_mac, net_local_mac, 6);
    net_memcpy(packet->sender_ip, net_local_ip, 4);
    net_memset(packet->target_mac, 0, 6);
    net_memcpy(packet->target_ip, target_ip, 4);

    kdebug_write("[net] Sending ARP request for ");
    net_debug_write_ip(target_ip);
    kdebug_put('\n');
    net_send_ethernet_frame(broadcast_mac, NET_ETHERTYPE_ARP, packet_buffer, sizeof(packet_buffer));
}

static void net_send_arp_reply(const uint8_t target_mac[6], const uint8_t target_ip[4]) {
    uint8_t packet_buffer[sizeof(arp_packet_t)];
    arp_packet_t *packet = (arp_packet_t *)packet_buffer;

    packet->hardware_type = net_swap16(NET_ARP_HTYPE_ETHERNET);
    packet->protocol_type = net_swap16(NET_ETHERTYPE_IPV4);
    packet->hardware_size = 6;
    packet->protocol_size = 4;
    packet->operation = net_swap16(NET_ARP_OPERATION_REPLY);
    net_memcpy(packet->sender_mac, net_local_mac, 6);
    net_memcpy(packet->sender_ip, net_local_ip, 4);
    net_memcpy(packet->target_mac, target_mac, 6);
    net_memcpy(packet->target_ip, target_ip, 4);

    kdebug_write("[net] Sending ARP reply to ");
    net_debug_write_ip(target_ip);
    kdebug_put('\n');
    net_send_ethernet_frame(target_mac, NET_ETHERTYPE_ARP, packet_buffer, sizeof(packet_buffer));
}

static void net_send_icmp_echo(const uint8_t target_mac[6], const uint8_t target_ip[4], uint8_t icmp_type, uint16_t identifier, uint16_t sequence) {
    static const uint8_t payload[] = "kernel-icmp-echo";
    uint8_t packet_buffer[NET_IP_HEADER_SIZE + NET_ICMP_HEADER_SIZE + sizeof(payload)];
    ipv4_header_t *ip = (ipv4_header_t *)packet_buffer;
    icmp_echo_header_t *icmp = (icmp_echo_header_t *)(packet_buffer + NET_IP_HEADER_SIZE);
    uint8_t *icmp_payload = packet_buffer + NET_IP_HEADER_SIZE + NET_ICMP_HEADER_SIZE;
    uint32_t icmp_length = NET_ICMP_HEADER_SIZE + sizeof(payload);

    ip->version_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_length = net_swap16((uint16_t)(NET_IP_HEADER_SIZE + icmp_length));
    ip->identification = net_swap16(net_next_ip_identification++);
    ip->flags_fragment = net_swap16(0x4000);
    ip->ttl = 64;
    ip->protocol = NET_IP_PROTOCOL_ICMP;
    ip->header_checksum = 0;
    net_memcpy(ip->source_ip, net_local_ip, 4);
    net_memcpy(ip->destination_ip, target_ip, 4);
    ip->header_checksum = net_swap16(net_checksum((const uint8_t *)ip, NET_IP_HEADER_SIZE));

    icmp->type = icmp_type;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->identifier = net_swap16(identifier);
    icmp->sequence = net_swap16(sequence);
    net_memcpy(icmp_payload, payload, sizeof(payload));
    icmp->checksum = net_swap16(net_checksum((const uint8_t *)icmp, icmp_length));

    kdebug_write("[net] Sending ICMP packet to ");
    net_debug_write_ip(target_ip);
    kdebug_write(" seq ");
    kdebug_write_dec(sequence);
    kdebug_put('\n');
    net_send_ethernet_frame(target_mac, NET_ETHERTYPE_IPV4, packet_buffer, NET_IP_HEADER_SIZE + icmp_length);
}

static void net_send_icmpv6_echo(const uint8_t target_mac[6], const uint8_t target_ip[16],
                                 uint8_t icmp_type, uint16_t identifier, uint16_t sequence) {
    static const uint8_t payload[] = "kernel-icmpv6-echo";
    uint8_t packet_buffer[NET_IPV6_HEADER_SIZE + NET_ICMP_HEADER_SIZE + sizeof(payload)];
    ipv6_header_t *ip = (ipv6_header_t *)packet_buffer;
    icmp_echo_header_t *icmp = (icmp_echo_header_t *)(packet_buffer + NET_IPV6_HEADER_SIZE);
    uint8_t *icmp_payload = packet_buffer + NET_IPV6_HEADER_SIZE + NET_ICMP_HEADER_SIZE;
    uint32_t icmp_length = NET_ICMP_HEADER_SIZE + sizeof(payload);

    ip->version_traffic_flow = 0x00000060;
    ip->payload_length = net_swap16((uint16_t)icmp_length);
    ip->next_header = NET_IP_PROTOCOL_ICMPV6;
    ip->hop_limit = 64;
    net_memcpy(ip->source_ip, net_local_ipv6, 16);
    net_memcpy(ip->destination_ip, target_ip, 16);

    icmp->type = icmp_type;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->identifier = net_swap16(identifier);
    icmp->sequence = net_swap16(sequence);
    net_memcpy(icmp_payload, payload, sizeof(payload));
    icmp->checksum = net_swap16(net_checksum_ipv6_pseudo(net_local_ipv6, target_ip,
                                                         NET_IP_PROTOCOL_ICMPV6,
                                                         (const uint8_t *)icmp, icmp_length));

    kdebug_write("[net6] Sending ICMPv6 packet to ");
    net_debug_write_ipv6(target_ip);
    kdebug_write(" seq ");
    kdebug_write_dec(sequence);
    kdebug_put('\n');
    net_send_ethernet_frame(target_mac, NET_ETHERTYPE_IPV6, packet_buffer, NET_IPV6_HEADER_SIZE + icmp_length);
}

static void net_send_neighbor_solicitation(const uint8_t target_ip[16]) {
    uint8_t packet_buffer[NET_IPV6_HEADER_SIZE + sizeof(icmpv6_neighbor_message_t) + 8];
    uint8_t multicast_mac[6];
    uint8_t multicast_ip[16];
    ipv6_header_t *ip = (ipv6_header_t *)packet_buffer;
    icmpv6_neighbor_message_t *nd =
        (icmpv6_neighbor_message_t *)(packet_buffer + NET_IPV6_HEADER_SIZE);
    uint8_t *option = packet_buffer + NET_IPV6_HEADER_SIZE + sizeof(icmpv6_neighbor_message_t);
    uint32_t payload_length = sizeof(icmpv6_neighbor_message_t) + 8;

    net_ipv6_multicast_mac_from_solicited(target_ip, multicast_mac);
    net_ipv6_solicited_node_multicast(target_ip, multicast_ip);

    ip->version_traffic_flow = 0x00000060;
    ip->payload_length = net_swap16((uint16_t)payload_length);
    ip->next_header = NET_IP_PROTOCOL_ICMPV6;
    ip->hop_limit = 255;
    net_memcpy(ip->source_ip, net_local_ipv6, 16);
    net_memcpy(ip->destination_ip, multicast_ip, 16);

    nd->type = NET_ICMPV6_TYPE_NEIGHBOR_SOLICITATION;
    nd->code = 0;
    nd->checksum = 0;
    nd->flags_or_reserved = 0;
    net_memcpy(nd->target_address, target_ip, 16);

    option[0] = NET_ICMPV6_ND_OPTION_SOURCE_LLA;
    option[1] = 1;
    net_memcpy(option + 2, net_local_mac, 6);

    nd->checksum = net_swap16(net_checksum_ipv6_pseudo(net_local_ipv6, multicast_ip,
                                                       NET_IP_PROTOCOL_ICMPV6,
                                                       (const uint8_t *)nd, payload_length));

    kdebug_write("[net6] Sending Neighbor Solicitation for ");
    net_debug_write_ipv6(target_ip);
    kdebug_put('\n');
    net_send_ethernet_frame(multicast_mac, NET_ETHERTYPE_IPV6, packet_buffer,
                            NET_IPV6_HEADER_SIZE + payload_length);
}

static void net_send_neighbor_advertisement(const uint8_t target_mac[6], const uint8_t target_ip[16]) {
    uint8_t packet_buffer[NET_IPV6_HEADER_SIZE + sizeof(icmpv6_neighbor_message_t) + 8];
    ipv6_header_t *ip = (ipv6_header_t *)packet_buffer;
    icmpv6_neighbor_message_t *nd =
        (icmpv6_neighbor_message_t *)(packet_buffer + NET_IPV6_HEADER_SIZE);
    uint8_t *option = packet_buffer + NET_IPV6_HEADER_SIZE + sizeof(icmpv6_neighbor_message_t);
    uint32_t payload_length = sizeof(icmpv6_neighbor_message_t) + 8;

    ip->version_traffic_flow = 0x00000060;
    ip->payload_length = net_swap16((uint16_t)payload_length);
    ip->next_header = NET_IP_PROTOCOL_ICMPV6;
    ip->hop_limit = 255;
    net_memcpy(ip->source_ip, net_local_ipv6, 16);
    net_memcpy(ip->destination_ip, target_ip, 16);

    nd->type = NET_ICMPV6_TYPE_NEIGHBOR_ADVERTISEMENT;
    nd->code = 0;
    nd->checksum = 0;
    nd->flags_or_reserved = 0x60000000;
    net_memcpy(nd->target_address, net_local_ipv6, 16);

    option[0] = NET_ICMPV6_ND_OPTION_TARGET_LLA;
    option[1] = 1;
    net_memcpy(option + 2, net_local_mac, 6);

    nd->checksum = net_swap16(net_checksum_ipv6_pseudo(net_local_ipv6, target_ip,
                                                       NET_IP_PROTOCOL_ICMPV6,
                                                       (const uint8_t *)nd, payload_length));

    kdebug_write("[net6] Sending Neighbor Advertisement to ");
    net_debug_write_ipv6(target_ip);
    kdebug_put('\n');
    net_send_ethernet_frame(target_mac, NET_ETHERTYPE_IPV6, packet_buffer,
                            NET_IPV6_HEADER_SIZE + payload_length);
}

int net_init(void) {
    if (!rtl8139_init()) {
        kdebug_write_line("[net] Network stack offline because rtl8139 initialization failed.");
        return 0;
    }

    net_memcpy(net_local_mac, rtl8139_get_mac(), 6);
    kdebug_write("[net] Static IPv4 address configured as ");
    net_debug_write_ip(net_local_ip);
    kdebug_write(" / ");
    net_debug_write_ip(net_netmask);
    kdebug_write(" via gateway ");
    net_debug_write_ip(net_ipv4_gateway);
    kdebug_put('\n');
    kdebug_write("[net6] Static IPv6 address configured as ");
    net_debug_write_ipv6(net_local_ipv6);
    kdebug_write(" with gateway ");
    net_debug_write_ipv6(net_ipv6_gateway);
    kdebug_write(" and DNS ");
    net_debug_write_ipv6(net_ipv6_dns);
    kdebug_put('\n');
    kdebug_write_line("[net] Ethernet, ARP, IPv4, and ICMP layers are ready for polling.");
    return 1;
}

void net_poll(void) {
    rtl8139_poll();
}

int net_ping(const char *target_ip_string) {
    uint8_t target_ip[4];
    const uint8_t *next_hop_ip;
    uint32_t start_tick;
    uint32_t retry_tick;
    static const uint8_t unspecified_ip[4] = {0, 0, 0, 0};

    if (!rtl8139_is_ready()) {
        kdebug_write_line("[net] ping refused because NIC is not ready.");
        return 0;
    }

    if (!net_parse_ip(target_ip_string, target_ip)) {
        kdebug_write("[net] Invalid IPv4 address: ");
        kdebug_write_line(target_ip_string ? target_ip_string : "(null)");
        return 0;
    }

    if (net_memcmp(target_ip, unspecified_ip, 4) == 0) {
        kdebug_write_line("[net] Refusing to ping 0.0.0.0 because it is the unspecified IPv4 address.");
        return 0;
    }

    if (net_memcmp(target_ip, net_local_ip, 4) == 0) {
        kdebug_write_line("[net] Ping target matches the local NIC IPv4 address. Treating as local success.");
        return 1;
    }

    next_hop_ip = net_ipv4_is_on_link(target_ip) ? target_ip : net_ipv4_gateway;

    if (!net_have_arp_cache || net_memcmp(net_cached_arp_ip, next_hop_ip, 4) != 0) {
        kdebug_write("[net] Resolving next-hop IPv4 address ");
        net_debug_write_ip(next_hop_ip);
        if (next_hop_ip != target_ip) {
            kdebug_write(" for remote target ");
            net_debug_write_ip(target_ip);
        }
        kdebug_put('\n');
        net_send_arp_request(next_hop_ip);
        start_tick = timer_get_ticks();
        retry_tick = start_tick + 100;
        while (timer_get_ticks() - start_tick < NET_PING_TIMEOUT_TICKS) {
            net_poll();
            if (net_have_arp_cache && net_memcmp(net_cached_arp_ip, next_hop_ip, 4) == 0) {
                break;
            }
            if (timer_get_ticks() >= retry_tick) {
                net_send_arp_request(next_hop_ip);
                retry_tick += 100;
            }
        }
    }

    if (!net_have_arp_cache || net_memcmp(net_cached_arp_ip, next_hop_ip, 4) != 0) {
        kdebug_write("[net] ARP resolution failed for next-hop ");
        net_debug_write_ip(next_hop_ip);
        if (next_hop_ip != target_ip) {
            kdebug_write(" while targeting ");
            net_debug_write_ip(target_ip);
        }
        kdebug_put('\n');
        return 0;
    }

    net_active_ping_sequence = net_next_ping_sequence++;
    net_ping_reply_received = 0;
    net_waiting_for_ping_reply = 1;
    net_ping_start_tick = timer_get_ticks();
    net_send_icmp_echo(net_cached_arp_mac, target_ip, NET_ICMP_TYPE_ECHO_REQUEST, net_active_ping_identifier, net_active_ping_sequence);

    while (timer_get_ticks() - net_ping_start_tick < NET_PING_TIMEOUT_TICKS) {
        net_poll();
        if (net_ping_reply_received) {
            return 1;
        }
    }

    net_waiting_for_ping_reply = 0;
    kdebug_write("[net] ping timed out waiting for ");
    net_debug_write_ip(target_ip);
    kdebug_put('\n');
    return 0;
}

int net_ping6(const char *target_ip_string) {
    uint8_t target_ip[16];
    const uint8_t *next_hop_ip;
    uint32_t start_tick;
    uint32_t retry_tick;

    if (!rtl8139_is_ready()) {
        kdebug_write_line("[net6] ping6 refused because NIC is not ready.");
        return 0;
    }

    if (!net_parse_ipv6(target_ip_string, target_ip)) {
        kdebug_write("[net6] Invalid IPv6 address: ");
        kdebug_write_line(target_ip_string ? target_ip_string : "(null)");
        return 0;
    }

    if (net_ipv6_is_unspecified(target_ip)) {
        kdebug_write_line("[net6] Refusing to ping :: because it is the unspecified IPv6 address.");
        return 0;
    }

    if (net_ipv6_is_local(target_ip)) {
        kdebug_write_line("[net6] Ping6 target matches the local NIC IPv6 address. Treating as local success.");
        return 1;
    }

    next_hop_ip = net_ipv6_is_on_link(target_ip) ? target_ip : net_ipv6_gateway;

    if (!net_have_nd_cache || net_memcmp(net_cached_nd_ip, next_hop_ip, 16) != 0) {
        net_send_neighbor_solicitation(next_hop_ip);
        start_tick = timer_get_ticks();
        retry_tick = start_tick + 100;
        while (timer_get_ticks() - start_tick < NET_PING_TIMEOUT_TICKS) {
            net_poll();
            if (net_have_nd_cache && net_memcmp(net_cached_nd_ip, next_hop_ip, 16) == 0) {
                break;
            }
            if (timer_get_ticks() >= retry_tick) {
                net_send_neighbor_solicitation(next_hop_ip);
                retry_tick += 100;
            }
        }
    }

    if (!net_have_nd_cache || net_memcmp(net_cached_nd_ip, next_hop_ip, 16) != 0) {
        kdebug_write("[net6] Neighbor discovery failed for ");
        net_debug_write_ipv6(next_hop_ip);
        kdebug_put('\n');
        return 0;
    }

    net_active_ping6_sequence = net_next_ping6_sequence++;
    net_ping6_reply_received = 0;
    net_waiting_for_ping6_reply = 1;
    net_memcpy(net_active_ping6_target, target_ip, 16);
    net_ping_start_tick = timer_get_ticks();
    net_send_icmpv6_echo(net_cached_nd_mac, target_ip, NET_ICMPV6_TYPE_ECHO_REQUEST,
                         net_active_ping6_identifier, net_active_ping6_sequence);

    while (timer_get_ticks() - net_ping_start_tick < NET_PING_TIMEOUT_TICKS) {
        net_poll();
        if (net_ping6_reply_received) {
            return 1;
        }
    }

    net_waiting_for_ping6_reply = 0;
    kdebug_write("[net6] ping6 timed out waiting for ");
    net_debug_write_ipv6(target_ip);
    kdebug_put('\n');
    return 0;
}

void net_handle_frame(const uint8_t *frame, uint32_t length) {
    const ethernet_header_t *ethernet;
    uint16_t ethertype;

    if (frame == 0 || length < sizeof(ethernet_header_t)) {
        return;
    }

    ethernet = (const ethernet_header_t *)frame;
    ethertype = net_swap16(ethernet->ethertype);

    kdebug_write("[net] RX Ethernet frame type ");
    kdebug_write_hex(ethertype);
    kdebug_write(" length ");
    kdebug_write_dec(length);
    kdebug_put('\n');

    if (ethertype == NET_ETHERTYPE_ARP) {
        const arp_packet_t *arp;
        uint16_t operation;

        if (length < NET_ETHERNET_HEADER_SIZE + sizeof(arp_packet_t)) {
            return;
        }

        arp = (const arp_packet_t *)(frame + NET_ETHERNET_HEADER_SIZE);
        operation = net_swap16(arp->operation);

        if (operation == NET_ARP_OPERATION_REPLY && net_memcmp(arp->target_ip, net_local_ip, 4) == 0) {
            kdebug_write("[net] Received ARP reply from ");
            net_debug_write_ip(arp->sender_ip);
            kdebug_put('\n');
            net_cache_arp_entry(arp->sender_ip, arp->sender_mac);
        } else if (operation == NET_ARP_OPERATION_REQUEST && net_memcmp(arp->target_ip, net_local_ip, 4) == 0) {
            kdebug_write("[net] Received ARP request from ");
            net_debug_write_ip(arp->sender_ip);
            kdebug_put('\n');
            net_cache_arp_entry(arp->sender_ip, arp->sender_mac);
            net_send_arp_reply(arp->sender_mac, arp->sender_ip);
        }
        return;
    }

    if (ethertype == NET_ETHERTYPE_IPV4) {
        const ipv4_header_t *ip;
        uint8_t ihl_bytes;
        uint16_t total_length;

        if (length < NET_ETHERNET_HEADER_SIZE + sizeof(ipv4_header_t)) {
            return;
        }

        ip = (const ipv4_header_t *)(frame + NET_ETHERNET_HEADER_SIZE);
        ihl_bytes = (uint8_t)((ip->version_ihl & 0x0F) * 4);
        total_length = net_swap16(ip->total_length);
        if (ihl_bytes < NET_IP_HEADER_SIZE || total_length < ihl_bytes ||
            length < (uint32_t)(NET_ETHERNET_HEADER_SIZE + total_length)) {
            return;
        }
        if (net_memcmp(ip->destination_ip, net_local_ip, 4) != 0) {
            return;
        }

        if (ip->protocol == NET_IP_PROTOCOL_ICMP) {
            const icmp_echo_header_t *icmp;
            uint32_t icmp_length = total_length - ihl_bytes;

            if (icmp_length < sizeof(icmp_echo_header_t)) {
                return;
            }

            icmp = (const icmp_echo_header_t *)(frame + NET_ETHERNET_HEADER_SIZE + ihl_bytes);
            if (icmp->type == NET_ICMP_TYPE_ECHO_REPLY &&
                net_waiting_for_ping_reply &&
                net_swap16(icmp->identifier) == net_active_ping_identifier &&
                net_swap16(icmp->sequence) == net_active_ping_sequence) {
                net_ping_reply_received = 1;
                net_waiting_for_ping_reply = 0;
                kdebug_write("[net] Received ICMP echo reply from ");
                net_debug_write_ip(ip->source_ip);
                kdebug_write(" in ticks ");
                kdebug_write_dec(timer_get_ticks() - net_ping_start_tick);
                kdebug_put('\n');
            } else if (icmp->type == NET_ICMP_TYPE_ECHO_REQUEST) {
                kdebug_write("[net] Received ICMP echo request from ");
                net_debug_write_ip(ip->source_ip);
                kdebug_put('\n');
                net_cache_arp_entry(ip->source_ip, ethernet->source);
                net_send_icmp_echo(ethernet->source, ip->source_ip, NET_ICMP_TYPE_ECHO_REPLY,
                                   net_swap16(icmp->identifier), net_swap16(icmp->sequence));
            }
        }
    }

    if (ethertype == NET_ETHERTYPE_IPV6) {
        const ipv6_header_t *ip6;
        uint16_t payload_length;

        if (length < NET_ETHERNET_HEADER_SIZE + sizeof(ipv6_header_t)) {
            return;
        }

        ip6 = (const ipv6_header_t *)(frame + NET_ETHERNET_HEADER_SIZE);
        payload_length = net_swap16(ip6->payload_length);
        if (length < (uint32_t)(NET_ETHERNET_HEADER_SIZE + NET_IPV6_HEADER_SIZE + payload_length)) {
            return;
        }
        if (net_memcmp(ip6->destination_ip, net_local_ipv6, 16) != 0) {
            return;
        }

        if (ip6->next_header == NET_IP_PROTOCOL_ICMPV6) {
            const uint8_t *icmp_payload = frame + NET_ETHERNET_HEADER_SIZE + NET_IPV6_HEADER_SIZE;
            uint8_t icmp_type = icmp_payload[0];

            if (icmp_type == NET_ICMPV6_TYPE_ECHO_REPLY) {
                const icmp_echo_header_t *icmp = (const icmp_echo_header_t *)icmp_payload;
                if (net_waiting_for_ping6_reply &&
                    net_swap16(icmp->identifier) == net_active_ping6_identifier &&
                    net_swap16(icmp->sequence) == net_active_ping6_sequence &&
                    net_memcmp(ip6->source_ip, net_active_ping6_target, 16) == 0) {
                    net_ping6_reply_received = 1;
                    net_waiting_for_ping6_reply = 0;
                    kdebug_write("[net6] Received ICMPv6 echo reply from ");
                    net_debug_write_ipv6(ip6->source_ip);
                    kdebug_write(" in ticks ");
                    kdebug_write_dec(timer_get_ticks() - net_ping_start_tick);
                    kdebug_put('\n');
                }
            } else if (icmp_type == NET_ICMPV6_TYPE_ECHO_REQUEST) {
                const icmp_echo_header_t *icmp = (const icmp_echo_header_t *)icmp_payload;
                kdebug_write("[net6] Received ICMPv6 echo request from ");
                net_debug_write_ipv6(ip6->source_ip);
                kdebug_put('\n');
                net_cache_nd_entry(ip6->source_ip, ethernet->source);
                net_send_icmpv6_echo(ethernet->source, ip6->source_ip, NET_ICMPV6_TYPE_ECHO_REPLY,
                                     net_swap16(icmp->identifier), net_swap16(icmp->sequence));
            } else if (icmp_type == NET_ICMPV6_TYPE_NEIGHBOR_ADVERTISEMENT) {
                const icmpv6_neighbor_message_t *nd = (const icmpv6_neighbor_message_t *)icmp_payload;
                kdebug_write("[net6] Received Neighbor Advertisement for ");
                net_debug_write_ipv6(nd->target_address);
                kdebug_put('\n');
                net_cache_nd_entry(nd->target_address, ethernet->source);
            } else if (icmp_type == NET_ICMPV6_TYPE_NEIGHBOR_SOLICITATION) {
                const icmpv6_neighbor_message_t *nd = (const icmpv6_neighbor_message_t *)icmp_payload;
                if (net_memcmp(nd->target_address, net_local_ipv6, 16) == 0) {
                    kdebug_write("[net6] Received Neighbor Solicitation from ");
                    net_debug_write_ipv6(ip6->source_ip);
                    kdebug_put('\n');
                    net_cache_nd_entry(ip6->source_ip, ethernet->source);
                    net_send_neighbor_advertisement(ethernet->source, ip6->source_ip);
                }
            }
        }
    }
}

const uint8_t* net_get_local_mac(void) {
    return net_local_mac;
}

const uint8_t* net_get_local_ip(void) {
    return net_local_ip;
}

const uint8_t* net_get_local_ipv6(void) {
    return net_local_ipv6;
}
