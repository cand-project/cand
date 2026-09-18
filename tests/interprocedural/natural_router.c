/*
 * Tiny packet-router batch simulator.
 *
 * This is an ordinary single-translation-unit C program: packets have a
 * create/destroy lifecycle, are routed using borrowed views, and are scored
 * into caller-owned metrics. It intentionally keeps packets in a fixed batch
 * so the demo does not depend on a custom allocator or hidden global owner.
 */
#include <stddef.h>
#include <stdlib.h>
#include "cand/cand.h"

#define ROUTER_BATCH_CAPACITY 12
#define PACKET_PAYLOAD_CAPACITY 80
#define ROUTE_COUNT 4

typedef struct EthernetHeader {
    unsigned char destination[6];
    unsigned char source[6];
    unsigned short ether_type;
} EthernetHeader;

typedef struct IPv4Header {
    unsigned char version;
    unsigned char ttl;
    unsigned char protocol;
    unsigned int source;
    unsigned int destination;
} IPv4Header;

typedef struct TcpHeader {
    unsigned short source_port;
    unsigned short destination_port;
    unsigned int sequence;
    unsigned int acknowledgement;
    unsigned char flags;
} TcpHeader;

typedef struct PayloadView {
    const unsigned char *bytes;
    size_t length;
} PayloadView;

typedef struct Packet {
    unsigned id;
    unsigned route;
    unsigned retry_count;
    size_t length;
    unsigned char payload[PACKET_PAYLOAD_CAPACITY];
    EthernetHeader ethernet;
    IPv4Header ipv4;
    TcpHeader tcp;
    PayloadView payload_view;
} Packet;

typedef struct RouteRule {
    unsigned route;
    unsigned minimum_length;
    unsigned maximum_length;
    unsigned retry_limit;
    unsigned enabled;
} RouteRule;

typedef struct RouterMetrics {
    unsigned received;
    unsigned accepted;
    unsigned rejected;
    unsigned retries;
    unsigned checksum;
    unsigned by_route[ROUTE_COUNT];
} RouterMetrics;

typedef enum RouteDecision {
    ROUTE_DROP = 0,
    ROUTE_ACCEPT = 1,
    ROUTE_RETRY = 2
} RouteDecision;

static void packet_initialize_protocol_headers(Packet *packet,
                                                unsigned route,
                                                unsigned id);

static const RouteRule DEFAULT_RULES[ROUTE_COUNT] = {
    {0u, 1u, PACKET_PAYLOAD_CAPACITY, 2u, 1u},
    {1u, 2u, 64u, 3u, 1u},
    {2u, 4u, 48u, 1u, 1u},
    {3u, 1u, 32u, 0u, 1u}
};

static unsigned clamp_route(unsigned route)
{
    if (route >= ROUTE_COUNT) {
        return ROUTE_COUNT - 1u;
    }
    return route;
}

static unsigned byte_mix(unsigned hash, unsigned char value)
{
    hash ^= (unsigned)value;
    hash *= 16777619u;
    hash ^= hash >> 13;
    return hash;
}

static unsigned payload_hash(const unsigned char *bytes, size_t length)
{
    unsigned hash = 2166136261u;
    size_t index;

    if (bytes == NULL) {
        return 0u;
    }
    for (index = 0; index < length; ++index) {
        hash = byte_mix(hash, bytes[index]);
    }
    return hash;
}

CAND_RETURNS_OWN Packet *packet_create(unsigned id, unsigned route,
                                       const unsigned char *payload, size_t length)
{
    Packet *packet = (Packet *)malloc(sizeof(*packet));
    size_t index;

    if (payload == NULL || length == 0u ||
        length > PACKET_PAYLOAD_CAPACITY) {
        abort();
    }
    if (packet == NULL) {
        abort();
    }
    packet->id = id;
    packet->route = clamp_route(route);
    packet->retry_count = 0u;
    packet->length = length;
    for (index = 0; index < length; ++index) {
        packet->payload[index] = payload[index];
    }
    for (; index < PACKET_PAYLOAD_CAPACITY; ++index) {
        packet->payload[index] = 0u;
    }
    packet_initialize_protocol_headers(packet, route, id);
    return packet;
}

