#ifndef ARI64_OPTIMIZER_TYPES_H
#define ARI64_OPTIMIZER_TYPES_H
struct value_expression { unsigned op, a, b, immediate, value; };
struct region_analysis {
    unsigned next[34][33];
    // Expression history is used only while emitting the straight-line path.
    // Precise exits resume existing code and need no copy of this history.
    unsigned expression_count;
    value_expression expressions[32];
};
#endif
