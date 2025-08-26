#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_PACKET_SIZE 256
#define BUFFER_SIZE 16
#define APPLICATION_START 0x08008000

typedef enum {
    STATE_IDLE = 0,
    STATE_DFU_ACTIVE,
    STATE_ERROR
} bootloader_state_t;

typedef enum {
    PKT_START_SESSION = 0x01,
    PKT_DATA = 0x02,
    PKT_END_SESSION = 0x03
} packet_type_t;

// Main API
void bootloader_init(void);
bool bootloader_receive_packet(const uint8_t *data, size_t length);
void bootloader_process_cycle(void);
void bootloader_print_stats(void);

// Platform functions (implemented in platform.c)
extern bool start_flash_write(uint32_t address, const uint8_t *data, size_t length);
extern bool is_flash_operation_complete(void);
extern void send_ack_packet(void);
extern void send_nack_packet(uint8_t error_code);

#endif