static void packet_destroy(Packet *packet CAND_DESTROYS)
{
    free(packet);
}

/* The packet owns these protocol records; the accessors expose views only. */
CAND_RETURNS_BORROW_FROM(0) EthernetHeader *packet_ethernet(Packet *packet)
{
    return &packet->ethernet;
}

CAND_RETURNS_BORROW_FROM(0) IPv4Header *packet_ipv4(Packet *packet)
{
    return &packet->ipv4;
}

CAND_RETURNS_BORROW_FROM(0) TcpHeader *packet_tcp(Packet *packet)
{
    return &packet->tcp;
}

CAND_RETURNS_BORROW_FROM(0) PayloadView *packet_payload(Packet *packet)
{
    return &packet->payload_view;
}

static void packet_initialize_protocol_headers(Packet *packet,
                                                unsigned route,
                                                unsigned id)
{
    size_t index;

    packet->ethernet.ether_type = 0x0800u;
    for (index = 0; index < 6u; ++index) {
        packet->ethernet.destination[index] =
            (unsigned char)(route * 7u + index);
        packet->ethernet.source[index] =
            (unsigned char)(id + index * 3u);
    }
    packet->ipv4.version = 4u;
    packet->ipv4.ttl = (unsigned char)(32u + route * 8u);
    packet->ipv4.protocol = 6u;
    packet->ipv4.source = 0x0a000001u + id;
    packet->ipv4.destination = 0xc0000201u + route;
    packet->tcp.source_port = (unsigned short)(10000u + id % 1000u);
    packet->tcp.destination_port = (unsigned short)(80u + route);
    packet->tcp.sequence = id * 17u;
    packet->tcp.acknowledgement = id * 19u;
    packet->tcp.flags = 0x18u;
    packet->payload_view.bytes = packet->payload;
    packet->payload_view.length = packet->length;
}

static unsigned ethernet_view_score(const EthernetHeader *header CAND_BORROW)
{
    unsigned score = header->ether_type;
    size_t index;

    for (index = 0; index < 6u; ++index) {
        score ^= (unsigned)header->destination[index] << (index % 4u);
        score += header->source[index];
    }
    return score;
}

static unsigned ipv4_view_score(const IPv4Header *header CAND_BORROW)
{
    if (header->version != 4u || header->ttl == 0u) {
        return 0u;
    }
    return header->source ^ header->destination ^
           ((unsigned)header->protocol << 8u) ^ header->ttl;
}

static unsigned tcp_view_score(const TcpHeader *header CAND_BORROW)
{
    return (unsigned)header->source_port +
           (unsigned)header->destination_port +
           header->sequence + header->acknowledgement + header->flags;
}

static unsigned payload_view_score(const PayloadView *view CAND_BORROW)
{
    unsigned score = 0u;
    size_t index;

    if (view->bytes == NULL) {
        return 0u;
    }
    for (index = 0; index < view->length; ++index) {
        score = (score << 5u) ^ view->bytes[index] ^ (score >> 2u);
    }
    return score;
}

static unsigned inspect_packet_views(const EthernetHeader *ethernet CAND_BORROW,
                                     const IPv4Header *ipv4 CAND_BORROW,
                                     const TcpHeader *tcp CAND_BORROW,
                                     const PayloadView *payload CAND_BORROW)
{
    if (ethernet == NULL || ipv4 == NULL || tcp == NULL || payload == NULL) {
        return 0u;
    }
    return ethernet_view_score(ethernet) ^ ipv4_view_score(ipv4) ^
           tcp_view_score(tcp) ^ payload_view_score(payload);
}

