#include "bootloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Application validation result
typedef struct {
    bool valid;
    uint32_t calculated_crc;
    uint32_t expected_crc;
    uint32_t size;
} app_validation_t;

typedef struct {
    uint8_t data[MAX_PACKET_SIZE];
    size_t length;
    bool valid;
} packet_t;

static struct {
    bootloader_state_t state;
    bootloader_state_t previous_state;
    packet_t buffer[BUFFER_SIZE];
    int head, tail, count;
    
    // Session management
    uint32_t expected_seq;
    uint32_t bytes_received;
    uint32_t total_size;
    uint32_t expected_crc;
    bool session_active;
    
    // Statistics and error tracking
    uint32_t packets_processed;
    uint32_t packets_dropped;
    uint32_t error_count;
    uint32_t recovery_attempts;
    uint32_t app_launch_attempts;
    
    // Timeouts and watchdogs
    uint32_t state_entry_time;
    uint32_t last_activity_time;
    uint32_t session_timeout_ms;
    uint32_t app_validation_timeout_ms;
    
    // Application management
    app_validation_t app_validation;
    bool force_bootloader_mode;
    
} bootloader = {0};

// Forward declarations
static void enter_state(bootloader_state_t new_state);
static bool validate_state_transition(bootloader_state_t from, bootloader_state_t to);
static void handle_timeout_checks(void);
static bool validate_application(void);
static void handle_emergency_condition(void);
static void handle_idle_packet(packet_t *pkt, uint8_t seq, uint8_t packet_type);
static void handle_dfu_packet(packet_t *pkt, uint8_t seq, uint8_t packet_type);

void bootloader_init(void) {
    memset(&bootloader, 0, sizeof(bootloader));
    bootloader.session_timeout_ms = 30000; // 30 seconds
    bootloader.app_validation_timeout_ms = 5000; // 5 seconds
    bootloader.force_bootloader_mode = false;
    
    enter_state(STATE_IDLE);
    printf("[BOOT] Advanced bootloader initialized (v1.2.0)\n");
}

static void enter_state(bootloader_state_t new_state) {
    if (!validate_state_transition(bootloader.state, new_state)) {
        printf("[BOOT] ERROR: Invalid state transition %d -> %d\n", bootloader.state, new_state);
        enter_state(STATE_ERROR);
        return;
    }
    
    bootloader.previous_state = bootloader.state;
    bootloader.state = new_state;
    bootloader.state_entry_time = get_system_tick();
    
    // State entry actions
    switch (new_state) {
        case STATE_IDLE:
            printf("[BOOT] Entered IDLE state\n");
            bootloader.session_active = false;
            bootloader.expected_seq = 0;
            bootloader.bytes_received = 0;
            break;
            
        case STATE_DFU_ACTIVE:
            printf("[BOOT] Entered DFU_ACTIVE state\n");
            break;
            
        case STATE_DFU_VERIFY:
            printf("[BOOT] Entered DFU_VERIFY state - validating application\n");
            break;
            
        case STATE_RUNNING_APP:
            printf("[BOOT] Entered RUNNING_APP state - launching application\n");
            bootloader.app_launch_attempts++;
            break;
            
        case STATE_EMERGENCY_RECOVERY:
            printf("[BOOT] Entered EMERGENCY_RECOVERY state\n");
            bootloader.recovery_attempts++;
            bootloader.force_bootloader_mode = true;
            break;
            
        case STATE_ERROR:
            printf("[BOOT] Entered ERROR state (previous: %d)\n", bootloader.previous_state);
            bootloader.error_count++;
            break;
    }
}

static bool validate_state_transition(bootloader_state_t from, bootloader_state_t to) {
    switch (from) {
        case STATE_IDLE:
            return (to == STATE_DFU_ACTIVE || to == STATE_RUNNING_APP || 
                    to == STATE_EMERGENCY_RECOVERY || to == STATE_ERROR);
            
        case STATE_DFU_ACTIVE:
            return (to == STATE_DFU_VERIFY || to == STATE_IDLE || 
                    to == STATE_EMERGENCY_RECOVERY || to == STATE_ERROR);
            
        case STATE_DFU_VERIFY:
            return (to == STATE_RUNNING_APP || to == STATE_IDLE || 
                    to == STATE_EMERGENCY_RECOVERY || to == STATE_ERROR);
            
        case STATE_RUNNING_APP:
            return (to == STATE_IDLE || to == STATE_EMERGENCY_RECOVERY || 
                    to == STATE_ERROR);
            
        case STATE_EMERGENCY_RECOVERY:
            return (to == STATE_IDLE || to == STATE_ERROR);
            
        case STATE_ERROR:
            return (to == STATE_IDLE || to == STATE_EMERGENCY_RECOVERY);
    }
    return false;
}

