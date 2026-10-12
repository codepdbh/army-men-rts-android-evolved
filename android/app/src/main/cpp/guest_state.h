#pragma once

// Included after recomp_types.h in the runtime, never in generated C.
struct MachineState {
    uint32_t registers[8], flags[4], fs, gs, current, mxcsr;
    uint16_t segments[6], cw;
    double st[8]; int64_t stIntegers[8]; int fpTop;
    uint64_t mm[8]; V128 xmm[8];
    MachineState() {
        uint32_t regs[] = {g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp};
        std::memcpy(registers, regs, sizeof regs);
        uint32_t arithmetic[] = {g_flag_k,g_flag_a,g_flag_b,g_flag_cf};
        std::memcpy(flags, arithmetic, sizeof arithmetic);
        uint16_t segs[] = {g_seg_cs,g_seg_ds,g_seg_es,g_seg_fs,g_seg_gs,g_seg_ss};
        std::memcpy(segments, segs, sizeof segs);
        fs=g_fs_base; gs=g_gs_base; current=g_cur_func; mxcsr=g_mxcsr; cw=g_fpu_cw; fpTop=g_fp_top;
        std::memcpy(st,g_st,sizeof st); std::memcpy(stIntegers,g_st_i64,sizeof stIntegers);
        std::memcpy(mm,g_mm,sizeof mm); std::memcpy(xmm,g_xmm,sizeof xmm);
    }
    void restore() const {
        g_eax=registers[0];g_ecx=registers[1];g_edx=registers[2];g_esp=registers[3];
        g_ebx=registers[4];g_esi=registers[5];g_edi=registers[6];g_ebp=registers[7];
        g_flag_k=flags[0];g_flag_a=flags[1];g_flag_b=flags[2];g_flag_cf=flags[3];
        g_seg_cs=segments[0];g_seg_ds=segments[1];g_seg_es=segments[2];g_seg_fs=segments[3];g_seg_gs=segments[4];g_seg_ss=segments[5];
        g_fs_base=fs;g_gs_base=gs;g_cur_func=current;g_mxcsr=mxcsr;g_fpu_cw=cw;g_fp_top=fpTop;
        std::memcpy(g_st,st,sizeof st);std::memcpy(g_st_i64,stIntegers,sizeof stIntegers);
        std::memcpy(g_mm,mm,sizeof mm);std::memcpy(g_xmm,xmm,sizeof xmm);
    }
};
