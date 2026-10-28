// Generated helper declarations and immutable resolver table.
#include "rsp_op.hpp"
#include "simd_caps.hpp"
#include <cstdlib>
#include <cstring>
unsigned rsp_simd_runtime_level()
{
    static const unsigned level = [] {
        __builtin_cpu_init();
        unsigned actual = __builtin_cpu_supports("sse4.2") ? 42 : __builtin_cpu_supports("sse4.1") ? 41 : __builtin_cpu_supports("ssse3") ? 3 : 2;
        // Diagnostics may lower the ISA, never request unsupported instructions.
        const char *limit = std::getenv("PARALLAX_RSP_SIMD");
        unsigned requested = !limit ? actual : !std::strcmp(limit,"sse2") ? 2 : !std::strcmp(limit,"ssse3") ? 3 : !std::strcmp(limit,"sse41") ? 41 : actual;
        return requested < actual ? requested : actual;
    }();
    return level;
}
extern "C" {
decltype(RSP_LBV) RSP_LBV_ssse3;
decltype(RSP_LBV) RSP_LBV_sse41;
decltype(RSP_LBV) RSP_LBV_sse42;
decltype(RSP_LSV) RSP_LSV_ssse3;
decltype(RSP_LSV) RSP_LSV_sse41;
decltype(RSP_LSV) RSP_LSV_sse42;
decltype(RSP_LLV) RSP_LLV_ssse3;
decltype(RSP_LLV) RSP_LLV_sse41;
decltype(RSP_LLV) RSP_LLV_sse42;
decltype(RSP_LDV) RSP_LDV_ssse3;
decltype(RSP_LDV) RSP_LDV_sse41;
decltype(RSP_LDV) RSP_LDV_sse42;
decltype(RSP_LQV) RSP_LQV_ssse3;
decltype(RSP_LQV) RSP_LQV_sse41;
decltype(RSP_LQV) RSP_LQV_sse42;
decltype(RSP_LRV) RSP_LRV_ssse3;
decltype(RSP_LRV) RSP_LRV_sse41;
decltype(RSP_LRV) RSP_LRV_sse42;
decltype(RSP_LPV) RSP_LPV_ssse3;
decltype(RSP_LPV) RSP_LPV_sse41;
decltype(RSP_LPV) RSP_LPV_sse42;
decltype(RSP_LUV) RSP_LUV_ssse3;
decltype(RSP_LUV) RSP_LUV_sse41;
decltype(RSP_LUV) RSP_LUV_sse42;
decltype(RSP_LHV) RSP_LHV_ssse3;
decltype(RSP_LHV) RSP_LHV_sse41;
decltype(RSP_LHV) RSP_LHV_sse42;
decltype(RSP_LFV) RSP_LFV_ssse3;
decltype(RSP_LFV) RSP_LFV_sse41;
decltype(RSP_LFV) RSP_LFV_sse42;
decltype(RSP_LTV) RSP_LTV_ssse3;
decltype(RSP_LTV) RSP_LTV_sse41;
decltype(RSP_LTV) RSP_LTV_sse42;
decltype(RSP_SBV) RSP_SBV_ssse3;
decltype(RSP_SBV) RSP_SBV_sse41;
decltype(RSP_SBV) RSP_SBV_sse42;
decltype(RSP_SSV) RSP_SSV_ssse3;
decltype(RSP_SSV) RSP_SSV_sse41;
decltype(RSP_SSV) RSP_SSV_sse42;
decltype(RSP_SLV) RSP_SLV_ssse3;
decltype(RSP_SLV) RSP_SLV_sse41;
decltype(RSP_SLV) RSP_SLV_sse42;
decltype(RSP_SDV) RSP_SDV_ssse3;
decltype(RSP_SDV) RSP_SDV_sse41;
decltype(RSP_SDV) RSP_SDV_sse42;
decltype(RSP_SQV) RSP_SQV_ssse3;
decltype(RSP_SQV) RSP_SQV_sse41;
decltype(RSP_SQV) RSP_SQV_sse42;
decltype(RSP_SRV) RSP_SRV_ssse3;
decltype(RSP_SRV) RSP_SRV_sse41;
decltype(RSP_SRV) RSP_SRV_sse42;
decltype(RSP_SPV) RSP_SPV_ssse3;
decltype(RSP_SPV) RSP_SPV_sse41;
decltype(RSP_SPV) RSP_SPV_sse42;
decltype(RSP_SUV) RSP_SUV_ssse3;
decltype(RSP_SUV) RSP_SUV_sse41;
decltype(RSP_SUV) RSP_SUV_sse42;
decltype(RSP_SHV) RSP_SHV_ssse3;
decltype(RSP_SHV) RSP_SHV_sse41;
decltype(RSP_SHV) RSP_SHV_sse42;
decltype(RSP_SFV) RSP_SFV_ssse3;
decltype(RSP_SFV) RSP_SFV_sse41;
decltype(RSP_SFV) RSP_SFV_sse42;
decltype(RSP_SWV) RSP_SWV_ssse3;
decltype(RSP_SWV) RSP_SWV_sse41;
decltype(RSP_SWV) RSP_SWV_sse42;
decltype(RSP_STV) RSP_STV_ssse3;
decltype(RSP_STV) RSP_STV_sse41;
decltype(RSP_STV) RSP_STV_sse42;
decltype(RSP_VMULF) RSP_VMULF_ssse3;
decltype(RSP_VMULF) RSP_VMULF_sse41;
decltype(RSP_VMULF) RSP_VMULF_sse42;
decltype(RSP_VMULU) RSP_VMULU_ssse3;
decltype(RSP_VMULU) RSP_VMULU_sse41;
decltype(RSP_VMULU) RSP_VMULU_sse42;
decltype(RSP_VMUDL) RSP_VMUDL_ssse3;
decltype(RSP_VMUDL) RSP_VMUDL_sse41;
decltype(RSP_VMUDL) RSP_VMUDL_sse42;
decltype(RSP_VMUDM) RSP_VMUDM_ssse3;
decltype(RSP_VMUDM) RSP_VMUDM_sse41;
decltype(RSP_VMUDM) RSP_VMUDM_sse42;
decltype(RSP_VMUDN) RSP_VMUDN_ssse3;
decltype(RSP_VMUDN) RSP_VMUDN_sse41;
decltype(RSP_VMUDN) RSP_VMUDN_sse42;
decltype(RSP_VMUDH) RSP_VMUDH_ssse3;
decltype(RSP_VMUDH) RSP_VMUDH_sse41;
decltype(RSP_VMUDH) RSP_VMUDH_sse42;
decltype(RSP_VMACF) RSP_VMACF_ssse3;
decltype(RSP_VMACF) RSP_VMACF_sse41;
decltype(RSP_VMACF) RSP_VMACF_sse42;
decltype(RSP_VMACU) RSP_VMACU_ssse3;
decltype(RSP_VMACU) RSP_VMACU_sse41;
decltype(RSP_VMACU) RSP_VMACU_sse42;
decltype(RSP_VMADL) RSP_VMADL_ssse3;
decltype(RSP_VMADL) RSP_VMADL_sse41;
decltype(RSP_VMADL) RSP_VMADL_sse42;
decltype(RSP_VMADM) RSP_VMADM_ssse3;
decltype(RSP_VMADM) RSP_VMADM_sse41;
decltype(RSP_VMADM) RSP_VMADM_sse42;
decltype(RSP_VMADN) RSP_VMADN_ssse3;
decltype(RSP_VMADN) RSP_VMADN_sse41;
decltype(RSP_VMADN) RSP_VMADN_sse42;
decltype(RSP_VMADH) RSP_VMADH_ssse3;
decltype(RSP_VMADH) RSP_VMADH_sse41;
decltype(RSP_VMADH) RSP_VMADH_sse42;
decltype(RSP_VADD) RSP_VADD_ssse3;
decltype(RSP_VADD) RSP_VADD_sse41;
decltype(RSP_VADD) RSP_VADD_sse42;
decltype(RSP_VSUB) RSP_VSUB_ssse3;
decltype(RSP_VSUB) RSP_VSUB_sse41;
decltype(RSP_VSUB) RSP_VSUB_sse42;
decltype(RSP_VABS) RSP_VABS_ssse3;
decltype(RSP_VABS) RSP_VABS_sse41;
decltype(RSP_VABS) RSP_VABS_sse42;
decltype(RSP_VADDC) RSP_VADDC_ssse3;
decltype(RSP_VADDC) RSP_VADDC_sse41;
decltype(RSP_VADDC) RSP_VADDC_sse42;
decltype(RSP_VSUBC) RSP_VSUBC_ssse3;
decltype(RSP_VSUBC) RSP_VSUBC_sse41;
decltype(RSP_VSUBC) RSP_VSUBC_sse42;
decltype(RSP_VSAR) RSP_VSAR_ssse3;
decltype(RSP_VSAR) RSP_VSAR_sse41;
decltype(RSP_VSAR) RSP_VSAR_sse42;
decltype(RSP_VLT) RSP_VLT_ssse3;
decltype(RSP_VLT) RSP_VLT_sse41;
decltype(RSP_VLT) RSP_VLT_sse42;
decltype(RSP_VEQ) RSP_VEQ_ssse3;
decltype(RSP_VEQ) RSP_VEQ_sse41;
decltype(RSP_VEQ) RSP_VEQ_sse42;
decltype(RSP_VNE) RSP_VNE_ssse3;
decltype(RSP_VNE) RSP_VNE_sse41;
decltype(RSP_VNE) RSP_VNE_sse42;
decltype(RSP_VGE) RSP_VGE_ssse3;
decltype(RSP_VGE) RSP_VGE_sse41;
decltype(RSP_VGE) RSP_VGE_sse42;
decltype(RSP_VCL) RSP_VCL_ssse3;
decltype(RSP_VCL) RSP_VCL_sse41;
decltype(RSP_VCL) RSP_VCL_sse42;
decltype(RSP_VCH) RSP_VCH_ssse3;
decltype(RSP_VCH) RSP_VCH_sse41;
decltype(RSP_VCH) RSP_VCH_sse42;
decltype(RSP_VCR) RSP_VCR_ssse3;
decltype(RSP_VCR) RSP_VCR_sse41;
decltype(RSP_VCR) RSP_VCR_sse42;
decltype(RSP_VMRG) RSP_VMRG_ssse3;
decltype(RSP_VMRG) RSP_VMRG_sse41;
decltype(RSP_VMRG) RSP_VMRG_sse42;
decltype(RSP_VAND) RSP_VAND_ssse3;
decltype(RSP_VAND) RSP_VAND_sse41;
decltype(RSP_VAND) RSP_VAND_sse42;
decltype(RSP_VNAND) RSP_VNAND_ssse3;
decltype(RSP_VNAND) RSP_VNAND_sse41;
decltype(RSP_VNAND) RSP_VNAND_sse42;
decltype(RSP_VOR) RSP_VOR_ssse3;
decltype(RSP_VOR) RSP_VOR_sse41;
decltype(RSP_VOR) RSP_VOR_sse42;
decltype(RSP_VNOR) RSP_VNOR_ssse3;
decltype(RSP_VNOR) RSP_VNOR_sse41;
decltype(RSP_VNOR) RSP_VNOR_sse42;
decltype(RSP_VXOR) RSP_VXOR_ssse3;
decltype(RSP_VXOR) RSP_VXOR_sse41;
decltype(RSP_VXOR) RSP_VXOR_sse42;
decltype(RSP_VNXOR) RSP_VNXOR_ssse3;
decltype(RSP_VNXOR) RSP_VNXOR_sse41;
decltype(RSP_VNXOR) RSP_VNXOR_sse42;
decltype(RSP_VRCP) RSP_VRCP_ssse3;
decltype(RSP_VRCP) RSP_VRCP_sse41;
decltype(RSP_VRCP) RSP_VRCP_sse42;
decltype(RSP_VRCPL) RSP_VRCPL_ssse3;
decltype(RSP_VRCPL) RSP_VRCPL_sse41;
decltype(RSP_VRCPL) RSP_VRCPL_sse42;
decltype(RSP_VRCPH) RSP_VRCPH_ssse3;
decltype(RSP_VRCPH) RSP_VRCPH_sse41;
decltype(RSP_VRCPH) RSP_VRCPH_sse42;
decltype(RSP_VMOV) RSP_VMOV_ssse3;
decltype(RSP_VMOV) RSP_VMOV_sse41;
decltype(RSP_VMOV) RSP_VMOV_sse42;
decltype(RSP_VRSQ) RSP_VRSQ_ssse3;
decltype(RSP_VRSQ) RSP_VRSQ_sse41;
decltype(RSP_VRSQ) RSP_VRSQ_sse42;
decltype(RSP_VRSQL) RSP_VRSQL_ssse3;
decltype(RSP_VRSQL) RSP_VRSQL_sse41;
decltype(RSP_VRSQL) RSP_VRSQL_sse42;
decltype(RSP_VRSQH) RSP_VRSQH_ssse3;
decltype(RSP_VRSQH) RSP_VRSQH_sse41;
decltype(RSP_VRSQH) RSP_VRSQH_sse42;
decltype(RSP_VNOP) RSP_VNOP_ssse3;
decltype(RSP_VNOP) RSP_VNOP_sse41;
decltype(RSP_VNOP) RSP_VNOP_sse42;
decltype(RSP_RESERVED) RSP_RESERVED_ssse3;
decltype(RSP_RESERVED) RSP_RESERVED_sse41;
decltype(RSP_RESERVED) RSP_RESERVED_sse42;
}
void *rsp_resolve_simd_helper(void *pointer)
{
    const unsigned level = rsp_simd_level();
    if (level < 3) return pointer;
    struct Entry { void *base, *ssse3, *sse41, *sse42; };
    static const Entry entries[] = {
        {reinterpret_cast<void *>(RSP_LBV), reinterpret_cast<void *>(RSP_LBV_ssse3), reinterpret_cast<void *>(RSP_LBV_sse41), reinterpret_cast<void *>(RSP_LBV_sse42)},
        {reinterpret_cast<void *>(RSP_LSV), reinterpret_cast<void *>(RSP_LSV_ssse3), reinterpret_cast<void *>(RSP_LSV_sse41), reinterpret_cast<void *>(RSP_LSV_sse42)},
        {reinterpret_cast<void *>(RSP_LLV), reinterpret_cast<void *>(RSP_LLV_ssse3), reinterpret_cast<void *>(RSP_LLV_sse41), reinterpret_cast<void *>(RSP_LLV_sse42)},
        {reinterpret_cast<void *>(RSP_LDV), reinterpret_cast<void *>(RSP_LDV_ssse3), reinterpret_cast<void *>(RSP_LDV_sse41), reinterpret_cast<void *>(RSP_LDV_sse42)},
        {reinterpret_cast<void *>(RSP_LQV), reinterpret_cast<void *>(RSP_LQV_ssse3), reinterpret_cast<void *>(RSP_LQV_sse41), reinterpret_cast<void *>(RSP_LQV_sse42)},
        {reinterpret_cast<void *>(RSP_LRV), reinterpret_cast<void *>(RSP_LRV_ssse3), reinterpret_cast<void *>(RSP_LRV_sse41), reinterpret_cast<void *>(RSP_LRV_sse42)},
        {reinterpret_cast<void *>(RSP_LPV), reinterpret_cast<void *>(RSP_LPV_ssse3), reinterpret_cast<void *>(RSP_LPV_sse41), reinterpret_cast<void *>(RSP_LPV_sse42)},
        {reinterpret_cast<void *>(RSP_LUV), reinterpret_cast<void *>(RSP_LUV_ssse3), reinterpret_cast<void *>(RSP_LUV_sse41), reinterpret_cast<void *>(RSP_LUV_sse42)},
        {reinterpret_cast<void *>(RSP_LHV), reinterpret_cast<void *>(RSP_LHV_ssse3), reinterpret_cast<void *>(RSP_LHV_sse41), reinterpret_cast<void *>(RSP_LHV_sse42)},
        {reinterpret_cast<void *>(RSP_LFV), reinterpret_cast<void *>(RSP_LFV_ssse3), reinterpret_cast<void *>(RSP_LFV_sse41), reinterpret_cast<void *>(RSP_LFV_sse42)},
        {reinterpret_cast<void *>(RSP_LTV), reinterpret_cast<void *>(RSP_LTV_ssse3), reinterpret_cast<void *>(RSP_LTV_sse41), reinterpret_cast<void *>(RSP_LTV_sse42)},
        {reinterpret_cast<void *>(RSP_SBV), reinterpret_cast<void *>(RSP_SBV_ssse3), reinterpret_cast<void *>(RSP_SBV_sse41), reinterpret_cast<void *>(RSP_SBV_sse42)},
        {reinterpret_cast<void *>(RSP_SSV), reinterpret_cast<void *>(RSP_SSV_ssse3), reinterpret_cast<void *>(RSP_SSV_sse41), reinterpret_cast<void *>(RSP_SSV_sse42)},
        {reinterpret_cast<void *>(RSP_SLV), reinterpret_cast<void *>(RSP_SLV_ssse3), reinterpret_cast<void *>(RSP_SLV_sse41), reinterpret_cast<void *>(RSP_SLV_sse42)},
        {reinterpret_cast<void *>(RSP_SDV), reinterpret_cast<void *>(RSP_SDV_ssse3), reinterpret_cast<void *>(RSP_SDV_sse41), reinterpret_cast<void *>(RSP_SDV_sse42)},
        {reinterpret_cast<void *>(RSP_SQV), reinterpret_cast<void *>(RSP_SQV_ssse3), reinterpret_cast<void *>(RSP_SQV_sse41), reinterpret_cast<void *>(RSP_SQV_sse42)},
        {reinterpret_cast<void *>(RSP_SRV), reinterpret_cast<void *>(RSP_SRV_ssse3), reinterpret_cast<void *>(RSP_SRV_sse41), reinterpret_cast<void *>(RSP_SRV_sse42)},
        {reinterpret_cast<void *>(RSP_SPV), reinterpret_cast<void *>(RSP_SPV_ssse3), reinterpret_cast<void *>(RSP_SPV_sse41), reinterpret_cast<void *>(RSP_SPV_sse42)},
        {reinterpret_cast<void *>(RSP_SUV), reinterpret_cast<void *>(RSP_SUV_ssse3), reinterpret_cast<void *>(RSP_SUV_sse41), reinterpret_cast<void *>(RSP_SUV_sse42)},
        {reinterpret_cast<void *>(RSP_SHV), reinterpret_cast<void *>(RSP_SHV_ssse3), reinterpret_cast<void *>(RSP_SHV_sse41), reinterpret_cast<void *>(RSP_SHV_sse42)},
        {reinterpret_cast<void *>(RSP_SFV), reinterpret_cast<void *>(RSP_SFV_ssse3), reinterpret_cast<void *>(RSP_SFV_sse41), reinterpret_cast<void *>(RSP_SFV_sse42)},
        {reinterpret_cast<void *>(RSP_SWV), reinterpret_cast<void *>(RSP_SWV_ssse3), reinterpret_cast<void *>(RSP_SWV_sse41), reinterpret_cast<void *>(RSP_SWV_sse42)},
        {reinterpret_cast<void *>(RSP_STV), reinterpret_cast<void *>(RSP_STV_ssse3), reinterpret_cast<void *>(RSP_STV_sse41), reinterpret_cast<void *>(RSP_STV_sse42)},
        {reinterpret_cast<void *>(RSP_VMULF), reinterpret_cast<void *>(RSP_VMULF_ssse3), reinterpret_cast<void *>(RSP_VMULF_sse41), reinterpret_cast<void *>(RSP_VMULF_sse42)},
        {reinterpret_cast<void *>(RSP_VMULU), reinterpret_cast<void *>(RSP_VMULU_ssse3), reinterpret_cast<void *>(RSP_VMULU_sse41), reinterpret_cast<void *>(RSP_VMULU_sse42)},
        {reinterpret_cast<void *>(RSP_VMUDL), reinterpret_cast<void *>(RSP_VMUDL_ssse3), reinterpret_cast<void *>(RSP_VMUDL_sse41), reinterpret_cast<void *>(RSP_VMUDL_sse42)},
        {reinterpret_cast<void *>(RSP_VMUDM), reinterpret_cast<void *>(RSP_VMUDM_ssse3), reinterpret_cast<void *>(RSP_VMUDM_sse41), reinterpret_cast<void *>(RSP_VMUDM_sse42)},
        {reinterpret_cast<void *>(RSP_VMUDN), reinterpret_cast<void *>(RSP_VMUDN_ssse3), reinterpret_cast<void *>(RSP_VMUDN_sse41), reinterpret_cast<void *>(RSP_VMUDN_sse42)},
        {reinterpret_cast<void *>(RSP_VMUDH), reinterpret_cast<void *>(RSP_VMUDH_ssse3), reinterpret_cast<void *>(RSP_VMUDH_sse41), reinterpret_cast<void *>(RSP_VMUDH_sse42)},
        {reinterpret_cast<void *>(RSP_VMACF), reinterpret_cast<void *>(RSP_VMACF_ssse3), reinterpret_cast<void *>(RSP_VMACF_sse41), reinterpret_cast<void *>(RSP_VMACF_sse42)},
        {reinterpret_cast<void *>(RSP_VMACU), reinterpret_cast<void *>(RSP_VMACU_ssse3), reinterpret_cast<void *>(RSP_VMACU_sse41), reinterpret_cast<void *>(RSP_VMACU_sse42)},
        {reinterpret_cast<void *>(RSP_VMADL), reinterpret_cast<void *>(RSP_VMADL_ssse3), reinterpret_cast<void *>(RSP_VMADL_sse41), reinterpret_cast<void *>(RSP_VMADL_sse42)},
        {reinterpret_cast<void *>(RSP_VMADM), reinterpret_cast<void *>(RSP_VMADM_ssse3), reinterpret_cast<void *>(RSP_VMADM_sse41), reinterpret_cast<void *>(RSP_VMADM_sse42)},
        {reinterpret_cast<void *>(RSP_VMADN), reinterpret_cast<void *>(RSP_VMADN_ssse3), reinterpret_cast<void *>(RSP_VMADN_sse41), reinterpret_cast<void *>(RSP_VMADN_sse42)},
        {reinterpret_cast<void *>(RSP_VMADH), reinterpret_cast<void *>(RSP_VMADH_ssse3), reinterpret_cast<void *>(RSP_VMADH_sse41), reinterpret_cast<void *>(RSP_VMADH_sse42)},
        {reinterpret_cast<void *>(RSP_VADD), reinterpret_cast<void *>(RSP_VADD_ssse3), reinterpret_cast<void *>(RSP_VADD_sse41), reinterpret_cast<void *>(RSP_VADD_sse42)},
        {reinterpret_cast<void *>(RSP_VSUB), reinterpret_cast<void *>(RSP_VSUB_ssse3), reinterpret_cast<void *>(RSP_VSUB_sse41), reinterpret_cast<void *>(RSP_VSUB_sse42)},
        {reinterpret_cast<void *>(RSP_VABS), reinterpret_cast<void *>(RSP_VABS_ssse3), reinterpret_cast<void *>(RSP_VABS_sse41), reinterpret_cast<void *>(RSP_VABS_sse42)},
        {reinterpret_cast<void *>(RSP_VADDC), reinterpret_cast<void *>(RSP_VADDC_ssse3), reinterpret_cast<void *>(RSP_VADDC_sse41), reinterpret_cast<void *>(RSP_VADDC_sse42)},
        {reinterpret_cast<void *>(RSP_VSUBC), reinterpret_cast<void *>(RSP_VSUBC_ssse3), reinterpret_cast<void *>(RSP_VSUBC_sse41), reinterpret_cast<void *>(RSP_VSUBC_sse42)},
        {reinterpret_cast<void *>(RSP_VSAR), reinterpret_cast<void *>(RSP_VSAR_ssse3), reinterpret_cast<void *>(RSP_VSAR_sse41), reinterpret_cast<void *>(RSP_VSAR_sse42)},
        {reinterpret_cast<void *>(RSP_VLT), reinterpret_cast<void *>(RSP_VLT_ssse3), reinterpret_cast<void *>(RSP_VLT_sse41), reinterpret_cast<void *>(RSP_VLT_sse42)},
        {reinterpret_cast<void *>(RSP_VEQ), reinterpret_cast<void *>(RSP_VEQ_ssse3), reinterpret_cast<void *>(RSP_VEQ_sse41), reinterpret_cast<void *>(RSP_VEQ_sse42)},
        {reinterpret_cast<void *>(RSP_VNE), reinterpret_cast<void *>(RSP_VNE_ssse3), reinterpret_cast<void *>(RSP_VNE_sse41), reinterpret_cast<void *>(RSP_VNE_sse42)},
        {reinterpret_cast<void *>(RSP_VGE), reinterpret_cast<void *>(RSP_VGE_ssse3), reinterpret_cast<void *>(RSP_VGE_sse41), reinterpret_cast<void *>(RSP_VGE_sse42)},
        {reinterpret_cast<void *>(RSP_VCL), reinterpret_cast<void *>(RSP_VCL_ssse3), reinterpret_cast<void *>(RSP_VCL_sse41), reinterpret_cast<void *>(RSP_VCL_sse42)},
        {reinterpret_cast<void *>(RSP_VCH), reinterpret_cast<void *>(RSP_VCH_ssse3), reinterpret_cast<void *>(RSP_VCH_sse41), reinterpret_cast<void *>(RSP_VCH_sse42)},
        {reinterpret_cast<void *>(RSP_VCR), reinterpret_cast<void *>(RSP_VCR_ssse3), reinterpret_cast<void *>(RSP_VCR_sse41), reinterpret_cast<void *>(RSP_VCR_sse42)},
        {reinterpret_cast<void *>(RSP_VMRG), reinterpret_cast<void *>(RSP_VMRG_ssse3), reinterpret_cast<void *>(RSP_VMRG_sse41), reinterpret_cast<void *>(RSP_VMRG_sse42)},
        {reinterpret_cast<void *>(RSP_VAND), reinterpret_cast<void *>(RSP_VAND_ssse3), reinterpret_cast<void *>(RSP_VAND_sse41), reinterpret_cast<void *>(RSP_VAND_sse42)},
        {reinterpret_cast<void *>(RSP_VNAND), reinterpret_cast<void *>(RSP_VNAND_ssse3), reinterpret_cast<void *>(RSP_VNAND_sse41), reinterpret_cast<void *>(RSP_VNAND_sse42)},
        {reinterpret_cast<void *>(RSP_VOR), reinterpret_cast<void *>(RSP_VOR_ssse3), reinterpret_cast<void *>(RSP_VOR_sse41), reinterpret_cast<void *>(RSP_VOR_sse42)},
        {reinterpret_cast<void *>(RSP_VNOR), reinterpret_cast<void *>(RSP_VNOR_ssse3), reinterpret_cast<void *>(RSP_VNOR_sse41), reinterpret_cast<void *>(RSP_VNOR_sse42)},
        {reinterpret_cast<void *>(RSP_VXOR), reinterpret_cast<void *>(RSP_VXOR_ssse3), reinterpret_cast<void *>(RSP_VXOR_sse41), reinterpret_cast<void *>(RSP_VXOR_sse42)},
        {reinterpret_cast<void *>(RSP_VNXOR), reinterpret_cast<void *>(RSP_VNXOR_ssse3), reinterpret_cast<void *>(RSP_VNXOR_sse41), reinterpret_cast<void *>(RSP_VNXOR_sse42)},
        {reinterpret_cast<void *>(RSP_VRCP), reinterpret_cast<void *>(RSP_VRCP_ssse3), reinterpret_cast<void *>(RSP_VRCP_sse41), reinterpret_cast<void *>(RSP_VRCP_sse42)},
        {reinterpret_cast<void *>(RSP_VRCPL), reinterpret_cast<void *>(RSP_VRCPL_ssse3), reinterpret_cast<void *>(RSP_VRCPL_sse41), reinterpret_cast<void *>(RSP_VRCPL_sse42)},
        {reinterpret_cast<void *>(RSP_VRCPH), reinterpret_cast<void *>(RSP_VRCPH_ssse3), reinterpret_cast<void *>(RSP_VRCPH_sse41), reinterpret_cast<void *>(RSP_VRCPH_sse42)},
        {reinterpret_cast<void *>(RSP_VMOV), reinterpret_cast<void *>(RSP_VMOV_ssse3), reinterpret_cast<void *>(RSP_VMOV_sse41), reinterpret_cast<void *>(RSP_VMOV_sse42)},
        {reinterpret_cast<void *>(RSP_VRSQ), reinterpret_cast<void *>(RSP_VRSQ_ssse3), reinterpret_cast<void *>(RSP_VRSQ_sse41), reinterpret_cast<void *>(RSP_VRSQ_sse42)},
        {reinterpret_cast<void *>(RSP_VRSQL), reinterpret_cast<void *>(RSP_VRSQL_ssse3), reinterpret_cast<void *>(RSP_VRSQL_sse41), reinterpret_cast<void *>(RSP_VRSQL_sse42)},
        {reinterpret_cast<void *>(RSP_VRSQH), reinterpret_cast<void *>(RSP_VRSQH_ssse3), reinterpret_cast<void *>(RSP_VRSQH_sse41), reinterpret_cast<void *>(RSP_VRSQH_sse42)},
        {reinterpret_cast<void *>(RSP_VNOP), reinterpret_cast<void *>(RSP_VNOP_ssse3), reinterpret_cast<void *>(RSP_VNOP_sse41), reinterpret_cast<void *>(RSP_VNOP_sse42)},
        {reinterpret_cast<void *>(RSP_RESERVED), reinterpret_cast<void *>(RSP_RESERVED_ssse3), reinterpret_cast<void *>(RSP_RESERVED_sse41), reinterpret_cast<void *>(RSP_RESERVED_sse42)},
    };
    for (const auto &entry : entries)
        if (entry.base == pointer) return level >= 42 ? entry.sse42 : level >= 41 ? entry.sse41 : entry.ssse3;
    return pointer;
}
