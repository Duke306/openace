#pragma once

#include <stdint.h>
#include <stddef.h>

class ConfigStore
{

public:

    /**
     * Rewind the write pointer to the beginning
    */
    virtual void rewind() = 0;

    // Writes one byte, returns the number of bytes written (0 or 1)
    virtual size_t write(uint8_t c) = 0;

    /**
     * Write data to the datastore from a buffer.
     */
    virtual size_t write(const uint8_t *buffer, size_t length) = 0;

    /**
     * Erase the complete datastore.
     *
     * Returns the number of bytes erased, or zero when the erase failed.
     */
    virtual size_t erase() = 0;

    /**
     * Get's a pointer to the datastore
     * Note: If it's not possible to give a pointer to the data then the implementation must provide an internal buffer
     * as it's assumed flash is always readble and is located within the same flash as
    */
    virtual const uint8_t* data() const = 0;

    /**
     * Gets the number of bytes written since the store was last rewound.
     */
    virtual size_t writtenSize() const = 0;

    /**
     * Gets the maximum number of bytes that can be stored.
     */
    virtual size_t capacity() const = 0;

};