bool bootloader_receive_packet(const uint8_t *data, size_t length) {
    if (bootloader.count >= BUFFER_SIZE) {
        bootloader.packets_dropped++;
        printf("[BOOT] Buffer full - packet dropped (dropped: %d)\n", bootloader.packets_dropped);
        
        // If too many drops, enter recovery
        if (bootloader.packets_dropped > 10 && bootloader.state != STATE_EMERGENCY_RECOVERY) {
            handle_emergency_condition();
        }
        return false;
    }
    
    packet_t *pkt = &bootloader.buffer[bootloader.head];
    memcpy(pkt->data, data, length);
    pkt->length = length;
    pkt->valid = true;
    
    bootloader.head = (bootloader.head + 1) % BUFFER_SIZE;
    bootloader.count++;
    bootloader.last_activity_time = get_system_tick();
    
    printf("[BOOT] Packet received (%zu bytes) - buffer: %d/%d\n", 
           length, bootloader.count, BUFFER_SIZE);
    
    return true;
}

void bootloader_process_cycle(void) {
    handle_timeout_checks();
    is_flash_operation_complete();
    
    // State-specific background processing
    switch (bootloader.state) {
        case STATE_DFU_VERIFY:
            if (validate_application()) {
                printf("[BOOT] Application validation successful\n");
                enter_state(STATE_RUNNING_APP);
            } else {
                printf("[BOOT] Application validation failed\n");
                enter_state(STATE_ERROR);
            }
            break;
            
        case STATE_RUNNING_APP:
            // In real implementation, would jump to application
            printf("[BOOT] Application launch simulation complete\n");
            enter_state(STATE_IDLE); // For simulation, return to idle
            break;
            
        case STATE_EMERGENCY_RECOVERY:
            // Auto-recovery after timeout
            if ((get_system_tick() - bootloader.state_entry_time) > 10000000) { // 10 seconds
                printf("[BOOT] Emergency recovery timeout - returning to idle\n");
                bootloader.packets_dropped = 0; // Reset error counters
                bootloader.error_count = 0;
                enter_state(STATE_IDLE);
            }
            break;
            
        default:
            break;
    }
    
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
        
        // Global packet handlers (work in any state)
        switch (packet_type) {
            case PKT_PING:
                printf("[BOOT] Ping received\n");
                send_ack_packet();
                break;
                
            case PKT_GET_STATUS:
                printf("[BOOT] Status request\n");
                // In real implementation, would send status packet
                send_ack_packet();
                break;
                
            case PKT_EMERGENCY_RESET:
                printf("[BOOT] Emergency reset requested\n");
                handle_emergency_condition();
                break;
                
            case PKT_ABORT:
                if (bootloader.state == STATE_DFU_ACTIVE) {
                    printf("[BOOT] DFU session aborted\n");
                    enter_state(STATE_IDLE);
                    send_ack_packet();
                }
                break;
                
            default:
                // State-specific packet handling
                switch (bootloader.state) {
                    case STATE_IDLE:
                        handle_idle_packet(pkt, seq, packet_type);
                        break;
                        
                    case STATE_DFU_ACTIVE:
                        handle_dfu_packet(pkt, seq, packet_type);
                        break;
                        
                    case STATE_EMERGENCY_RECOVERY:
                        // Only respond to emergency reset and ping in recovery mode
                        if (packet_type != PKT_PING && packet_type != PKT_EMERGENCY_RESET) {
                            printf("[BOOT] Only emergency commands accepted in recovery mode\n");
                            send_nack_packet(0x10); // Recovery mode error
                        }
                        break;
                        
                    default:
                        printf("[BOOT] Packet ignored in state %d\n", bootloader.state);
                        send_nack_packet(0x11); // Invalid state error
                        break;
                }
                break;
        }
        
        pkt->valid = false;
    }
}

