#include "protocols/storage/storage_record.h"
#include <string.h>


typedef enum {
    SEQUENCE_EQUAL,
    SEQUENCE_A_NEWER,
    SEQUENCE_B_NEWER,
    SEQUENCE_AMBIGUOUS
} SequenceOrder;

static SequenceOrder compare_sequence(uint32_t a, uint32_t b)
{
    uint32_t difference = (uint32_t)(a - b);
    if (difference == 0u)
        return SEQUENCE_EQUAL;
    if (difference == 0x80000000u)
        return SEQUENCE_AMBIGUOUS;
    return difference < 0x80000000u ? SEQUENCE_A_NEWER
                                    : SEQUENCE_B_NEWER;
}

static bool slot_reason_is_protected(SlotClassification reason)
{
    return reason == SLOT_VALID_UNSUPPORTED_SCHEMA ||
           reason == SLOT_VALID_INCOMPATIBLE;
}

SlotClassification ab_slot_classify(
    const uint8_t *slot_buffer,
    uint16_t slot_size,
    uint8_t expected_type,
    uint8_t expected_schema,
    uint16_t expected_payload_size)
{
    return StorageRecord_ClassifySlot(slot_buffer, slot_size, expected_type,
                                      expected_schema,
                                      expected_payload_size);
}


SlotSelectionResult ab_slot_select(
    const uint8_t *buf_a, uint16_t size_a,
    const uint8_t *buf_b, uint16_t size_b,
    uint8_t        expected_type,
    uint8_t        expected_schema,
    uint16_t       expected_payload_size)
{
    SlotSelectionResult r;
    memset(&r, 0, sizeof(r));
    r.selected_slot = SLOT_INDEX_NONE;
    r.status = SLOT_SELECTION_NONE;

    r.reason_a = StorageRecord_ClassifySlot(buf_a, size_a,
                                             expected_type, expected_schema,
                                             expected_payload_size);
    r.reason_b = StorageRecord_ClassifySlot(buf_b, size_b,
                                             expected_type, expected_schema,
                                             expected_payload_size);

    r.slot_a_valid = (r.reason_a == SLOT_VALID_COMPATIBLE);
    r.slot_b_valid = (r.reason_b == SLOT_VALID_COMPATIBLE);

    if (r.slot_a_valid)
        r.sequence_a = buf_a ? le_read32(buf_a + 8) : 0;
    if (r.slot_b_valid)
        r.sequence_b = buf_b ? le_read32(buf_b + 8) : 0;

    if (r.slot_a_valid && r.slot_b_valid) {
        SequenceOrder order = compare_sequence(r.sequence_a, r.sequence_b);
        if (order == SEQUENCE_EQUAL) {
            bool identical = size_a == size_b &&
                memcmp(buf_a, buf_b, (size_t)(size_a - 1u)) == 0;
            if (identical) {
                r.selected_slot = 0;
                r.status = SLOT_SELECTION_SELECTED;
            } else {
                r.status = SLOT_SELECTION_SEQUENCE_CONFLICT;
            }
        } else if (order == SEQUENCE_AMBIGUOUS) {
            r.status = SLOT_SELECTION_SEQUENCE_CONFLICT;
        } else {
            r.selected_slot = (uint8_t)(order == SEQUENCE_A_NEWER ? 0u : 1u);
            r.status = SLOT_SELECTION_SELECTED;
        }
    } else if (r.slot_a_valid && !r.slot_b_valid) {
        r.selected_slot = 0;
        r.status = SLOT_SELECTION_SELECTED;
    } else if (!r.slot_a_valid && r.slot_b_valid) {
        r.selected_slot = 1;
        r.status = SLOT_SELECTION_SELECTED;
    }

    return r;
}


uint8_t ab_slot_choose_target(const SlotSelectionResult *selection)
{
    if (!selection ||
        selection->status == SLOT_SELECTION_SEQUENCE_CONFLICT ||
        slot_reason_is_protected(selection->reason_a) ||
        slot_reason_is_protected(selection->reason_b))
        return SLOT_INDEX_NONE;

    if (selection->status == SLOT_SELECTION_SELECTED)
        return selection->selected_slot == 0u ? 1u : 0u;

    return 0u;
}
