/* The slice blob (tools/slice.py output) linked into the ELF. */
    .section .rodata
    .balign 64
    .global slice_blob
slice_blob:
    .incbin "data/slice.bin"
    .global slice_blob_end
slice_blob_end:
