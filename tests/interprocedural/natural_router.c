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

#define ROUTER_BATCH_CAPACITY 12
#define PACKET_PAYLOAD_CAPACITY 80
#define ROUTE_COUNT 4

typedef struct Packet {
    unsigned id;
    unsigned route;
    unsigned retry_count;
    size_t length;
    unsigned char payload[PACKET_PAYLOAD_CAPACITY];
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

static Packet *packet_create(unsigned id, unsigned route,
                             const unsigned char *payload, size_t length)
{
    Packet *packet;
    size_t index;

    if (payload == NULL || length == 0u ||
        length > PACKET_PAYLOAD_CAPACITY) {
        return NULL;
    }
    packet = (Packet *)malloc(sizeof(*packet));
    if (packet == NULL) {
        return NULL;
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
    return packet;
}

static void packet_destroy(Packet *packet)
{
    free(packet);
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
