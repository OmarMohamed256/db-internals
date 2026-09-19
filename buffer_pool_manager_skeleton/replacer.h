#pragma once

#include "page.h"

/**
 * Abstract class for a page replacement policy.
 */
class Replacer {
public:
    virtual ~Replacer() = default;

    // Remove the victim frame as defined by the replacement policy.
    virtual bool Victim(frame_id_t *frame_id) = 0;

    // Pins a frame, indicating that it should not be victimized until it is unpinned.
    virtual void Pin(frame_id_t frame_id) = 0;

    // Unpins a frame, indicating that it can now be victimized.
    virtual void Unpin(frame_id_t frame_id) = 0;

    virtual void RecordAccess(frame_id_t frame_id) = 0;

    // Returns the number of frames that are currently in the replacer.
    virtual size_t Size() = 0;
};


