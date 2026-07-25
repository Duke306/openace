#include <stdint.h>
#include <stdio.h>
#include "../flashstore.hpp"

#include "pico/flash.h"
#include "FreeRTOS.h"
#include "task.h"

extern char __flash_binary_end;

typedef struct
{
    const size_t size;
    uint32_t address;
    const uint8_t *p1;
} FlashMutation;

FlashStore::FlashStore(size_t size_, size_t startsOffsetFromEnd_) : ConfigStore(),
                                                                    _size(((size_ + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE),
                                                                    startsOffsetFromEnd(((startsOffsetFromEnd_ + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE),
                                                                    bytesWritten(0)
{
    // #if defined(RUN_FREERTOS_ON_CORE)
    //     flash_safe_execute_core_init();
    // #endif
}

uint32_t FlashStore::flashAddress() const
{
    return (PICO_FLASH_SIZE_BYTES - startsOffsetFromEnd);
}

void FlashStore::rewind()
{
    // NOOP
}

size_t FlashStore::write(uint8_t c)
{
    (void)c;
    panic("Operation of one byte not supported in FlashStore");
}

void __not_in_flash_func(pico_flash_bank_perform_flash_erase)(void *param)
{
    const FlashMutation *mop = (const FlashMutation *)param;
    auto flashEraseBytes = ((mop->size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    flash_range_erase(mop->address, flashEraseBytes);
}

void __not_in_flash_func(pico_flash_bank_perform_flash_write)(void *param)
{
    const FlashMutation *mop = (const FlashMutation *)param;
    auto flashProgramBytes = ((mop->size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    flash_range_program(mop->address, mop->p1, flashProgramBytes);
}


size_t __not_in_flash_func(FlashStore::write)(const uint8_t *buffer, size_t length)
{
    if (length > _size)
    {
        return 0;
    }

    FlashMutation mop =
    {
        .size = length,
        .address = flashAddress(),
        .p1 = buffer
    };

    // Check if FreeRTOS scheduler is running
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
//        printf("flash b:%p s:%d l:%d erase:%d flash:%d \n",  buffer, _size, length, ((length + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE, ((length + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE);
        // Might need to be doen in two different calls, I had one user where
        // the flash was not written...
        flash_safe_execute(pico_flash_bank_perform_flash_erase, &mop, UINT32_MAX);
        flash_safe_execute(pico_flash_bank_perform_flash_write, &mop, UINT32_MAX);
        bytesWritten = length;
    }
    else
    {
        puts("Doing Flash operations seems to be buggy outside of FreeRTOS task, please call these only from within a FreeRTOS Task");
        puts("This is NOOP, nothing stored in flash!");
        // Direct call when FreeRTOS is not active (early boot or baremetal) and we ar enot using pico_multicore
        // uint32_t irqStatus = save_and_disable_interrupts();
        // irq_set_enabled(USBCTRL_IRQ, false);
        // pico_flash_bank_perform_flash_mutation_operation(&mop);
        // flash_safe_execute(pico_flash_bank_perform_flash_mutation_operation, &mop, UINT32_MAX);
        // irq_set_enabled(USBCTRL_IRQ, true);
        // restore_interrupts(irqStatus);
    }
    return length;
}

size_t __not_in_flash_func(FlashStore::erase)()
{
    FlashMutation mop =
        {
            .size = _size,
            .address = flashAddress(),
            .p1 = nullptr};

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        puts("Flash erase requires the FreeRTOS scheduler to be running");
        return 0;
    }

    auto result = flash_safe_execute(pico_flash_bank_perform_flash_erase, &mop, UINT32_MAX);
    if (result != PICO_OK)
    {
        return 0;
    }

    return _size;
}

const uint8_t *FlashStore::data() const
{
    return (const uint8_t *)(XIP_BASE + flashAddress());
}

size_t FlashStore::writtenSize() const
{
    return bytesWritten;
}

size_t FlashStore::capacity() const
{
    return _size;
}
