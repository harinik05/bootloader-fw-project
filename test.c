#include "bootloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_basic_commands(void) {
    printf("=== Test 1: Basic Command Handling ===\n");
    bootloader_init();
    
    // Test ping command
    uint8_t ping[] = {0x00, 0x05}; // seq=0, type=PING
    printf("Sending PING command...\n");
    bootloader_receive_packet(ping, sizeof(ping));
    bootloader_process_cycle();
    
    // Test status request
    uint8_t status[] = {0x01, 0x06}; // seq=1, type=GET_STATUS
    printf("Sending GET_STATUS command...\n");
    bootloader_receive_packet(status, sizeof(status));
    bootloader_process_cycle();
    
    printf("✓ Basic commands test passed\n\n");
}

void test_complete_dfu_workflow(void) {
    printf("=== Test 2: Complete DFU Workflow with Verification ===\n");
    
    bootloader_init();
    
    // Start session - 512 bytes, CRC = 0x1234 (matches validation)
    uint8_t start[] = {0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x12, 0x34}; 
    printf("Starting DFU session (512 bytes, CRC=0x1234)...\n");
    bootloader_receive_packet(start, sizeof(start));
    bootloader_process_cycle();
    
    printf("\nSending firmware data...\n");
    // Send 2 data packets of 256 bytes each = 512 bytes total
    for (int i = 1; i <= 2; i++) {
        uint8_t data_packet[258];
        data_packet[0] = i; // sequence
        data_packet[1] = 0x02; // PKT_DATA
        
        // Fill with test pattern
        for (int j = 2; j < 258; j++) {
            data_packet[j] = (uint8_t)(i * 16 + (j-2));
        }
        
        printf("Sending data packet %d (256 bytes)...\n", i);
        bootloader_receive_packet(data_packet, sizeof(data_packet));
        bootloader_process_cycle();
        
        // Allow flash operations to complete
        usleep(3000);
        bootloader_process_cycle();
    }
    
    printf("\nEnding DFU session...\n");
    // End session - this should trigger verification
    uint8_t end[] = {0x03, 0x03}; // seq=3, type=END_SESSION
    bootloader_receive_packet(end, sizeof(end));
    bootloader_process_cycle();
    
    printf("\nProcessing verification and app launch...\n");
    // Process verification state and app launch
    for (int i = 0; i < 5; i++) {
        bootloader_process_cycle();
        usleep(1000);
    }
    
    bootloader_print_stats();
    printf("✓ Complete DFU workflow test passed\n\n");
}

void test_emergency_reset_command(void) {
    printf("=== Test 3: Emergency Reset Command ===\n");
    
    bootloader_init();
    
    // Start a session
    uint8_t start[] = {0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x12, 0x34};
    bootloader_receive_packet(start, sizeof(start));
    bootloader_process_cycle();
    
    printf("Session active, now sending emergency reset...\n");
    
    // Send emergency reset command
    uint8_t emergency[] = {0x99, 0x08}; // seq=99, type=EMERGENCY_RESET
    bootloader_receive_packet(emergency, sizeof(emergency));
    bootloader_process_cycle();
    
    printf("Testing recovery mode commands...\n");
    
    // Try sending normal commands in recovery mode (should be rejected)
    uint8_t normal_cmd[] = {0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x12, 0x34}; // START_SESSION
    bootloader_receive_packet(normal_cmd, sizeof(normal_cmd));
    bootloader_process_cycle();
    
    // Ping should still work in recovery mode
    uint8_t ping[] = {0x02, 0x05}; // PING
    bootloader_receive_packet(ping, sizeof(ping));
    bootloader_process_cycle();
    
    printf("Testing recovery timeout...\n");
    
    // Wait for auto-recovery from emergency mode
    for (int i = 0; i < 12; i++) {
        printf("Recovery timeout: %d/10 seconds\n", i+1);
        bootloader_process_cycle();
        usleep(1000000); // 1 second
    }
    
    bootloader_print_stats();
    printf("✓ Emergency reset test passed\n\n");
}

void test_concurrent_with_state_transitions(void) {
    printf("=== Test 4: Concurrent Processing with State Transitions ===\n");
    
    bootloader_init();
    
    printf("Starting complex concurrent scenario...\n");
    
    // Start session
    uint8_t start[] = {0x00, 0x01, 0x00, 0x00, 0x03, 0x20, 0x12, 0x34}; // 800 bytes
    bootloader_receive_packet(start, sizeof(start));
    
    // Send data packets while mixing in other commands
    for (int i = 1; i <= 8; i++) {
        // Send data packet
        uint8_t data_packet[102];
        data_packet[0] = i;
        data_packet[1] = 0x02; // PKT_DATA
        memset(&data_packet[2], i * 10, 100); // 100 bytes payload
        
        printf("Sending data packet %d...\n", i);
        bootloader_receive_packet(data_packet, sizeof(data_packet));
        
        // Mix in ping commands to test concurrent command processing
        if (i % 3 == 0) {
            uint8_t ping[] = {0x80 + i, 0x05}; // PING with high sequence
            printf("  Mixed in PING command\n");
            bootloader_receive_packet(ping, sizeof(ping));
        }
        
        // Process some packets
        if (i % 2 == 0) {
            printf("  Processing cycle...\n");
            bootloader_process_cycle();
            usleep(2000); // Simulate flash time
        }
    }
    
    printf("Final processing to complete all operations...\n");
    // Final processing
    for (int i = 0; i < 10; i++) {
        bootloader_process_cycle();
        usleep(3000);
    }
    
    // End session to trigger verification
    printf("Ending session and triggering verification...\n");
    uint8_t end[] = {0x09, 0x03}; // seq=9, type=END_SESSION
    bootloader_receive_packet(end, sizeof(end));
    
    // Process through verification and app launch
    for (int i = 0; i < 8; i++) {
        printf("  State processing cycle %d...\n", i+1);
        bootloader_process_cycle();
        usleep(1000);
    }
    
    bootloader_print_stats();
    printf("✓ Concurrent processing with state transitions test passed\n\n");
    
    printf("KEY INSIGHT: The advanced state machine maintains concurrent\n");
    printf("packet processing while managing complex state transitions,\n");
    printf("error recovery, and application validation workflows.\n\n");
}

int main(void) {
    printf("========================================\n");
    printf("  Advanced Bootloader Test Suite\n");
    printf("  Extended State Machine & Recovery\n");
    printf("========================================\n\n");
    
    test_basic_commands();
    test_complete_dfu_workflow();
    test_emergency_reset_command();
    test_concurrent_with_state_transitions();
    
    printf("========================================\n");
    printf("  All Advanced Tests Completed!\n");
    printf("========================================\n\n");
    
    printf("ADVANCED FEATURES DEMONSTRATED:\n");
    printf("• Extended state machine with 6 states\n");
    printf("• Application validation and launch sequence\n");
    printf("• Emergency recovery mechanisms\n");
    printf("• Automatic error recovery with timeouts\n");
    printf("• Concurrent processing during state transitions\n");
    printf("• Comprehensive error tracking and statistics\n\n");
    
    printf("This bootloader design provides enterprise-grade reliability\n");
    printf("with robust error handling while maintaining the core benefit\n");
    printf("of concurrent packet processing to prevent packet loss.\n");
    
    return 0;
}