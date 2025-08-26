// Fixed bootloader.c
#include "bootloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t data[MAX_PACKET_SIZE];
    size_t length;
    bool valid;
} packet_t;

static struct {
    bootloader_state_t state;
    packet_t buffer[BUFFER_SIZE];
    int head, tail, count;
    uint32_t expected_seq;
    uint32_t bytes_received;
    uint32_t total_size;
    bool session_active;
    uint32_t packets_processed;
    uint32_t packets_dropped;
} bootloader = {0};

void bootloader_init(void) {
    memset(&bootloader, 0, sizeof(bootloader));
    bootloader.state = STATE_IDLE;
    printf("[BOOT] Initialized\n");
}

bool bootloader_receive_packet(const uint8_t *data, size_t length) {
    if (bootloader.count >= BUFFER_SIZE) {
        bootloader.packets_dropped++;
        printf("[BOOT] Buffer full - packet dropped\n");
        return false;
    }
    
    packet_t *pkt = &bootloader.buffer[bootloader.head];
    memcpy(pkt->data, data, length);
    pkt->length = length;
    pkt->valid = true;
    
    bootloader.head = (bootloader.head + 1) % BUFFER_SIZE;
    bootloader.count++;
    
    printf("[BOOT] Packet received (%zu bytes) - buffer: %d/%d\n", 
           length, bootloader.count, BUFFER_SIZE);
    
    return true;
}

void bootloader_process_cycle(void) {
    // Check flash operations first
    is_flash_operation_complete();
    
    // Process packets from buffer
    while (bootloader.count > 0) {
        packet_t *pkt = &bootloader.buffer[bootloader.tail];
        if (!pkt->valid) break;
        
        bootloader.tail = (bootloader.tail + 1) % BUFFER_SIZE;
        bootloader.count--;
        bootloader.packets_processed++;
        
        uint8_t seq = pkt->data[0];
        uint8_t packet_type = pkt->data[1];
        
        printf("[BOOT] Processing packet: seq=%d, type=%d, state=%d\n", 
               seq, packet_type, bootloader.state);
        
        switch (bootloader.state) {
            case STATE_IDLE:
                if (packet_type == PKT_START_SESSION && pkt->length >= 4) {
                    bootloader.total_size = (pkt->data[2] << 8) | pkt->data[3];
                    bootloader.state = STATE_DFU_ACTIVE;
                    bootloader.session_active = true;
                    bootloader.expected_seq = 1;
                    bootloader.bytes_received = 0;
                    
                    printf("[BOOT] Session started: %d bytes\n", bootloader.total_size);
                    send_ack_packet();
                } else {
                    printf("[BOOT] Invalid packet in IDLE state\n");
                    send_nack_packet(0x01);
                }
                break;
                
            case STATE_DFU_ACTIVE:
                if (packet_type == PKT_DATA) {
                    if (seq == bootloader.expected_seq) {
                        const uint8_t *payload = &pkt->data[2];
                        size_t payload_len = pkt->length - 2;
                        
                        uint32_t flash_addr = APPLICATION_START + bootloader.bytes_received;
                        
                        printf("[BOOT] Data packet %d: %zu bytes payload\n", seq, payload_len);
                        
                        if (start_flash_write(flash_addr, payload, payload_len)) {
                            bootloader.bytes_received += payload_len;
                            bootloader.expected_seq++;
                            send_ack_packet();
                            printf("[BOOT] Accepted data packet %d (%d/%d bytes total)\n", 
                                   seq, bootloader.bytes_received, bootloader.total_size);
                        } else {
                            printf("[BOOT] Flash busy - sending NACK\n");
                            send_nack_packet(0x03); // Flash busy
                        }
                    } else {
                        printf("[BOOT] Sequence error: got %d, expected %d\n", seq, bootloader.expected_seq);
                        send_nack_packet(0x02); // Sequence error
                    }
                } else if (packet_type == PKT_END_SESSION) {
                    printf("[BOOT] End session request: %d/%d bytes received\n", 
                           bootloader.bytes_received, bootloader.total_size);
                    
                    if (bootloader.bytes_received == bootloader.total_size) {
                        printf("[BOOT] Session complete - all data received\n");
                        send_ack_packet();
                        bootloader.state = STATE_IDLE;
                        bootloader.session_active = false;
                    } else {
                        printf("[BOOT] Incomplete transfer\n");
                        send_nack_packet(0x08); // Incomplete
                    }
                } else {
                    printf("[BOOT] Invalid packet type %d in DFU_ACTIVE state\n", packet_type);
                    send_nack_packet(0x04);
                }
                break;
                
            default:
                printf("[BOOT] Unknown state: %d\n", bootloader.state);
                send_nack_packet(0xFF);
                break;
        }
        
        pkt->valid = false;
    }
}

void bootloader_print_stats(void) {
    printf("\n=== Bootloader Statistics ===\n");
    printf("State: %d (%s)\n", bootloader.state, 
           bootloader.state == STATE_IDLE ? "IDLE" : 
           bootloader.state == STATE_DFU_ACTIVE ? "DFU_ACTIVE" : "ERROR");
    printf("Session Active: %s\n", bootloader.session_active ? "Yes" : "No");
    printf("Packets Processed: %d\n", bootloader.packets_processed);
    printf("Packets Dropped: %d\n", bootloader.packets_dropped);
    printf("Bytes Received: %d/%d\n", bootloader.bytes_received, bootloader.total_size);
    printf("Buffer Count: %d/%d\n", bootloader.count, BUFFER_SIZE);
    printf("Expected Sequence: %d\n", bootloader.expected_seq);
    printf("============================\n\n");
}