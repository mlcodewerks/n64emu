/* Restore executable protection if a C++ assembler rejects a block.
 * Compilation runs with all generated frames unwound. GPL-2.0-or-later. */
struct code_write_scope {
    void* memory;
    size_t size;
    bool published = false;
    code_write_scope(void* memory_,size_t size_) : memory(memory_),size(size_) {}
    ~code_write_scope() { if(!published) code_protect(memory,size,1); }
};
