#include "InferenceStage.h"
#include "Logger.h"
#include "Sha256.h"
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

bool InferenceStage::switch_model(const std::string& model_path, const std::string& expected_sha256) {
    std::lock_guard<std::mutex> lock(model_mutex_);  // 热切换期间阻塞推理线程

    // 校验模型文件 SHA256（如果提供了期望值）
    if (!expected_sha256.empty()) {
        std::string actual_sha256 = sha256_file(model_path);
        if (actual_sha256.empty()) {
            GetLogger("InferenceStage")->error("模型文件不存在或无法读取: {}", model_path);
            return false;
        }
        if (actual_sha256 != expected_sha256) {
            GetLogger("InferenceStage")->error("SHA256 校验失败: expected={}, actual={}",
                expected_sha256, actual_sha256);
            return false;
        }
        GetLogger("InferenceStage")->info("SHA256 校验通过: {}", actual_sha256);
    }

    // 保存旧模型信息，用于失败回滚
    auto old_ctx = ctx_;
    auto old_input_buf = std::move(input_buf_);
    auto old_input_attrs = std::move(input_attrs_);
    auto old_output_attrs = std::move(output_attrs_);
    auto old_io_num = io_num_;
    auto old_input_size = input_size_;
    auto old_model_path = model_path_;
    ctx_ = 0;
    input_buf_.clear();
    input_attrs_.clear();
    output_attrs_.clear();
    io_num_ = {};
    input_size_ = 0;

    bool ok = init_rknn(model_path);
    if (ok) {
        // 新模型加载成功，销毁旧模型
        if (old_ctx != 0) {
            rknn_destroy(old_ctx);
        }
        model_path_ = model_path;
        GetLogger("InferenceStage")->info("模型热切换成功: {} -> {}", old_model_path, model_path);
        return true;
    }

    // 新模型加载失败，尝试回滚旧模型
    GetLogger("InferenceStage")->error("新模型加载失败: {}, 尝试回滚旧模型: {}", model_path, old_model_path);

    // 清理新模型可能残留的状态
    if (ctx_ != 0) {
        rknn_destroy(ctx_);
        ctx_ = 0;
    }

    // 恢复旧模型
    ctx_ = old_ctx;
    input_buf_ = std::move(old_input_buf);
    input_attrs_ = std::move(old_input_attrs);
    output_attrs_ = std::move(old_output_attrs);
    io_num_ = old_io_num;
    input_size_ = old_input_size;
    model_path_ = old_model_path;

    if (ctx_ != 0) {
        GetLogger("InferenceStage")->warn("已回滚到旧模型: {}", old_model_path);
        return false;
    }

    // 旧模型也无效（极端情况），上报 FATAL
    GetLogger("InferenceStage")->error("FATAL: 旧模型回滚失败，模型文件可能已损坏");
    if (fatal_cb_) {
        fatal_cb_("MODEL_ROLLBACK_FATAL: 新旧模型均加载失败");
    }
    return false;
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

    // 准备 rknn_input / rknn_output（按模型实际 IO 数动态分配，原先固定 [1] 数组在
    // 多输出模型如 YOLOv5s(n_output=3) 会栈溢出）
    while (running_.load()) {
        std::shared_ptr<Frame> frame;
        if (!input_queue_->pop(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (!frame || !frame->preprocessed_data) {
            continue;
        }

        // 加锁保护 ctx_ 访问，防止与 switch_model() 并发
        std::lock_guard<std::mutex> lock(model_mutex_);
        if (ctx_ == 0) continue;  // 模型未加载，跳过

        // 设置 RKNN 输入：PreprocessStage 输出 uint8 NHWC RGB letterbox 图像，
        // pass_through=0 让 RKNN 内部做 /255 归一化并转换到模型内部格式。
        // （与 Rockchip 参考工程一致：喂 uint8，RKNN 做均值/标准差归一化）
        std::vector<rknn_input> inputs(io_num_.n_input);
        for (uint32_t i = 0; i < io_num_.n_input; ++i) {
            inputs[i].index = i;
            inputs[i].type = RKNN_TENSOR_UINT8;
            inputs[i].fmt  = RKNN_TENSOR_NHWC;
            inputs[i].size = static_cast<uint32_t>(input_size_) * input_size_ * 3;
            inputs[i].buf  = frame->preprocessed_data;
            inputs[i].pass_through = 0;
        }

        int ret = rknn_inputs_set(ctx_, io_num_.n_input, inputs.data());
        if (ret < 0) {
            continue;
        }

        // 推理
        ret = rknn_run(ctx_, nullptr);
        if (ret < 0) {
            continue;
        }

        // 获取全部输出
        std::vector<rknn_output> outputs(io_num_.n_output);
        for (uint32_t i = 0; i < io_num_.n_output; ++i) {
            outputs[i].want_float = 1;
            outputs[i].is_prealloc = 0;
        }

        ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs.data(), nullptr);
        if (ret < 0) {
            continue;
        }

        // 构造 InferenceResult：拷贝每个输出及其维度
        InferenceResult result;
        result.frame = frame;
        result.outputs.resize(io_num_.n_output);
        for (uint32_t i = 0; i < io_num_.n_output; ++i) {
            size_t n_float = outputs[i].size / sizeof(float);
            result.outputs[i].data.resize(n_float);
            if (n_float > 0) {
                std::memcpy(result.outputs[i].data.data(), outputs[i].buf, outputs[i].size);
            }
            result.outputs[i].dims.assign(
                output_attrs_[i].dims, output_attrs_[i].dims + output_attrs_[i].n_dims);
        }

        // 释放 RKNN 输出
        rknn_outputs_release(ctx_, io_num_.n_output, outputs.data());

        // 送入后处理队列
        output_queue_->push(result);
    }

    destroy_rknn();
}

} // namespace gateway_engine
