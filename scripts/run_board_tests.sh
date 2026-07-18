#!/bin/bash
# 板端运行全部测试（绕过 ctest 硬编码路径问题）
# 用法: bash scripts/run_board_tests.sh

set -e
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

TESTS=(
    test_RingBuffer test_ShmLayout test_InternalMessage test_DbWriter
    test_EventLoop test_ShmPublisher test_ShmReader test_ModbusRtu
    test_SpscQueue test_ModbusTcp test_RuleEngine test_PipelineFps
    test_FramePool test_PipelineQueue test_StageBase test_EventFusion
)

PASSED=0
FAILED=0

for t in "${TESTS[@]}"; do
    echo "=== $t ==="
    if ./build/tests/"$t" 2>&1; then
        PASSED=$((PASSED + 1))
    else
        echo "FAILED: $t"
        FAILED=$((FAILED + 1))
    fi
    echo ""
done

echo "=============================="
echo "Result: $PASSED passed, $FAILED failed"
echo "=============================="
