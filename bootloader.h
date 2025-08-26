#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_PACKET_SIZE 256
#define BUFFER_SIZE 16
#define APPLICATION_START 0x08008000
#define MAX_APPLICATION_SIZE (1024*1024)
#define FLASH_PAGE_SIZE 2048

// Extended state machine
typedef enum {
    STATE_IDLE = 0,
    STATE_DFU_ACTIVE,
    STATE_DFU_VERIFY,
    STATE_RUNNING_APP,
    STATE_EMERGENCY_RECOVERY,
    STATE_ERROR
} bootloader_state_t;

// Extended packet types
typedef enum {
    PKT_START_SESSION = 0x01,
    PKT_DATA = 0x02,
    PKT_END_SESSION = 0x03,
    PKT_ABORT = 0x04,
    PKT_PING = 0x05,
    PKT_GET_STATUS = 0x06,
    PKT_JUMP_APP = 0x07,
    PKT_EMERGENCY_RESET = 0x08,
    PKT_GET_VERSION = 0x09
} packet_type_t;

// Main API
void bootloader_init(void);
bool bootloader_receive_packet(const uint8_t *data, size_t length);
void bootloader_process_cycle(void);
void bootloader_print_stats(void);
uint32_t get_system_tick(void);

// Platform functions (implemented in platform.c)
extern bool start_flash_write(uint32_t address, const uint8_t *data, size_t length);
extern bool start_flash_erase(uint32_t address);
extern bool is_flash_operation_complete(void);
extern void send_ack_packet(void);
extern void send_nack_packet(uint8_t error_code);

#endif