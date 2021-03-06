// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

/* This file is automatically generated. Do not modify it directly. */
#if HWY_TARGET != HWY_NEON
#error "only include this file from fast_dct-inl.h"
#endif

constexpr size_t FastIDCTIntegerBits(FastDCTTag<16>) { return 1; }

void FastIDCT(FastDCTTag<16>, const int16_t* in, size_t in_stride, int16_t* out,
              size_t out_stride, size_t count) {
  JXL_ASSERT(count % 8 == 0);
  for (size_t i = 0; i < count; i += 8) {
    int16x8_t v0 = vld1q_s16(in + in_stride * 0 + i);
    int16x8_t v1 = vld1q_s16(in + in_stride * 8 + i);
    int16x8_t v2 = vaddq_s16(v0, v1);
    int16x8_t v3 = vld1q_s16(in + in_stride * 4 + i);
    int16x8_t v4_tmp = vqrdmulhq_n_s16(v3, 13573);
    int16x8_t v4 = vaddq_s16(v4_tmp, v3);
    int16x8_t v5 = vld1q_s16(in + in_stride * 12 + i);
    int16x8_t v6 = vaddq_s16(v5, v3);
    int16x8_t v7 = vaddq_s16(v4, v6);
    int16x8_t v8 = vqrdmulhq_n_s16(v7, 17734);
    int16x8_t v9 = vaddq_s16(v2, v8);
    int16x8_t v10 = vld1q_s16(in + in_stride * 2 + i);
    int16x8_t v11_tmp = vqrdmulhq_n_s16(v10, 13573);
    int16x8_t v11 = vaddq_s16(v11_tmp, v10);
    int16x8_t v12 = vld1q_s16(in + in_stride * 10 + i);
    int16x8_t v13 = vld1q_s16(in + in_stride * 6 + i);
    int16x8_t v14 = vaddq_s16(v12, v13);
    int16x8_t v15 = vaddq_s16(v11, v14);
    int16x8_t v16 = vaddq_s16(v13, v10);
    int16x8_t v17 = vqrdmulhq_n_s16(v16, 25080);
    int16x8_t v18 = vld1q_s16(in + in_stride * 14 + i);
    int16x8_t v19 = vaddq_s16(v18, v12);
    int16x8_t v20 = vaddq_s16(v16, v19);
    int16x8_t v21 = vqrdmulhq_n_s16(v20, 17734);
    int16x8_t v22 = vaddq_s16(v17, v21);
    int16x8_t v23 = vaddq_s16(v15, v22);
    int16x8_t v24 = vqrdmulhq_n_s16(v23, 16705);
    int16x8_t v25 = vaddq_s16(v9, v24);
    int16x8_t v26 = vld1q_s16(in + in_stride * 15 + i);
    int16x8_t v27 = vld1q_s16(in + in_stride * 13 + i);
    int16x8_t v28 = vaddq_s16(v26, v27);
    int16x8_t v29 = vld1q_s16(in + in_stride * 11 + i);
    int16x8_t v30 = vld1q_s16(in + in_stride * 9 + i);
    int16x8_t v31 = vaddq_s16(v29, v30);
    int16x8_t v32 = vaddq_s16(v28, v31);
    int16x8_t v33 = vqrdmulhq_n_s16(v32, 17734);
    int16x8_t v34 = vld1q_s16(in + in_stride * 3 + i);
    int16x8_t v35 = vld1q_s16(in + in_stride * 1 + i);
    int16x8_t v36 = vaddq_s16(v34, v35);
    int16x8_t v37 = vld1q_s16(in + in_stride * 7 + i);
    int16x8_t v38 = vld1q_s16(in + in_stride * 5 + i);
    int16x8_t v39 = vaddq_s16(v37, v38);
    int16x8_t v40 = vaddq_s16(v36, v39);
    int16x8_t v41_tmp = vqrdmulhq_n_s16(v40, 10045);
    int16x8_t v41 = vaddq_s16(v41_tmp, v40);
    int16x8_t v42 = vaddq_s16(v33, v41);
    int16x8_t v43 = vqrdmulhq_n_s16(v42, 16705);
    int16x8_t v44_tmp = vqrdmulhq_n_s16(v36, 13573);
    int16x8_t v44 = vaddq_s16(v44_tmp, v36);
    int16x8_t v45 = vaddq_s16(v39, v31);
    int16x8_t v46 = vaddq_s16(v44, v45);
    int16x8_t v47 = vqrdmulhq_n_s16(v46, 16705);
    int16x8_t v48 = vaddq_s16(v43, v47);
    int16x8_t v49_tmp = vqrdmulhq_n_s16(v35, 13573);
    int16x8_t v49 = vaddq_s16(v49_tmp, v35);
    int16x8_t v50 = vaddq_s16(v30, v37);
    int16x8_t v51 = vaddq_s16(v49, v50);
    int16x8_t v52 = vaddq_s16(v38, v34);
    int16x8_t v53 = vaddq_s16(v27, v29);
    int16x8_t v54 = vaddq_s16(v52, v53);
    int16x8_t v55 = vqrdmulhq_n_s16(v54, 17734);
    int16x8_t v56 = vqrdmulhq_n_s16(v52, 25080);
    int16x8_t v57 = vaddq_s16(v55, v56);
    int16x8_t v58 = vaddq_s16(v51, v57);
    int16x8_t v59 = vaddq_s16(v48, v58);
    int16x8_t v60 = vqrdmulhq_n_s16(v59, 16463);
    int16x8_t v61 = vaddq_s16(v25, v60);
    int16x8_t v62 = vsubq_s16(v0, v1);
    int16x8_t v63 = vsubq_s16(v4, v6);
    int16x8_t v64_tmp = vqrdmulhq_n_s16(v63, 10045);
    int16x8_t v64 = vaddq_s16(v64_tmp, v63);
    int16x8_t v65 = vaddq_s16(v62, v64);
    int16x8_t v66 = vsubq_s16(v11, v14);
    int16x8_t v67 = vqrdmulhq_n_s16(v16, 17734);
    int16x8_t v68_tmp = vqrdmulhq_n_s16(v19, 10045);
    int16x8_t v68 = vaddq_s16(v68_tmp, v19);
    int16x8_t v69 = vsubq_s16(v67, v68);
    int16x8_t v70 = vaddq_s16(v66, v69);
    int16x8_t v71 = vqrdmulhq_n_s16(v70, 19705);
    int16x8_t v72 = vaddq_s16(v65, v71);
    int16x8_t v73 = vsubq_s16(v49, v50);
    int16x8_t v74 = vqrdmulhq_n_s16(v52, 17734);
    int16x8_t v75_tmp = vqrdmulhq_n_s16(v53, 10045);
    int16x8_t v75 = vaddq_s16(v75_tmp, v53);
    int16x8_t v76 = vsubq_s16(v74, v75);
    int16x8_t v77 = vaddq_s16(v73, v76);
    int16x8_t v78 = vsubq_s16(v44, v45);
    int16x8_t v79 = vqrdmulhq_n_s16(v78, 19705);
    int16x8_t v80 = vqrdmulhq_n_s16(v40, 13573);
    int16x8_t v81 = vsubq_s16(v80, v32);
    int16x8_t v82 = vqrdmulhq_n_s16(v81, 25746);
    int16x8_t v83 = vaddq_s16(v79, v82);
    int16x8_t v84 = vaddq_s16(v77, v83);
    int16x8_t v85 = vqrdmulhq_n_s16(v84, 17121);
    int16x8_t v86 = vaddq_s16(v72, v85);
    int16x8_t v87 = vsubq_s16(v62, v64);
    int16x8_t v88 = vsubq_s16(v66, v69);
    int16x8_t v89 = vqrdmulhq_n_s16(v88, 29490);
    int16x8_t v90 = vaddq_s16(v87, v89);
    int16x8_t v91 = vsubq_s16(v73, v76);
    int16x8_t v92 = vqrdmulhq_n_s16(v78, 29490);
    int16x8_t v93_tmp = vqrdmulhq_n_s16(v81, 5763);
    int16x8_t v93 = vaddq_s16(v93_tmp, v81);
    int16x8_t v94 = vsubq_s16(v92, v93);
    int16x8_t v95 = vaddq_s16(v91, v94);
    int16x8_t v96 = vqrdmulhq_n_s16(v95, 18578);
    int16x8_t v97 = vaddq_s16(v90, v96);
    int16x8_t v98 = vsubq_s16(v46, v42);
    int16x8_t v99_tmp = vqrdmulhq_n_s16(v98, 18446);
    int16x8_t v99 = vmlaq_n_s16(v99_tmp, v98, 2);
    int16x8_t v100 = vsubq_s16(v51, v57);
    int16x8_t v101 = vaddq_s16(v99, v100);
    int16x8_t v102 = vqrdmulhq_n_s16(v101, 21195);
    int16x8_t v103 = vsubq_s16(v2, v8);
    int16x8_t v104 = vsubq_s16(v15, v22);
    int16x8_t v105_tmp = vqrdmulhq_n_s16(v104, 18446);
    int16x8_t v105 = vmlaq_n_s16(v105_tmp, v104, 2);
    int16x8_t v106 = vaddq_s16(v103, v105);
    int16x8_t v107 = vaddq_s16(v102, v106);
    int16x8_t v108 = vsubq_s16(v103, v105);
    int16x8_t v109 = vsubq_s16(v100, v99);
    int16x8_t v110 = vqrdmulhq_n_s16(v109, 25826);
    int16x8_t v111 = vaddq_s16(v108, v110);
    int16x8_t v112 = vsubq_s16(v87, v89);
    int16x8_t v113 = vsubq_s16(v91, v94);
    int16x8_t v114_tmp = vqrdmulhq_n_s16(v113, 1988);
    int16x8_t v114 = vaddq_s16(v114_tmp, v113);
    int16x8_t v115 = vaddq_s16(v112, v114);
    int16x8_t v116 = vsubq_s16(v65, v71);
    int16x8_t v117 = vsubq_s16(v77, v83);
    int16x8_t v118_tmp = vqrdmulhq_n_s16(v117, 23673);
    int16x8_t v118 = vaddq_s16(v118_tmp, v117);
    int16x8_t v119 = vaddq_s16(v116, v118);
    int16x8_t v120 = vsubq_s16(v58, v48);
    int16x8_t v121_tmp = vqrdmulhq_n_s16(v120, 3314);
    int16x8_t v121 = vmlaq_n_s16(v121_tmp, v120, 5);
    int16x8_t v122 = vsubq_s16(v9, v24);
    int16x8_t v123 = vaddq_s16(v121, v122);
    int16x8_t v124 = vsubq_s16(v122, v121);
    int16x8_t v125 = vsubq_s16(v116, v118);
    int16x8_t v126 = vsubq_s16(v112, v114);
    int16x8_t v127 = vsubq_s16(v108, v110);
    int16x8_t v128 = vsubq_s16(v106, v102);
    int16x8_t v129 = vsubq_s16(v90, v96);
    int16x8_t v130 = vsubq_s16(v72, v85);
    int16x8_t v131 = vsubq_s16(v25, v60);
    vst1q_s16(out + out_stride * 0 + i, v61);
    vst1q_s16(out + out_stride * 1 + i, v86);
    vst1q_s16(out + out_stride * 2 + i, v97);
    vst1q_s16(out + out_stride * 3 + i, v107);
    vst1q_s16(out + out_stride * 4 + i, v111);
    vst1q_s16(out + out_stride * 5 + i, v115);
    vst1q_s16(out + out_stride * 6 + i, v119);
    vst1q_s16(out + out_stride * 7 + i, v123);
    vst1q_s16(out + out_stride * 8 + i, v124);
    vst1q_s16(out + out_stride * 9 + i, v125);
    vst1q_s16(out + out_stride * 10 + i, v126);
    vst1q_s16(out + out_stride * 11 + i, v127);
    vst1q_s16(out + out_stride * 12 + i, v128);
    vst1q_s16(out + out_stride * 13 + i, v129);
    vst1q_s16(out + out_stride * 14 + i, v130);
    vst1q_s16(out + out_stride * 15 + i, v131);
  }
}
