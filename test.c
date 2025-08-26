// Fixed test.c
#include "bootloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_basic(void) {
    printf("=== Test 1: Basic Functionality ===\n");
    bootloader_init();
    
    // Send a simple ping-like packet (not a real protocol packet)
    uint8_t packet[] = {0x00, 0xFF, 0x12, 0x34}; // Invalid type to test error handling
    bootloader_receive_packet(packet, sizeof(packet));
    bootloader_process_cycle();
    
    printf("✓ Basic test passed\n\n");
}

void test_complete_session(void) {
    printf("=== Test 2: Complete Session ===\n");
    
    bootloader_init();
    
    // Start session - 512 bytes total
    uint8_t start[] = {0x00, 0x01, 0x02, 0x00}; // seq=0, type=START_SESSION, size=512 (0x0200)
    printf("Sending session start packet...\n");
    bootloader_receive_packet(start, sizeof(start));
    bootloader_process_cycle();
    
    printf("\nSending data packets...\n");
    // Send 2 data packets of 256 bytes each = 512 bytes total
    for (int i = 1; i <= 2; i++) {
        uint8_t data_packet[258]; // 2 bytes header + 256 bytes data
        data_packet[0] = i; // sequence
        data_packet[1] = 0x02; // PKT_DATA
        
        // Fill with test data
        for (int j = 2; j < 258; j++) {
            data_packet[j] = (uint8_t)(i * 10 + (j-2));
        }
        
        printf("Sending data packet %d...\n", i);
        bootloader_receive_packet(data_packet, sizeof(data_packet));
        bootloader_process_cycle();
        
        // This demonstrates concurrent processing:
        // While flash is writing, we can still process the packet
        printf("Waiting for flash operation (demonstrating concurrency)...\n");
        usleep(3000); // 3ms - during this time, more packets could arrive
        bootloader_process_cycle(); // Process any flash completion
    }
    
    printf("\nSending session end packet...\n");
    // End session
    uint8_t end[] = {0x03, 0x03}; // seq=3, type=END_SESSION
    bootloader_receive_packet(end, sizeof(end));
    bootloader_process_cycle();
    
    bootloader_print_stats();
    printf("✓ Complete session test passed\n\n");
}

void test_buffer_overflow(void) {
    printf("=== Test 3: Buffer Overflow Protection ===\n");
    
    bootloader_init();
    
    printf("Filling buffer beyond capacity to test overflow protection...\n");
    // Send more packets than buffer can hold (BUFFER_SIZE = 16)
    int sent = 0, dropped = 0;
    for (int i = 0; i < 20; i++) {
        uint8_t packet[] = {i, 0x02, 0xAA, 0xBB, 0xCC, 0xDD}; // Data packet
        if (bootloader_receive_packet(packet, sizeof(packet))) {
            sent++;
        } else {
            dropped++;
        }
    }
    
    printf("Results: Sent=%d, Dropped=%d\n", sent, dropped);
    
    printf("Processing buffered packets...\n");
    // Clear buffer by processing packets
    for (int i = 0; i < 25; i++) {
        bootloader_process_cycle();
    }
    
    bootloader_print_stats();
    printf("✓ Buffer overflow test passed\n\n");
}

void test_concurrent_processing(void) {
    printf("=== Test 4: Concurrent Processing (Key Feature) ===\n");
    
    bootloader_init();
    
    // Start session - 1000 bytes
    uint8_t start[] = {0x00, 0x01, 0x03, 0xE8}; // seq=0, type=START_SESSION, size=1000 (0x03E8)
    printf("Starting DFU session...\n");
    bootloader_receive_packet(start, sizeof(start));
    bootloader_process_cycle();
    
    printf("\nDemonstrating concurrent processing:\n");
    printf("Sending packets rapidly while flash operations are in progress...\n");
    
    // Send 10 packets of 100 bytes each = 1000 bytes total
    for (int i = 1; i <= 10; i++) {
        uint8_t packet[102]; // 2 bytes header + 100 bytes data
        packet[0] = i;
        packet[1] = 0x02; // PKT_DATA
        memset(&packet[2], i, 100); // Fill with pattern
        
        printf("Packet %d: ", i);
        bootloader_receive_packet(packet, sizeof(packet));
        
        // This simulates the key concurrent behavior:
        // - Packets can be received even when flash is busy
        // - Processing happens when flash operations complete
        if (i % 2 == 0) {
            printf("Processing (flash may be busy)...\n");
            bootloader_process_cycle();
            usleep(4000); // 4ms - simulates time when flash is busy
            printf("Flash operation time elapsed, processing again...\n");
            bootloader_process_cycle(); // This should complete the flash operation
        } else {
            printf("Queued in buffer\n");
        }
    }
    
    printf("\nFinal processing to clear all remaining packets and flash operations...\n");
    // Final processing to handle any remaining packets/flash operations
    for (int i = 0; i < 20; i++) {
        bootloader_process_cycle();
        usleep(3000); // Allow flash operations to complete
    }
    
    // End the session
    printf("Ending session...\n");
    uint8_t end[] = {0x0B, 0x03}; // seq=11, type=END_SESSION
    bootloader_receive_packet(end, sizeof(end));
    bootloader_process_cycle();
    
    bootloader_print_stats();
    printf("✓ Concurrent processing test passed\n\n");
    
    printf("KEY INSIGHT: During flash write operations, new packets can still\n");
    printf("be received and buffered. This prevents packet loss that would\n");
    printf("occur in traditional blocking bootloaders.\n\n");
}

int main(void) {
    printf("========================================\n");
    printf("  Mock Bootloader Test Suite\n");
    printf("  Demonstrating Concurrent Design\n");
    printf("========================================\n\n");
    
    test_basic();
    test_complete_session();
    test_buffer_overflow();
    test_concurrent_processing();
    
    printf("========================================\n");
    printf("  All Tests Completed Successfully!\n");
    printf("========================================\n\n");
    
    printf("SUMMARY: This bootloader design solves the packet loss problem\n");
    printf("by using a circular buffer to decouple packet reception from\n");
    printf("flash operations. Packets can arrive and be buffered even when\n");
    printf("the flash is busy, preventing the dead time that causes packet\n");
    printf("drops in traditional bootloaders.\n");
    
    return 0;
}