static int packet_views_are_consistent(const IPv4Header *ipv4 CAND_BORROW,
                                       const PayloadView *payload CAND_BORROW)
{
    return ipv4 != NULL && ipv4->version == 4u &&
           payload != NULL && payload->bytes != NULL && payload->length != 0u;
}

static void decrement_packet_ttl(IPv4Header *ipv4 CAND_BORROW_MUT)
{
    if (ipv4 != NULL && ipv4->ttl > 0u) {
        --ipv4->ttl;
    }
}

static unsigned route_protocol_views(const EthernetHeader *ethernet CAND_BORROW,
                                     const IPv4Header *ipv4 CAND_BORROW,
                                     const TcpHeader *tcp CAND_BORROW,
                                     const PayloadView *payload CAND_BORROW,
                                     unsigned route, unsigned retry_count)
{
    unsigned score = inspect_packet_views(ethernet, ipv4, tcp, payload);

    if (!packet_views_are_consistent(ipv4, payload)) {
        return 0u;
    }
    return score ^ route ^ retry_count;
}

static int run_protocol_view_demo(void)
{
    static const unsigned char payload[] = {9u, 2u, 6u, 5u, 3u, 5u};
    Packet *packet CAND_OWN = packet_create(909u, 1u, payload, sizeof(payload));
    unsigned score;

    if (packet == NULL) {
        return 0;
    }
    {
        EthernetHeader *ethernet CAND_BORROW = packet_ethernet(packet);
        IPv4Header *ip CAND_BORROW = packet_ipv4(packet);
        TcpHeader *tcp CAND_BORROW = packet_tcp(packet);
        PayloadView *view CAND_BORROW = packet_payload(packet);

        score = route_protocol_views(ethernet, ip, tcp, view,
                                     packet->route, packet->retry_count);
        if (ip == NULL || view == NULL || score == 0u || ip->ttl == 0u) {
            packet_destroy(packet);
            return 0;
        }
    }

    {
        IPv4Header *edit CAND_BORROW_MUT = packet_ipv4(packet);
        decrement_packet_ttl(edit);
        if (edit == NULL || edit->ttl == 0u) {
            packet_destroy(packet);
            return 0;
        }
    }
    if (score == 0u) {
        packet_destroy(packet);
        return 0;
    }
    packet_destroy(packet);
    return 1;
}

static int packet_has_valid_shape(const Packet *packet)
{
    if (packet == NULL) {
        return 0;
    }
    if (packet->length == 0u ||
        packet->length > PACKET_PAYLOAD_CAPACITY) {
        return 0;
    }
    if (packet->route >= ROUTE_COUNT) {
        return 0;
    }
    return 1;
}

static unsigned packet_checksum(const Packet *packet)
{
    if (!packet_has_valid_shape(packet)) {
        return 0u;
    }
    return payload_hash(packet->payload, packet->length) ^ packet->id;
}

static const RouteRule *find_rule(const RouteRule *rules, size_t count,
                                  unsigned route)
{
    size_t index;

    if (rules == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        if (rules[index].route == route) {
            return &rules[index];
        }
    }
    return NULL;
}

static int rule_accepts(const RouteRule *rule, const Packet *packet)
{
    if (rule == NULL || !packet_has_valid_shape(packet)) {
        return 0;
    }
    if (!rule->enabled || packet->route != rule->route) {
        return 0;
    }
    if (packet->length < rule->minimum_length ||
        packet->length > rule->maximum_length) {
        return 0;
    }
    return 1;
}

static RouteDecision classify_packet(const RouteRule *rules,
                                     size_t rule_count,
                                     const Packet *packet)
{
    const RouteRule *rule;

    if (!packet_has_valid_shape(packet)) {
        return ROUTE_DROP;
    }
    rule = find_rule(rules, rule_count, packet->route);
    if (rule_accepts(rule, packet)) {
        return ROUTE_ACCEPT;
    }
    if (rule != NULL && packet->retry_count < rule->retry_limit) {
        return ROUTE_RETRY;
    }
    return ROUTE_DROP;
}

