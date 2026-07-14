#include "InferenceStage.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace gateway_engine {

InferenceStage::InferenceStage(
    PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue,
    PipelineQueue<InferenceResult, 4>* output_queue,
    const std::string& model_path)
    : input_queue_(input_queue)
    , output_queue_(output_queue)
    , model_path_(model_path) {
}

InferenceStage::~InferenceStage() {
    stop();
    destroy_rknn();
}

bool InferenceStage::init_rknn(const std::string& model_path) {
    // 读取模型文件
    int fd = open(model_path.c_str(), O_RDONLY);
    if (fd < 0) {
        // 模型文件不存在时打印日志但返回 true（后续 run 会跳过推理）
        return false;
    }

    struct stat st;
    fstat(fd, &st);
    size_t model_size = st.st_size;

    std::vector<uint8_t> model_data(model_size);
    ssize_t bytes_read = 0;
    while (bytes_read < (ssize_t)model_size) {
        ssize_t n = read(fd, model_data.data() + bytes_read, model_size - bytes_read);
        if (n <= 0) break;
        bytes_read += n;
    }
    close(fd);

    if (bytes_read != (ssize_t)model_size) {
        return false;
    }

    // rknn_init
    int ret = rknn_init(&ctx_, model_data.data(), model_size, 0, nullptr);
    if (ret < 0) {
        ctx_ = 0;
        return false;
    }

    // 查询输入输出信息
    ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret < 0) {
        destroy_rknn();
        return false;
    }

    input_attrs_.resize(io_num_.n_input);
    output_attrs_.resize(io_num_.n_output);
    for (uint32_t i = 0; i < io_num_.n_input; i++) {
        input_attrs_[i] = {};
        input_attrs_[i].index = i;
        rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(input_attrs_[i]));
    }
    for (uint32_t i = 0; i < io_num_.n_output; i++) {
        output_attrs_[i] = {};
        output_attrs_[i].index = i;
        rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(output_attrs_[i]));
    }

    // 输入一般是 NCHW，取 input_size
    if (io_num_.n_input > 0) {
        // rknn_tensor_attr 中 dims[2] = H, dims[3] = W (NHWC) 或 dims[1]=H,dims[2]=W (NCHW)
        // 取决于模型格式，取最大值作为 input_size
        int h = input_attrs_[0].dims[1];
        int w = input_attrs_[0].dims[2];
        if (input_attrs_[0].fmt == RKNN_TENSOR_NCHW) {
            h = input_attrs_[0].dims[2];
            w = input_attrs_[0].dims[3];
        }
        input_size_ = (h > w) ? h : w;
        // 预分配输入缓冲区
        uint32_t buf_size = input_attrs_[0].n_elems * input_attrs_[0].size / input_attrs_[0].n_elems;
        // 更稳健：用 w_stride * h_stride
        size_t total = 1;
        for (uint32_t d = 0; d < input_attrs_[0].n_dims; d++) {
            total *= input_attrs_[0].dims[d];
        }
        if (input_attrs_[0].type == RKNN_TENSOR_FLOAT32) {
            total *= sizeof(float);
        }
        input_buf_.resize(total);
    }

    return true;
}

void InferenceStage::destroy_rknn() {
    if (ctx_ != 0) {
        rknn_destroy(ctx_);
        ctx_ = 0;
    }
}

bool InferenceStage::switch_model(const std::string& model_path) {
    destroy_rknn();
    if (!init_rknn(model_path)) {
        return false;
    }
    model_path_ = model_path;
    return true;
}

void InferenceStage::run() {
    // 首次加载模型
    if (!init_rknn(model_path_)) {
        // 模型加载失败，进入空循环（等待热切换或退出）
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return;
    }

    // 准备 rknn_input / rknn_output
    rknn_input inputs[1];
    rknn_output outputs[1];

    while (running_.load()) {
        std::shared_ptr<Frame> frame;
        if (!input_queue_->pop(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (!frame || !frame->preprocessed_data) {
            continue;
        }

        // 设置 RKNN 输入
        memset(inputs, 0, sizeof(inputs));
        inputs[0].index = 0;
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].size = input_buf_.size();
        inputs[0].fmt = RKNN_TENSOR_NHWC;
        inputs[0].buf = frame->preprocessed_data;
        inputs[0].pass_through = 0;

        int ret = rknn_inputs_set(ctx_, io_num_.n_input, inputs);
        if (ret < 0) {
            continue;
        }

        // 推理
        ret = rknn_run(ctx_, nullptr);
        if (ret < 0) {
            continue;
        }

        // 获取输出
        memset(outputs, 0, sizeof(outputs));
        outputs[0].want_float = 1;
        outputs[0].is_prealloc = 0;

        ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs, nullptr);
        if (ret < 0) {
            continue;
        }

        // 构造 InferenceResult
        InferenceResult result;
        result.frame = frame;
        result.output_size = output_attrs_[0].n_elems;
        // 如果 output_attrs_[0].type == FLOAT32，n_elems 就是 float 数
        // 如果 type == UINT8，需要除 sizeof(float)
        if (output_attrs_[0].type == RKNN_TENSOR_FLOAT32) {
            result.output = (float*)malloc(outputs[0].size);
            result.output_size = outputs[0].size / sizeof(float);
        } else {
            // 非 float 类型，按 byte 拷贝
            result.output = (float*)malloc(outputs[0].size);
            memcpy(result.output, outputs[0].buf, outputs[0].size);
            result.output_size = outputs[0].size / sizeof(float);
        }
        if (result.output) {
            memcpy(result.output, outputs[0].buf, outputs[0].size);
        }

        // 释放 RKNN 输出
        rknn_outputs_release(ctx_, io_num_.n_output, outputs);

        // 送入后处理队列
        output_queue_->push(result);
    }

    destroy_rknn();
}

} // namespace gateway_engine
