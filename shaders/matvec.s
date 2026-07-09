    .text
    .globl matvec
    .p2align 8
    .type matvec,@function
matvec:
    s_lshl_b32     s14, s13, 6
    v_add_i32_e32  v1, vcc, s14, v0
    v_mul_lo_u32   v2, v1, s12
    v_mov_b32_e32  v3, 0
    s_mov_b32      s15, 0
loop:
    v_add_i32_e32  v4, vcc, s15, v2
    v_lshlrev_b32_e32 v4, 2, v4
    buffer_load_dword v5, v4, s[0:3], 0 offen
    s_lshl_b32     s16, s15, 2
    v_mov_b32_e32  v6, s16
    buffer_load_dword v7, v6, s[4:7], 0 offen
    s_waitcnt      vmcnt(0)
    v_mac_f32_e32  v3, v5, v7
    s_add_u32      s15, s15, 1
    s_cmp_lt_u32   s15, s12
    s_cbranch_scc1 loop
    v_lshlrev_b32_e32 v8, 2, v1
    buffer_store_dword v3, v8, s[8:11], 0 offen
    s_waitcnt      vmcnt(0) lgkmcnt(0)
    s_endpgm
    .size matvec, .-matvec