static void metrics_init(RouterMetrics *metrics)
{
    size_t index;

    metrics->received = 0u;
    metrics->accepted = 0u;
    metrics->rejected = 0u;
    metrics->retries = 0u;
    metrics->checksum = 0u;
    for (index = 0; index < ROUTE_COUNT; ++index) {
        metrics->by_route[index] = 0u;
    }
}

static void metrics_record(RouterMetrics *metrics, const Packet *packet,
                           RouteDecision decision)
{
    unsigned route;

    ++metrics->received;
    if (packet == NULL) {
        ++metrics->rejected;
        return;
    }
    route = clamp_route(packet->route);
    ++metrics->by_route[route];
    metrics->checksum ^= packet_checksum(packet);
    if (decision == ROUTE_ACCEPT) {
        ++metrics->accepted;
    } else if (decision == ROUTE_RETRY) {
        ++metrics->retries;
    } else {
        ++metrics->rejected;
    }
}

static void metrics_merge(RouterMetrics *target,
                          const RouterMetrics *source)
{
    size_t index;

    target->received += source->received;
    target->accepted += source->accepted;
    target->rejected += source->rejected;
    target->retries += source->retries;
    target->checksum ^= source->checksum;
    for (index = 0; index < ROUTE_COUNT; ++index) {
        target->by_route[index] += source->by_route[index];
    }
}

static int metrics_are_consistent(const RouterMetrics *metrics)
{
    unsigned routed = 0u;
    size_t index;

    for (index = 0; index < ROUTE_COUNT; ++index) {
        routed += metrics->by_route[index];
    }
    return routed <= metrics->received &&
           metrics->accepted + metrics->rejected <= metrics->received &&
           metrics->retries <= metrics->received;
}

static unsigned route_weight(unsigned route)
{
    static const unsigned weights[ROUTE_COUNT] = {1u, 3u, 5u, 2u};
    return weights[clamp_route(route)];
}

static unsigned packet_priority(const Packet *packet)
{
    unsigned checksum;

    if (!packet_has_valid_shape(packet)) {
        return 0u;
    }
    checksum = packet_checksum(packet);
    return (checksum ^ (packet->id * 33u)) +
           route_weight(packet->route) + packet->retry_count;
}

static int packet_is_duplicate(const Packet *candidate,
                               const Packet *const *batch,
                               size_t count)
{
    size_t index;

    if (!packet_has_valid_shape(candidate) || batch == NULL) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        const Packet *existing = batch[index];
        if (existing != NULL && existing != candidate &&
            existing->id == candidate->id) {
            return 1;
        }
    }
    return 0;
}

static void packet_increment_retry(Packet *packet)
{
    if (packet != NULL && packet->retry_count < 100u) {
        ++packet->retry_count;
    }
}

static void packet_reset_retry(Packet *packet)
{
    if (packet != NULL) {
        packet->retry_count = 0u;
    }
}

static unsigned batch_checksum(const Packet *const *batch, size_t count)
{
    unsigned checksum = 0u;
    size_t index;

    if (batch == NULL) {
        return checksum;
    }
    for (index = 0; index < count; ++index) {
        if (batch[index] != NULL) {
            checksum ^= packet_checksum(batch[index]);
        }
    }
    return checksum;
}

static unsigned batch_priority_total(const Packet *const *batch, size_t count)
{
    unsigned total = 0u;
    size_t index;

    if (batch == NULL) {
        return total;
    }
    for (index = 0; index < count; ++index) {
        if (batch[index] != NULL) {
            total += packet_priority(batch[index]);
        }
    }
    return total;
}

static void clear_batch(Packet **batch, size_t count)
{
    size_t index;

    if (batch == NULL) {
        return;
    }
    for (index = 0; index < count; ++index) {
        packet_destroy(batch[index]);
        batch[index] = NULL;
    }
}