static void handle_idle_packet(packet_t *pkt, uint8_t seq, uint8_t packet_type) {
    switch (packet_type) {
        case PKT_START_SESSION:
            if (!bootloader.force_bootloader_mode && pkt->length >= 8) {
                bootloader.total_size = (pkt->data[2] << 24) | (pkt->data[3] << 16) | 
                                       (pkt->data[4] << 8) | pkt->data[5];
                bootloader.expected_crc = (pkt->data[6] << 8) | pkt->data[7];
                
                if (bootloader.total_size > 0 && bootloader.total_size <= 1024*1024) {
                    enter_state(STATE_DFU_ACTIVE);
                    bootloader.session_active = true;
                    bootloader.expected_seq = 1;
                    bootloader.bytes_received = 0;
                    
                    printf("[BOOT] Session started: %d bytes, CRC=0x%04X\n", 
                           bootloader.total_size, bootloader.expected_crc);
                    send_ack_packet();
                } else {
                    printf("[BOOT] Invalid session size: %d\n", bootloader.total_size);
                    send_nack_packet(0x05); // Invalid size
                }
            } else if (bootloader.force_bootloader_mode) {
                printf("[BOOT] Bootloader mode forced - DFU disabled\n");
                send_nack_packet(0x12); // Bootloader mode forced
            } else {
                printf("[BOOT] Invalid session start packet\n");
                send_nack_packet(0x01); // Invalid packet
            }
            break;
            
        case PKT_JUMP_APP:
            if (!bootloader.force_bootloader_mode) {
                printf("[BOOT] Application launch requested\n");
                enter_state(STATE_DFU_VERIFY); // Validate before jumping
                send_ack_packet();
            } else {
                printf("[BOOT] Application launch disabled in forced bootloader mode\n");
                send_nack_packet(0x12);
            }
            break;
            
        default:
            printf("[BOOT] Invalid packet type %d in IDLE state\n", packet_type);
            send_nack_packet(0x01);
            break;
    }
}

static void handle_dfu_packet(packet_t *pkt, uint8_t seq, uint8_t packet_type) {
    switch (packet_type) {
        case PKT_DATA:
            if (seq == bootloader.expected_seq) {
                const uint8_t *payload = &pkt->data[2];
                size_t payload_len = pkt->length - 2;
                
                uint32_t flash_addr = APPLICATION_START + bootloader.bytes_received;
                
                printf("[BOOT] Data packet %d: %zu bytes payload\n", seq, payload_len);
                
                if (start_flash_write(flash_addr, payload, payload_len)) {
                    bootloader.bytes_received += payload_len;
                    bootloader.expected_seq++;
                    send_ack_packet();
                    printf("[BOOT] Progress: %d/%d bytes (%.1f%%)\n", 
                           bootloader.bytes_received, bootloader.total_size,
                           (float)bootloader.bytes_received * 100.0f / bootloader.total_size);
                } else {
                    printf("[BOOT] Flash busy - sending NACK\n");
                    send_nack_packet(0x03); // Flash busy
                }
            } else {
                printf("[BOOT] Sequence error: got %d, expected %d\n", seq, bootloader.expected_seq);
                send_nack_packet(0x02); // Sequence error
                
                // Too many sequence errors trigger recovery
                if (++bootloader.error_count > 5) {
                    handle_emergency_condition();
                }
            }
            break;
            
        case PKT_END_SESSION:
            printf("[BOOT] End session request: %d/%d bytes received\n", 
                   bootloader.bytes_received, bootloader.total_size);
            
            if (bootloader.bytes_received == bootloader.total_size) {
                printf("[BOOT] All data received - starting verification\n");
                enter_state(STATE_DFU_VERIFY);
                send_ack_packet();
            } else {
                printf("[BOOT] Incomplete transfer\n");
                send_nack_packet(0x08); // Incomplete
                enter_state(STATE_ERROR);
            }
            break;
            
        default:
            printf("[BOOT] Invalid packet type %d in DFU_ACTIVE state\n", packet_type);
            send_nack_packet(0x04);
            break;
    }
}

