/* BASELINE SENDER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *                  (format: 4-byte big-endian seq + 160-byte payload)
 *   send 47001  -> relay uplink toward the receiver (YOUR wire format)
 *   bind 47004  <- feedback from your receiver, via the relay (optional)
 *
 * This baseline forwards each frame once, unchanged, and ignores feedback.
 * No redundancy, no retransmission. It cannot pass. That is the point.
 *
 * Env vars available if you want them: T0 (epoch seconds, float),
 * DURATION_S, DELAY_MS. The harness kills this process when the run ends,
 * so a forever-loop is fine.
 *
 * build: make        run: python3 run.py --delay_ms 60
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr));

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char in_buf[2048];
    unsigned char out_buf[324];
    
    uint8_t prev1_payload[160] = {0}; // Frame i-1
    uint8_t prev2_payload[160] = {0}; // Frame i-2

    for (;;) {
        ssize_t n = recvfrom(in_fd, in_buf, sizeof(in_buf), 0, NULL, NULL);
        if (n < 164) continue;

        uint32_t seq;
        memcpy(&seq, in_buf, 4);
        seq = ntohl(seq);
        memcpy(out_buf, in_buf, 164); 
        
        int packet_size = 164;
        
        if (seq % 32 != 0) {
            memcpy(out_buf + 164, prev2_payload, 160);
            packet_size = 324;
        }

        sendto(out_fd, out_buf, packet_size, 0, (struct sockaddr *)&relay, sizeof(relay));

        memcpy(prev2_payload, prev1_payload, 160); // Old i-1 becomes new i-2
        memcpy(prev1_payload, in_buf + 4, 160);    // Current frame becomes new i-1
    }
    
    return 0;
}