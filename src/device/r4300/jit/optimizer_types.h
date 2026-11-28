#ifndef N64EMUJIT_OPTIMIZER_TYPES_H
#define N64EMUJIT_OPTIMIZER_TYPES_H
struct value_expression { unsigned op, a, b, immediate, value, producer; };
struct region_analysis {
    unsigned next[34][33];
    // Expression history is used only while emitting the straight-line path.
    // Precise exits resume existing code and need no copy of this history.
    uint64_t expression_slots;
    value_expression expressions[64];
};
#endif
