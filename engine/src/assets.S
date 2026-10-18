/* The slice blob (tools/slice.py output) linked into the ELF. */
    .section .rodata
    .balign 64
    .global slice_blob
slice_blob:
    .incbin "data/slice.bin"
    .global slice_blob_end
slice_blob_end:

/* IOP modules for audio (audio.c), from the SDK (build.sh copies them) */
    .balign 16
    .global libsd_irx
libsd_irx:
    .incbin "data/libsd.irx"
    .global libsd_irx_end
libsd_irx_end:
    .balign 16
    .global audsrv_irx
audsrv_irx:
    .incbin "data/audsrv.irx"
    .global audsrv_irx_end
audsrv_irx_end:
    .balign 16
    .global sndvoice_irx
sndvoice_irx:
    .incbin "data/sndvoice.irx"
    .global sndvoice_irx_end
sndvoice_irx_end:

/* The memory card save's browser icon (tools/ps2icon.py; mc.c writes them) */
    .balign 64
    .global save_icn
save_icn:
    .incbin "src/save.icn"
    .global save_icn_end
save_icn_end:
    .balign 64
    .global icon_sys
icon_sys:
    .incbin "src/icon.sys"
    .global icon_sys_end
icon_sys_end:
