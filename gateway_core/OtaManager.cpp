#include "OtaManager.h"
#include "Logger.h"
#include "InternalMessage.h"
#include "Sha256.h"
#include <fstream>
#include <curl/curl.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

OtaManager::OtaManager(const std::string& model_dir, const std::string& db_path)
    : model_dir_(model_dir)
    , db_path_(db_path) {}

void OtaManager::set_command_sender(CommandSender sender) {
    command_sender_ = std::move(sender);
}

void OtaManager::set_status_reporter(StatusReporter reporter) {
    status_reporter_ = std::move(reporter);
}

static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* stream = static_cast<std::ofstream*>(userdata);
    stream->write(static_cast<const char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool OtaManager::download_model(const std::string& url, const std::string& dest_path) {
    auto logger = GetLogger("OtaManager");

    struct stat st;
    size_t existing_size = 0;
    if (stat(dest_path.c_str(), &st) == 0) {
        existing_size = st.st_size;
    }

    std::ofstream f(dest_path, existing_size > 0 ? std::ios::app : std::ios::binary);
    if (!f.is_open()) {
        logger->error("cannot open dest file: {}", dest_path);
        return false;
    }

    if (existing_size > 0) {
        f.seekp(0, std::ios::end);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        logger->error("curl_easy_init failed");
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    if (existing_size > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM, (curl_off_t)existing_size);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    f.close();

    if (res != CURLE_OK) {
        logger->error("download failed: {}", curl_easy_strerror(res));
        return false;
    }

    if (http_code != 200 && http_code != 206) {
        logger->error("HTTP error: {}", http_code);
        return false;
    }

    logger->info("downloaded to {} (HTTP {})", dest_path, http_code);
    return true;
}

bool OtaManager::verify_sha256(const std::string& file_path, const std::string& expected_hash) {
    std::string actual_hash = sha256_file(file_path);
    if (actual_hash.empty()) return false;
    return actual_hash == expected_hash;
}

bool OtaManager::notify_engine_switch(const std::string& model_path, const std::string& sha256) {
    if (!command_sender_) return false;

    // payload 格式: "model_path|sha256"
    // 引擎收到 CMD_SWITCH_MODEL 后解析，用 sha256 校验模型完整性再热切换
    std::string payload = model_path + "|" + sha256;
    return command_sender_(0, CMD_SWITCH_MODEL, payload);
}

void OtaManager::save_version(const std::string& model_name, const std::string& version, const std::string& sha256) {
    std::string version_path = model_dir_ + "/model_version.json";
    std::ofstream f(version_path);
    if (!f.is_open()) return;

    f << "{\n";
    f << "  \"current_model\": \"" << model_name << "\",\n";
    f << "  \"version\": \"" << version << "\",\n";
    f << "  \"sha256\": \"" << sha256 << "\"\n";
    f << "}\n";
}

std::string OtaManager::handle_ota_update(const std::string& payload) {
    auto logger = GetLogger("OtaManager");

    auto url_pos = payload.find("\"url\"");
    auto sha256_pos = payload.find("\"sha256\"");
    auto name_pos = payload.find("\"model_name\"");
    auto ver_pos = payload.find("\"version\"");

    if (url_pos == std::string::npos || sha256_pos == std::string::npos) {
        return "NACK: missing url or sha256";
    }

    auto extract_str = [&](size_t pos) -> std::string {
        auto start = payload.find('"', pos + 1) + 1;
        auto end = payload.find('"', start);
        if (start == std::string::npos || end == std::string::npos) return "";
        return payload.substr(start, end - start);
    };

    std::string url = extract_str(url_pos);
    std::string expected_sha256 = extract_str(sha256_pos);
    std::string model_name = name_pos != std::string::npos ? extract_str(name_pos) : "model.rknn";
    std::string version = ver_pos != std::string::npos ? extract_str(ver_pos) : "1.0";

    if (url.empty() || expected_sha256.empty()) {
        return "NACK: invalid params";
    }

    std::string dest_path = model_dir_ + "/" + model_name;

    logger->info("OTA: downloading {} -> {}", url, dest_path);

    if (!download_model(url, dest_path)) {
        return "NACK: download failed";
    }

    if (!verify_sha256(dest_path, expected_sha256)) {
        logger->error("OTA: SHA256 mismatch, removing {}", dest_path);
        std::remove(dest_path.c_str());
        return "NACK: SHA256 verification failed";
    }

    logger->info("OTA: SHA256 verified, notifying engine to switch");

    if (!notify_engine_switch(dest_path, expected_sha256)) {
        return "NACK: failed to notify engine";
    }

    save_version(model_name, version, expected_sha256);

    if (status_reporter_) {
        status_reporter_("OTA: model updated to " + model_name);
    }

    return "ACK: model updated to " + model_name;
}
