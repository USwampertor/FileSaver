#pragma once

#include <thread>
#include <string>
#include <memory>
#include <filesystem>
#include <fstream>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <map>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/buffer.h>
#include <iomanip>
#include <sstream>

struct UploadAuthorization {
  std::string uploadUrl = "";
  std::string authorizationToken = "";
};

struct BackblazeCredentials
{
  std::string accountId = "";
  std::string applicationKey = "";
  std::string bucketId = "";
  std::string bucketName = "";

  std::string authToken;
  std::string apiUrl;
  std::string downloadUrl;

  bool isAuthenticated = false;
  CURL* curl = nullptr;

  BackblazeCredentials() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
  }

  ~BackblazeCredentials() {
    if (curl) {
      curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
  }

  static size_t writeCallback(char* ptr, size_t size, size_t nmemb, std::string* response) {
    size_t totalSize = size * nmemb;
    response->append(ptr, totalSize);
    return totalSize;
  }

  std::string b2ApiCall(const std::string& endpoint,
    const std::string& postData = "",
    const std::string& customAuthToken = "") {
    if (!curl) {
      std::cerr << "cURL not initialized" << std::endl;
      return "";
    }

    std::string response;
    std::string url;

    // Special handling for authorization call
    if (endpoint == "b2_authorize_account") {
      url = "https://api.backblazeb2.com/b2api/v2/" + endpoint;
    }
    else if (apiUrl.empty()) {
      // Fallback if apiUrl is not set
      url = "https://api.backblazeb2.com/b2api/v2/" + endpoint;
      std::cout << "WARNING: Using fallback API URL: " << url << std::endl;
    }
    else {
      url = apiUrl + "/b2api/v2/" + endpoint;
    }

    struct curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: " + (customAuthToken.empty() ? authToken : customAuthToken);
    headers = curl_slist_append(headers, authHeader.c_str());

    if (!postData.empty()) {
      headers = curl_slist_append(headers, "Content-Type: application/json");
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
    }
    else {
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
      std::cerr << "B2 API call failed: " << curl_easy_strerror(res) << std::endl;
      std::cerr << "URL: " << url << std::endl;
      return "";
    }

    if (http_code != 200) {
      std::cerr << "HTTP Error: " << http_code << std::endl;
      std::cerr << "Response: " << response << std::endl;
      return "";
    }

    return response;
  }

  bool authenticate() {

    std::cout << "Attempting authentication with:" << std::endl;
    std::cout << "Account ID: " << accountId << std::endl;
    std::cout << "Application Key: " << applicationKey << std::endl;

    std::string credentials = accountId + ":" + applicationKey;
    std::cout << "Raw credentials: " << credentials << std::endl;

    std::string authHeader = "Basic " + base64Encode(credentials);
    std::cout << "Auth header: " << authHeader << std::endl;

    std::string response = b2ApiCall("b2_authorize_account", "", authHeader);

    if (response.empty()) {
      return false;
    }

    // Debug: print the raw authentication response
    std::cout << "=== AUTHENTICATION RESPONSE ===" << std::endl;
    std::cout << response << std::endl;
    std::cout << "===============================" << std::endl;

    rapidjson::Document doc;
    doc.Parse(response.c_str());

    // Check for authentication error first
    if (doc.HasMember("code") && doc.HasMember("message")) {
      std::string errorCode = doc["code"].GetString();
      std::string errorMessage = doc["message"].GetString();
      std::cerr << "Authentication failed: " << errorCode << " - " << errorMessage << std::endl;

      if (errorCode == "bad_auth_token") {
        std::cerr << "This usually means your accountId or applicationKey is incorrect." << std::endl;
        std::cerr << "Account ID: " << accountId << std::endl;
        std::cerr << "Application Key: " << (applicationKey.empty() ? "EMPTY" : "SET") << std::endl;
      }
      return false;
    }

    if (doc.HasParseError() || !doc.IsObject()) {
      std::cerr << "Failed to parse authentication response" << std::endl;
      return false;
    }

    // Extract fields from successful response
    if (doc.HasMember("authorizationToken")) {
      authToken = doc["authorizationToken"].GetString();
      std::cout << "Got authorizationToken" << std::endl;
    }
    if (doc.HasMember("apiUrl")) {
      apiUrl = doc["apiUrl"].GetString();
      std::cout << "Got apiUrl: " << apiUrl << std::endl;
    }
    if (doc.HasMember("downloadUrl")) {
      downloadUrl = doc["downloadUrl"].GetString();
      std::cout << "Got downloadUrl: " << downloadUrl << std::endl;
    }

    // Extract bucket information from the "allowed" section
    if (doc.HasMember("allowed") && doc["allowed"].IsObject()) {
      const rapidjson::Value& allowed = doc["allowed"];
      if (allowed.HasMember("bucketId") && allowed["bucketId"].IsString()) {
        bucketId = allowed["bucketId"].GetString();
        std::cout << "Got bucketId from auth response: " << bucketId << std::endl;
      }
      if (allowed.HasMember("bucketName") && allowed["bucketName"].IsString()) {
        bucketName = allowed["bucketName"].GetString();
        std::cout << "Got bucketName from auth response: " << bucketName << std::endl;
      }
    }

    isAuthenticated = true;
    std::cout << "Backblaze B2 authentication successful!" << std::endl;
    return true;
  }

  static std::string base64Encode(const std::string& input) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    // Don't add newlines
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(bio, input.c_str(), static_cast<int>(input.length()));
    BIO_flush(bio);

    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    std::cout << "Base64 encoded: " << result << std::endl;
    return result;
  }

  bool createBucket(const std::string& bucketName) {
    if (!isAuthenticated) {
      std::cerr << "Not authenticated. Call authenticate() first." << std::endl;
      return false;
    }

    // Check if we already have a bucket from the authentication response
    if (!bucketId.empty()) {
      std::cout << "Bucket already available: " << bucketName << " (ID: " << bucketId << ")" << std::endl;
      this->bucketName = bucketName;
      return true;
    }

    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();

    doc.AddMember("bucketName", rapidjson::Value(bucketName.c_str(), allocator), allocator);
    doc.AddMember("bucketType", "allPrivate", allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    std::string response = b2ApiCall("b2_create_bucket", buffer.GetString());

    if (response.empty()) {
      return false;
    }

    rapidjson::Document responseDoc;
    responseDoc.Parse(response.c_str());

    if (responseDoc.HasParseError() || !responseDoc.IsObject() || responseDoc.HasMember("error")) {
      std::cerr << "Failed to create bucket: " << response << std::endl;
      return false;
    }

    if (responseDoc.HasMember("bucketId")) {
      bucketId = responseDoc["bucketId"].GetString();
      this->bucketName = bucketName;
      std::cout << "Bucket created successfully: " << bucketId << std::endl;
      return true;
    }

    std::cerr << "Failed to create bucket. Response: " << response << std::endl;
    return false;
  }

  UploadAuthorization getUploadUrl() {
    // Check if authenticated first

    UploadAuthorization result;

    if (!isAuthenticated) {
      std::cerr << "Not authenticated. Call authenticate() first." << std::endl;
      return {};
    }

    // Check if bucketId is set
    if (bucketId.empty()) {
      std::cerr << "Bucket ID is not set. Call createBucket() or set bucketId first." << std::endl;
      return {};
    }

    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();

    doc.AddMember("bucketId", rapidjson::Value(bucketId.c_str(), allocator), allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    std::string response = b2ApiCall("b2_get_upload_url", buffer.GetString());

    if (response.empty()) {
      std::cerr << "Empty response from b2_get_upload_url API call" << std::endl;
      return {};
    }

    // Debug: print the raw response
    std::cout << "Upload URL response: " << response << std::endl;

    rapidjson::Document responseDoc;
    responseDoc.Parse(response.c_str());

    if (responseDoc.HasParseError()) {
      std::cerr << "Failed to parse JSON response: " << rapidjson::GetParseErrorFunc(responseDoc.GetParseError()) << std::endl;
      return {};
    }

    if (!responseDoc.IsObject()) {
      std::cerr << "Response is not a JSON object" << std::endl;
      return {};
    }

    // Check for error first
    if (responseDoc.HasMember("code") && responseDoc.HasMember("message")) {
      std::string errorCode = responseDoc["code"].GetString();
      std::string errorMessage = responseDoc["message"].GetString();
      std::cerr << "B2 API Error: " << errorCode << " - " << errorMessage << std::endl;
      return {};
    }

    if (responseDoc.HasMember("uploadUrl") && responseDoc.HasMember("authorizationToken")) {
      result.uploadUrl = responseDoc["uploadUrl"].GetString();
      result.authorizationToken = responseDoc["authorizationToken"].GetString();
      std::cout << "Successfully obtained upload URL and token" << std::endl;
      return result;
    }

    std::cerr << "Response missing required fields (uploadUrl, authorizationToken)" << std::endl;
    return result;
  }
};

class FileSaver
{
public:
  FileSaver() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
  }

  ~FileSaver() {
    setSaveFileThread(false);
    curl_global_cleanup();
  }

  static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
  }

  static size_t readCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fread(ptr, size, nmemb, stream);
  }

  void readFile() {
    if (!std::filesystem::exists(m_filePath)) {
      throw std::runtime_error("File does not exist: " + m_filePath.string());
    }

    std::ifstream file(m_filePath, std::ios::binary);
    if (!file) {
      throw std::runtime_error("Cannot open file: " + m_filePath.string());
    }

    std::string content((std::istreambuf_iterator<char>(file)),
      std::istreambuf_iterator<char>());
    m_fileContent = content;
  }

  void makeLocalCopy() {
    if (!std::filesystem::exists(m_filePath)) {
      throw std::runtime_error("File does not exist: " + m_filePath.string());
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time);

    std::stringstream ss;
    ss << std::put_time(tm, "%Y%m%d_%H%M%S");
    std::string timestamp = ss.str();

    std::filesystem::path localCopyPath = m_filePath.parent_path() /
      (m_filePath.stem().string() + "_backup_" + timestamp + m_filePath.extension().string());

    std::filesystem::copy(m_filePath, localCopyPath, std::filesystem::copy_options::overwrite_existing);
    m_logger += "Local copy created: " + localCopyPath.string() + "\n";
  }

  bool uploadFile() {
    if (!m_b2Credentials.isAuthenticated && !m_b2Credentials.authenticate()) {
      m_logger += "Authentication failed\n";
      return false;
    }

    if (m_b2Credentials.bucketId.empty()) {
      m_logger += "No bucket available\n";
      return false;
    }

    // Get upload authorization (both URL and token)
    UploadAuthorization uploadAuth = m_b2Credentials.getUploadUrl();
    if (uploadAuth.uploadUrl.empty() || uploadAuth.authorizationToken.empty()) {
      m_logger += "Failed to get upload authorization\n";
      return false;
    }

    // Get file SHA1
    std::string fileSha1 = calculateFileSha1(m_filePath.string());

    CURL* curl = curl_easy_init();
    if (!curl) {
      m_logger += "Failed to initialize cURL\n";
      return false;
    }

    FILE* file = fopen(m_filePath.string().c_str(), "rb");
    if (!file) {
      m_logger += "Cannot open file: " + m_filePath.string() + "\n";
      curl_easy_cleanup(curl);
      return false;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Create filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time);

    std::stringstream ss;
    ss << std::put_time(tm, "%Y%m%d_%H%M%S_") << m_filePath.filename().string();
    std::string remoteFileName = ss.str();

    struct curl_slist* headers = nullptr;
    // Use the UPLOAD-SPECIFIC authorization token, not the general one
    headers = curl_slist_append(headers, ("Authorization: " + uploadAuth.authorizationToken).c_str());
    headers = curl_slist_append(headers, ("X-Bz-File-Name: " + remoteFileName).c_str());
    headers = curl_slist_append(headers, ("X-Bz-Content-Sha1: " + fileSha1).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");

    // Add Content-Length header to avoid chunked transfer encoding
    headers = curl_slist_append(headers, ("Content-Length: " + std::to_string(fileSize)).c_str());

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, uploadAuth.uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Use POST with explicit size to avoid chunked encoding
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)fileSize);
    curl_easy_setopt(curl, CURLOPT_READDATA, file);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Disable chunked transfer encoding
    curl_easy_setopt(curl, CURLOPT_HTTP_TRANSFER_DECODING, 0L);

    CURLcode res = curl_easy_perform(curl);

    fclose(file);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
      m_logger += "Upload failed: " + std::string(curl_easy_strerror(res)) + "\n";
      return false;
    }

    // Parse response
    rapidjson::Document doc;
    doc.Parse(response.c_str());

    if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("fileId")) {
      m_logger += "File uploaded successfully: " + remoteFileName + "\n";
      return true;
    }

    m_logger += "Upload failed. Response: " + response + "\n";
    return false;
  }

  void saveFile() {
    while (m_isSaving) {
      try {
        if (!m_isFilePathSet) {
          m_logger += "File path not set\n";
          continue;
        }

        // Make local copy
        makeLocalCopy();

        // Upload to Backblaze B2
        if (uploadFile()) {
          m_logger += "Backup completed successfully\n";
        }
        else {
          m_logger += "Backup failed\n";
        }
      }
      catch (const std::exception& e) {
        m_logger += std::string("Error: ") + e.what() + "\n";
      }

      // Sleep for the specified interval
      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(m_saveInterval * 1000)));
    }
  }

  void setSaveFileThread(bool set) {
    if (set && !m_isSaving) {
      m_isSaving = true;
      m_fileSaver = std::make_unique<std::thread>(&FileSaver::saveFile, this);
      m_fileSaver->detach();
    }
    else if (!set && m_isSaving) {
      m_isSaving = false;
      if (m_fileSaver && m_fileSaver->joinable()) {
        m_fileSaver->join();
      }
      m_fileSaver.reset();
    }
  }

  static std::string calculateFileSha1(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
      return "";
    }

    SHA_CTX context;
    SHA1_Init(&context);

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
      SHA1_Update(&context, buffer, file.gcount());
    }

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1_Final(hash, &context);

    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return ss.str();
  }

public:
  bool m_isFilePathSet = false;
  BackblazeCredentials m_b2Credentials;

  std::filesystem::path m_filePath;
  std::string m_fileContent;
  std::unique_ptr<std::thread> m_fileSaver;
  std::string m_logger;
  float m_saveInterval = 300.0f; // seconds
  bool m_isSaving = false;
};