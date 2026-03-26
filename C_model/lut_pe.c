#include <stdio.h>
#include <stdlib.h>

// ==========================================
// 1. 架构参数与模式定义
// ==========================================
typedef enum {
    MODE_BITNET_G3,   // g=3, 纯三值 {-1, 0, 1}
    MODE_SHERRY_G4,   // g=4, 带稀疏的三值 (有且仅有1个0)
    MODE_TMAC_G6,     // g=6, 纯二值 {-1, 1} TMAC
    MODE_SPARSE_2_4   // g=4, 2:4稀疏模式 (任意2个非0) 
} PEMode;

// 片上物理寄存器 (12 个槽位)
typedef struct {
    float base_lut[4];
    float comp_lut[8];
} OnChipRegisters;

// ==========================================
// 2. Generation Unit (离线建表单元)
// ==========================================
void generation_unit(float A[], PEMode mode, OnChipRegisters *regs) {
    if (mode == MODE_BITNET_G3) {
        regs->base_lut[0] = A[0] + A[1] + A[2]; 
        regs->base_lut[1] = A[0] + A[1] - A[2]; 
        regs->base_lut[2] = A[0] - A[1] + A[2]; 
        regs->base_lut[3] = A[0] - A[1] - A[2]; 
        
        regs->comp_lut[0] = 0.0f;                           
        regs->comp_lut[1] = -A[0];                          
        regs->comp_lut[2] = -A[1];                          
        regs->comp_lut[3] = -(A[0] + A[1]);                 
        regs->comp_lut[4] = -A[2];                          
        regs->comp_lut[5] = -(A[0] + A[2]);                 
        regs->comp_lut[6] = -(A[1] + A[2]);                 
        regs->comp_lut[7] = -(A[0] + A[1] + A[2]);          
        
    } else if (mode == MODE_SHERRY_G4) {
        regs->base_lut[0] = A[0] + A[1] + A[2] + A[3]; 
        regs->base_lut[1] = A[0] + A[1] + A[2] - A[3]; 
        regs->base_lut[2] = A[0] + A[1] - A[2] + A[3]; 
        regs->base_lut[3] = A[0] + A[1] - A[2] - A[3]; 
        
        regs->comp_lut[0] = -A[0];
        regs->comp_lut[1] = -A[1];
        regs->comp_lut[2] = -A[2];
        regs->comp_lut[3] = -A[3];
        regs->comp_lut[4] = 2*A[0] - A[0]; 
        regs->comp_lut[5] = 2*A[0] - A[1];
        regs->comp_lut[6] = 2*A[0] - A[2];
        regs->comp_lut[7] = 2*A[0] - A[3];
        
    } else if (mode == MODE_TMAC_G6) {
        regs->base_lut[0] = A[0] + A[1] + A[2] + A[3];
        regs->base_lut[1] = A[0] + A[1] + A[2] - A[3];
        regs->base_lut[2] = A[0] + A[1] - A[2] + A[3];
        regs->base_lut[3] = A[0] + A[1] - A[2] - A[3];
        
        regs->comp_lut[0] = 0.0f   + A[4] + A[5]; 
        regs->comp_lut[1] = 0.0f   + A[4] - A[5]; 
        regs->comp_lut[2] = 0.0f   - A[4] + A[5]; 
        regs->comp_lut[3] = 0.0f   - A[4] - A[5]; 
        regs->comp_lut[4] = 2*A[0] + A[4] + A[5]; 
        regs->comp_lut[5] = 2*A[0] + A[4] - A[5]; 
        regs->comp_lut[6] = 2*A[0] - A[4] + A[5]; 
        regs->comp_lut[7] = 2*A[0] - A[4] - A[5]; 
        
    } else if (mode == MODE_SPARSE_2_4) {
        regs->base_lut[0] = A[0]; regs->base_lut[1] = A[1];
        regs->base_lut[2] = A[2]; regs->base_lut[3] = A[3];
        
        regs->comp_lut[0] =  A[0]; regs->comp_lut[1] = -A[0];
        regs->comp_lut[2] =  A[1]; regs->comp_lut[3] = -A[1];
        regs->comp_lut[4] =  A[2]; regs->comp_lut[5] = -A[2];
        regs->comp_lut[6] =  A[3]; regs->comp_lut[7] = -A[3];
    }
}

