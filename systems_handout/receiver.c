/* BASELINE RECEIVER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte big-endian seq +
 *                  160-byte payload. Frame i counts only if it arrives
 *                  BEFORE its deadline t0 + DELAY_MS + i*20ms.
 *   send 47003  -> feedback to your sender, via the relay (optional)
 *
 * This baseline forwards whatever arrives straight to the player: lost
 * frames stay lost, late frames stay late, duplicates are re-sent
 * harmlessly. All yours to fix — jitter buffer, reordering, recovery.
 *
 * Env vars available: T0, DURATION_S, DELAY_MS. Harness kills the process
 * at run end; a forever-loop is fine.
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#define BUFFER_SIZE 256

typedef struct {
    bool is_ready;
    uint8_t payload[160];
} FrameSlot;

FrameSlot jitter_buffer[BUFFER_SIZE];
pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;

int in_fd;

void* ingest_thread_func(void* arg) {
    unsigned char buf[2048];
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 164) continue; 

        uint32_t seq;
        memcpy(&seq, buf, 4);
        seq = ntohl(seq);

        pthread_mutex_lock(&buffer_lock);

        int idx_current = seq % BUFFER_SIZE;
        if (!jitter_buffer[idx_current].is_ready) {
            memcpy(jitter_buffer[idx_current].payload, buf + 4, 160);
            jitter_buffer[idx_current].is_ready = true;
        }

        if (n == 324 && seq > 0) {
            int idx_prev = (seq - 1) % BUFFER_SIZE;
            if (!jitter_buffer[idx_prev].is_ready) {
                memcpy(jitter_buffer[idx_prev].payload, buf + 164, 160);
                jitter_buffer[idx_prev].is_ready = true;
            }
        }
        
        pthread_mutex_unlock(&buffer_lock);
    }
    return NULL;
}

int main(void) {
    in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr));

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    double t0 = atof(getenv("T0"));
    int delay_ms = atoi(getenv("DELAY_MS"));

    pthread_t ingest_tid;
    pthread_create(&ingest_tid, NULL, ingest_thread_func, NULL);

    uint32_t expected_seq = 0;
    
    for (;;) {
        double target_time = t0 + (delay_ms / 1000.0) + (expected_seq * 0.020) - 0.003;
        
        struct timespec ts;
        ts.tv_sec = (time_t)target_time;
        ts.tv_nsec = (long)((target_time - ts.tv_sec) * 1e9);
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        pthread_mutex_lock(&buffer_lock);
        int idx = expected_seq % BUFFER_SIZE;
        
        if (jitter_buffer[idx].is_ready) {
            unsigned char out_buf[164];
            uint32_t net_seq = htonl(expected_seq);
            
            memcpy(out_buf, &net_seq, 4);
            memcpy(out_buf + 4, jitter_buffer[idx].payload, 160);
            
            sendto(out_fd, out_buf, 164, 0, (struct sockaddr *)&player, sizeof(player));
            jitter_buffer[idx].is_ready = false; 
        }
        else {
            fprintf(stderr, "MISS seq=%u\n", expected_seq);
        }
                
        pthread_mutex_unlock(&buffer_lock);
        expected_seq++;
    }
    return 0;
}