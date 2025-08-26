#include "bootloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static uint8_t mock_flash[1024*1024] = {0xFF};
static bool flash_busy = false;
static struct timespec flash_start_time;

bool start_flash_write(uint32_t address, const uint8_t *data, size_t length) {
    if (flash_busy) {
        printf("[FLASH] Busy - rejected\n");
        return false;
    }
    
    printf("[FLASH] Writing %zu bytes to 0x%08X\n", length, address);
    
    // Copy to mock flash
    uint32_t offset = address & 0xFFFFF; // Mask to 1MB
    memcpy(&mock_flash[offset], data, length);
    
    // Simulate flash delay
    flash_busy = true;
    clock_gettime(CLOCK_MONOTONIC, &flash_start_time);
    
    return true;
}

bool is_flash_operation_complete(void) {
    if (!flash_busy) return true;
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    uint64_t elapsed_us = (now.tv_sec - flash_start_time.tv_sec) * 1000000 +
                         (now.tv_nsec - flash_start_time.tv_nsec) / 1000;
    
    if (elapsed_us > 2000) { // 2ms flash write time
        flash_busy = false;
        printf("[FLASH] Write complete\n");
    }
    
    return !flash_busy;
}

void send_ack_packet(void) {
    printf("[COMM] -> ACK\n");
}

void send_nack_packet(uint8_t error_code) {
    printf("[COMM] -> NACK (0x%02X)\n", error_code);
}