// ==========================================
// 3. Query & ALU (在线查表与执行单元)
// ==========================================
float execute_unit(int W[], PEMode mode, OnChipRegisters *regs) {
    float y_base = 0.0f, c_total = 0.0f;
    int sign_flip = 1; 
    int fold_flag = 0; 
    
    if (mode == MODE_BITNET_G3) {
        int pseudo_W[3];
        int zero_mask = 0; 
        for(int i=0; i<3; i++) {
            if (W[i] == 0) {
                pseudo_W[i] = 1;             
                zero_mask |= (1 << i);       
            } else {
                pseudo_W[i] = W[i];
            }
        }
        sign_flip = pseudo_W[0]; 
        int w1_norm = pseudo_W[1] * sign_flip;
        int w2_norm = pseudo_W[2] * sign_flip;
        int base_idx = ((w1_norm == -1) ? 2 : 0) + ((w2_norm == -1) ? 1 : 0);
        y_base = regs->base_lut[base_idx];
        c_total = regs->comp_lut[zero_mask];
        
    } else if (mode == MODE_SHERRY_G4 || mode == MODE_TMAC_G6) {
        int pseudo_W[4]; int zero_idx = -1;
        for(int i=0; i<4; i++) {
            if (W[i] == 0) { zero_idx = i; pseudo_W[i] = 1; } else pseudo_W[i] = W[i];
        }
        sign_flip = pseudo_W[0];
        int w1_norm = pseudo_W[1] * sign_flip, w2_norm = pseudo_W[2] * sign_flip, w3_norm = pseudo_W[3] * sign_flip;
        
        fold_flag = (w1_norm == -1) ? 1 : 0; // 记录是否发生折叠
        
        int w2_lookup = fold_flag ? -w2_norm : w2_norm;
        int w3_lookup = fold_flag ? -w3_norm : w3_norm;
        int base_idx = ((w2_lookup == -1) ? 2 : 0) + ((w3_lookup == -1) ? 1 : 0);
        y_base = regs->base_lut[base_idx];

        if (mode == MODE_TMAC_G6) {
            int c_idx = (fold_flag * 4) + ((W[4] == -1) ? 2 : 0) + ((W[5] == -1) ? 1 : 0);
            c_total = regs->comp_lut[c_idx];
        } else {
            c_total = (zero_idx != -1) ? regs->comp_lut[fold_flag * 4 + zero_idx] : 0.0f;
        }
        
    } else if (mode == MODE_SPARSE_2_4) {
        int active_idx[2], active_sign[2], cnt = 0;
        for(int i=0; i<4; i++) {
            if(W[i] != 0) { active_idx[cnt] = i; active_sign[cnt] = W[i]; cnt++; }
        }
        sign_flip = active_sign[0];
        y_base = regs->base_lut[active_idx[0]];
        c_total = regs->comp_lut[active_idx[1] * 2 + (active_sign[1] == -1 ? 1 : 0)];
    }
    
    // Sign Flip 模块由 Global Sign 和 Fold Flag 共同决定
    int final_sign = sign_flip * (fold_flag ? -1 : 1);
    
    return (final_sign * y_base) + c_total;
}

// ==========================================
// 4. Testbench 
// ==========================================
int main() {
    OnChipRegisters pe_regs;
    float A_full[6] = {5.0, 2.0, 4.0, 1.0, 3.0, 2.0};
    printf("=== LUT PE 架构验证 ===\n\n");

    // Test 1: BitNet g=3 
    int W_bitnet[3] = {-1, 0, 1}; // 内积: -5*1 + 0*2 + 1*4 = -1
    generation_unit(A_full, MODE_BITNET_G3, &pe_regs);
    float res1 = execute_unit(W_bitnet, MODE_BITNET_G3, &pe_regs);
    printf("[BitNet g=3] Ground Truth: -1.0 | 硬件输出: %.1f \n", res1);

    // Test 2: BitNet g=3 全0测试
    int W_bitnet_zero[3] = {0, 0, 0}; // 内积: 0
    float res1_zero = execute_unit(W_bitnet_zero, MODE_BITNET_G3, &pe_regs);
    printf("[BitNet g=3 全0] Ground Truth: 0.0 | 硬件输出: %.1f \n\n", res1_zero);

    // Test 3: Sherry g=4 (修复验证)
    int W_sherry[4] = {1, -1, 0, 1}; // 内积: 5*1 - 2*1 + 0 + 1*1 = 4
    generation_unit(A_full, MODE_SHERRY_G4, &pe_regs);
    float res2 = execute_unit(W_sherry, MODE_SHERRY_G4, &pe_regs);
    printf("[Sherry g=4] Ground Truth: 4.0 | 硬件输出: %.1f \n", res2);

    // Test 4: TMAC g=6 
    int W_tmac[6] = {-1, -1, 1, -1, 1, -1}; // 内积: -5 -2 +4 -1 +3 -2 = -3
    generation_unit(A_full, MODE_TMAC_G6, &pe_regs);
    float res3 = execute_unit(W_tmac, MODE_TMAC_G6, &pe_regs);
    printf("[TMAC g=6]   Ground Truth: -3.0 | 硬件输出: %.1f \n", res3);

    // Test 5: Sparse 2:4
    int W_sparse[4] = {0, -1, 1, 0}; // 零点在0和3。内积: 0 - 2 + 4 + 0 = 2
    generation_unit(A_full, MODE_SPARSE_2_4, &pe_regs);
    float res4 = execute_unit(W_sparse, MODE_SPARSE_2_4, &pe_regs);
    printf("[Sparse 2:4] Ground Truth: 2.0  | 硬件输出: %.1f \n", res4);

    return 0;
}