#include "protocols/storage/storage_record.h"
#include <string.h>


/** @brief Compare two sequence numbers with unsigned wraparound handling. */
static bool is_newer(uint32_t a, uint32_t b) {
    if (a == b)
        return false;
    return (uint32_t)(a - b) < 0x80000000u;
}

/** @brief Select the best valid slot between A and B at boot restore.
 *  @param[in] buf_a               Buffer for slot A.
 *  @param[in] size_a              Size of slot A buffer.
 *  @param[in] buf_b               Buffer for slot B.
 *  @param[in] size_b              Size of slot B buffer.
 *  @param[in] expected_type       Expected record type identifier.
 *  @param[in] expected_schema     Expected payload schema version.
 *  @param[in] expected_payload_size Expected payload byte count.
 *  @return SlotSelectionResult with the chosen slot and per-slot reasons. */
SlotSelectionResult ab_slot_select(const uint8_t* buf_a,
                                   uint16_t size_a,
                                   const uint8_t* buf_b,
                                   uint16_t size_b,
                                   uint8_t expected_type,
                                   uint8_t expected_schema,
                                   uint16_t expected_payload_size) {
    SlotSelectionResult r;
    memset(&r, 0, sizeof(r));
    r.selected_slot = 0xFF;

    r.reason_a = StorageRecord_ClassifySlot(
        buf_a, size_a, expected_type, expected_schema, expected_payload_size);
    r.reason_b = StorageRecord_ClassifySlot(
        buf_b, size_b, expected_type, expected_schema, expected_payload_size);

    r.slot_a_valid = (r.reason_a == SLOT_VALID_COMPATIBLE);
    r.slot_b_valid = (r.reason_b == SLOT_VALID_COMPATIBLE);

    if (r.slot_a_valid)
        r.sequence_a = buf_a ? le_read32(buf_a + 8) : 0;
    if (r.slot_b_valid)
        r.sequence_b = buf_b ? le_read32(buf_b + 8) : 0;

    if (r.slot_a_valid && r.slot_b_valid) {
        /* Both valid — pick newest */
        if (r.sequence_a == r.sequence_b) {
            /* Equal sequence — compare body for conflict detection.
             * Body is from offset 0x10 to (slot_size - 1) excluding commit byte. */
            uint16_t body_len = (uint16_t)(size_a - 0x10u - 1u);
            if (memcmp(buf_a + 0x10, buf_b + 0x10, body_len) == 0) {
                /* Identical content — pick A deterministic */
                r.selected_slot = 0;
            } else {
                /* Equal sequence with different content = conflict */
                r.selected_slot = 0xFF; /* conflict */
            }
        } else if (is_newer(r.sequence_a, r.sequence_b)) {
            r.selected_slot = 0; /* A newer */
        } else {
            r.selected_slot = 1; /* B newer */
        }
    } else if (r.slot_a_valid && !r.slot_b_valid) {
        r.selected_slot = 0; /* Only A */
    } else if (!r.slot_a_valid && r.slot_b_valid) {
        r.selected_slot = 1; /* Only B */
    } else {
        r.selected_slot = 0xFF; /* None valid */
    }

    return r;
}

/** @brief Choose the target slot for the next commit (round-robin wear levelling).
 *  @param[in] slot_a_valid  Whether slot A is currently valid.
 *  @param[in] slot_b_valid  Whether slot B is currently valid.
 *  @param[in] boot_selected The slot selected at boot (0 = A, 1 = B).
 *  @return Target slot for the next commit: 0 = A, 1 = B. */
uint8_t
ab_slot_choose_target(bool slot_a_valid, bool slot_b_valid, uint8_t boot_selected) {
    if (!slot_a_valid && !slot_b_valid)
        return 0; /* Neither valid — start with A */
    if (slot_a_valid && !slot_b_valid)
        return 1; /* Only A valid — target B */
    if (!slot_a_valid && slot_b_valid)
        return 0; /* Only B valid — target A */
    /* Both valid — target the one NOT selected at boot */
    return (boot_selected == 0) ? 1 : 0;
}