static void handle_timeout_checks(void) {
    uint32_t current_time = get_system_tick();
    
    // Session timeout check
    if (bootloader.session_active) {
        if ((current_time - bootloader.last_activity_time) > 
            (bootloader.session_timeout_ms * 1000)) {
            printf("[BOOT] Session timeout - aborting\n");
            enter_state(STATE_ERROR);
        }
    }
    
    // State-specific timeout checks
    switch (bootloader.state) {
        case STATE_DFU_VERIFY:
            if ((current_time - bootloader.state_entry_time) > 
                (bootloader.app_validation_timeout_ms * 1000)) {
                printf("[BOOT] Application validation timeout\n");
                enter_state(STATE_ERROR);
            }
            break;
            
        case STATE_ERROR:
            // Auto-recovery from error state after 5 seconds
            if ((current_time - bootloader.state_entry_time) > 5000000) {
                printf("[BOOT] Auto-recovery from error state\n");
                enter_state(STATE_IDLE);
            }
            break;
            
        default:
            break;
    }
}

static bool validate_application(void) {
    // Simulate application validation
    printf("[BOOT] Validating application...\n");
    
    // In real implementation, would read from flash and calculate CRC
    bootloader.app_validation.size = bootloader.bytes_received;
    bootloader.app_validation.calculated_crc = 0x1234; // Simulated
    bootloader.app_validation.expected_crc = bootloader.expected_crc;
    bootloader.app_validation.valid = (bootloader.app_validation.calculated_crc == 
                                      bootloader.app_validation.expected_crc);
    
    printf("[BOOT] Validation result: %s (CRC: calc=0x%04X, exp=0x%04X)\n",
           bootloader.app_validation.valid ? "PASS" : "FAIL",
           bootloader.app_validation.calculated_crc,
           bootloader.app_validation.expected_crc);
    
    return bootloader.app_validation.valid;
}

static void handle_emergency_condition(void) {
    printf("[BOOT] EMERGENCY CONDITION DETECTED\n");
    enter_state(STATE_EMERGENCY_RECOVERY);
}

// Mock function - in real implementation would get system tick
uint32_t get_system_tick(void) {
    static uint32_t tick = 0;
    return tick += 1000; // Increment by 1ms each call
}

void bootloader_print_stats(void) {
    printf("\n=== Advanced Bootloader Statistics ===\n");
    printf("Current State: %d (%s)\n", bootloader.state,
           bootloader.state == STATE_IDLE ? "IDLE" :
           bootloader.state == STATE_DFU_ACTIVE ? "DFU_ACTIVE" :
           bootloader.state == STATE_DFU_VERIFY ? "DFU_VERIFY" :
           bootloader.state == STATE_RUNNING_APP ? "RUNNING_APP" :
           bootloader.state == STATE_EMERGENCY_RECOVERY ? "EMERGENCY_RECOVERY" :
           "ERROR");
    printf("Previous State: %d\n", bootloader.previous_state);
    printf("Session Active: %s\n", bootloader.session_active ? "Yes" : "No");
    printf("Forced Bootloader Mode: %s\n", bootloader.force_bootloader_mode ? "Yes" : "No");
    printf("\nPacket Statistics:\n");
    printf("  Processed: %d\n", bootloader.packets_processed);
    printf("  Dropped: %d\n", bootloader.packets_dropped);
    printf("  Buffer Count: %d/%d\n", bootloader.count, BUFFER_SIZE);
    printf("\nTransfer Statistics:\n");
    printf("  Bytes Received: %d/%d\n", bootloader.bytes_received, bootloader.total_size);
    printf("  Expected Sequence: %d\n", bootloader.expected_seq);
    printf("\nError Statistics:\n");
    printf("  Error Count: %d\n", bootloader.error_count);
    printf("  Recovery Attempts: %d\n", bootloader.recovery_attempts);
    printf("  App Launch Attempts: %d\n", bootloader.app_launch_attempts);
    printf("\nApplication Validation:\n");
    printf("  Valid: %s\n", bootloader.app_validation.valid ? "Yes" : "No");
    printf("  Size: %d bytes\n", bootloader.app_validation.size);
    printf("  CRC: calc=0x%04X, exp=0x%04X\n", 
           bootloader.app_validation.calculated_crc,
           bootloader.app_validation.expected_crc);
    printf("=====================================\n\n");
}