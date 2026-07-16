#!/usr/bin/env python3
"""
YOLOv5s ONNX → RKNN 转换脚本
用法: python3 yolov5_convert.py <onnx_model> [output_rknn]

依赖: rknn-toolkit2 (建议 2.3.2), onnx<1.17
"""

import sys
import os
from rknn.api import RKNN

ONNX_MODEL = sys.argv[1] if len(sys.argv) > 1 else "yolov5s.onnx"
OUTPUT_RKNN = sys.argv[2] if len(sys.argv) > 2 else ONNX_MODEL.replace(".onnx", ".rknn")
TARGET_PLATFORM = "rk3588"

def export_rknn():
    rknn = RKNN(verbose=False)

    # 1. 配置
    print("--> Config model")
    rknn.config(
        target_platform=TARGET_PLATFORM,
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]],
    )

    # 2. 加载 ONNX
    print(f"--> Loading ONNX: {ONNX_MODEL}")
    ret = rknn.load_onnx(model=ONNX_MODEL)
    if ret != 0:
        print("load_onnx failed!")
        exit(1)

    # 3. 构建 RKNN（FP16，不做 INT8 量化）
    print("--> Building RKNN (FP16)...")
    ret = rknn.build(do_quantization=False)
    if ret != 0:
        print("build failed!")
        exit(1)

    # 4. 导出
    print(f"--> Exporting RKNN: {OUTPUT_RKNN}")
    ret = rknn.export_rknn(OUTPUT_RKNN)
    if ret != 0:
        print("export failed!")
        exit(1)

    size = os.path.getsize(OUTPUT_RKNN)
    print(f"--> Done! {OUTPUT_RKNN} ({size / 1024 / 1024:.1f} MB)")
    rknn.release()

if __name__ == "__main__":
    export_rknn()
