	.text
	.amdgcn_target "amdgcn-amd-amdhsa--gfx700"
	.hidden	vadd                            ; -- Begin function vadd
	.globl	vadd
	.p2align	2
	.type	vadd,@function
vadd:                                   ; @vadd
; %bb.0:
	s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)
	s_lshl_b32 s4, s12, 6
	v_and_b32_e32 v6, 0x3ff, v31
	v_add_i32_e32 v6, vcc, s4, v6
	v_mov_b32_e32 v7, 0
	v_lshl_b64 v[6:7], v[6:7], 2
	v_add_i32_e32 v0, vcc, v0, v6
	v_addc_u32_e32 v1, vcc, v1, v7, vcc
	flat_load_dword v8, v[0:1]
	v_add_i32_e32 v0, vcc, v2, v6
	v_addc_u32_e32 v1, vcc, v3, v7, vcc
	flat_load_dword v0, v[0:1]
	s_waitcnt vmcnt(0) lgkmcnt(0)
	v_add_f32_e32 v2, v8, v0
	v_add_i32_e32 v0, vcc, v4, v6
	v_addc_u32_e32 v1, vcc, v5, v7, vcc
	flat_store_dword v[0:1], v2
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_setpc_b64 s[30:31]
.Lfunc_end0:
	.size	vadd, .Lfunc_end0-vadd
                                        ; -- End function
	.section	.AMDGPU.csdata
; Function info:
; codeLenInByte = 96
; NumSgprs: 34
; NumVgprs: 32
; ScratchSize: 0
; MemoryBound: 0
	.ident	"Ubuntu clang version 14.0.6"
	.section	".note.GNU-stack"
	.addrsig
	.amdgpu_metadata
---
amdhsa.kernels:  []
amdhsa.target:   amdgcn-amd-amdhsa--gfx700
amdhsa.version:
  - 1
  - 1
...

	.end_amdgpu_metadata