static int run_router_batch(Packet **batch, size_t count,
                            const RouteRule *rules, size_t rule_count,
                            RouterMetrics *metrics)
{
    RouterMetrics batch_metrics;
    size_t index;

    if (batch == NULL || rules == NULL || metrics == NULL ||
        count > ROUTER_BATCH_CAPACITY) {
        return 0;
    }
    metrics_init(&batch_metrics);
    for (index = 0; index < count; ++index) {
        Packet *packet = batch[index];
        RouteDecision decision;

        if (packet == NULL) {
            continue;
        }
        decision = classify_packet(rules, rule_count, packet);
        if (decision == ROUTE_RETRY) {
            packet_increment_retry(packet);
            decision = classify_packet(rules, rule_count, packet);
        }
        metrics_record(&batch_metrics, packet, decision);
        if (decision == ROUTE_ACCEPT) {
            packet_reset_retry(packet);
        }
    }
    metrics_merge(metrics, &batch_metrics);
    return metrics_are_consistent(metrics);
}

static int build_demo_batch(Packet **batch, size_t capacity, size_t *used)
{
    static const unsigned char payload_a[] = {10u, 20u, 30u, 40u};
    static const unsigned char payload_b[] = {3u, 1u, 4u, 1u, 5u, 9u};
    static const unsigned char payload_c[] = {2u, 7u, 1u, 8u, 2u, 8u};
    static const unsigned char payload_d[] = {6u, 2u, 6u, 4u};

    if (batch == NULL || used == NULL || capacity < 4u) {
        return 0;
    }
    *used = 0u;
    batch[0] = packet_create(101u, 0u, payload_a, sizeof(payload_a));
    if (batch[0] == NULL) goto failed;
    ++*used;
    batch[1] = packet_create(202u, 1u, payload_b, sizeof(payload_b));
    if (batch[1] == NULL) goto failed;
    ++*used;
    batch[2] = packet_create(303u, 2u, payload_c, sizeof(payload_c));
    if (batch[2] == NULL) goto failed;
    ++*used;
    batch[3] = packet_create(404u, 3u, payload_d, sizeof(payload_d));
    if (batch[3] == NULL) goto failed;
    ++*used;
    return 1;

failed:
    clear_batch(batch, *used);
    return 0;
}

static int validate_batch(const Packet *const *batch, size_t count)
{
    size_t index;

    if (batch == NULL || count > ROUTER_BATCH_CAPACITY) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        const Packet *packet = batch[index];
        if (packet != NULL && !packet_has_valid_shape(packet)) {
            return 0;
        }
        if (packet != NULL && packet_is_duplicate(packet, batch, count)) {
            return 0;
        }
    }
    return 1;
}

static int run_demo(void)
{
    Packet *batch[ROUTER_BATCH_CAPACITY] = {NULL};
    RouterMetrics metrics;
    size_t used = 0u;
    int result = 1;
    unsigned checksum;
    unsigned priority;

    metrics_init(&metrics);
    if (!run_protocol_view_demo()) {
        return 0;
    }
    if (!build_demo_batch(batch, ROUTER_BATCH_CAPACITY, &used)) {
        return 0;
    }
    if (!validate_batch((const Packet *const *)batch, used)) {
        result = 0;
        goto cleanup;
    }
    checksum = batch_checksum((const Packet *const *)batch, used);
    priority = batch_priority_total((const Packet *const *)batch, used);
    if (checksum == 0u || priority == 0u) {
        result = 0;
        goto cleanup;
    }
    if (!run_router_batch(batch, used, DEFAULT_RULES, ROUTE_COUNT,
                          &metrics)) {
        result = 0;
        goto cleanup;
    }
    if (metrics.received != used || metrics.accepted + metrics.rejected == 0u) {
        result = 0;
        goto cleanup;
    }

cleanup:
    clear_batch(batch, used);
    return result;
}

int main(void)
{
    return run_demo() ? 0 : 1;
}
