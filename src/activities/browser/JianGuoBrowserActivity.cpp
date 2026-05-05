#include "JianGuoBrowserActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <base64.h>  // 显式包含Base64头文件

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <HTTPClient.h>
#include "network/HttpDownloader.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"
#include <Epub.h>

#include <cctype>
#include <iomanip>
#include <sstream>

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long goHomeMs = 500;
}

static std::string urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return escaped.str();
}

// ===================== 内嵌ESPWebDAV核心代码（修正版）=====================
class ESPWebDAV {
public:
    void begin(const char* url, const char* user, const char* pass) {
        _baseUrl = url;
        _user = user;
        _pass = pass;
        _timeout = 5000;
        _basePath = "";  // 新增：初始化
    }

    void setTimeout(int timeout) {
        _timeout = timeout;
    }

    // 新增：设置自定义基础路径
    void setBasePath(const char* basePath) {
        _basePath = basePath;
    }

    int propfind(const char* relativePath, String& result) {
        WiFiClientSecure client;
        client.setInsecure();

        const char* host = "dav.jianguoyun.com";
        const int port = 443;

        if (!client.connect(host, port)) {
            Serial.println("[DAV] Failed to connect to server");
            return 0;
        }

        // === 修改：优先使用 _basePath，如果没有则用 SETTINGS.jgBookFolder ===
        String requestPath = String("/dav/");
        if (_basePath.length() > 0) {
            requestPath += _basePath;
        } else {
            requestPath += String(SETTINGS.jgBookFolder);
        }
        
        if (relativePath && strlen(relativePath) > 0) {
            if (requestPath[requestPath.length()-1] != '/') requestPath += "/";
            requestPath += relativePath;
        }
        if (!requestPath.endsWith("/")) requestPath += "/";

        // 【调试】打印路径
        Serial.printf("[DAV] PROPFIND 請求路徑: %s\n", requestPath.c_str());

        // Base64 认证
        String authStr = _user + ":" + _pass;
        String base64Auth = base64::encode(authStr);
        base64Auth.trim();

        client.print("PROPFIND ");
        client.print(requestPath);
        client.println(" HTTP/1.1");
        client.println("Host: dav.jianguoyun.com");
        client.println("Authorization: Basic " + base64Auth);
        client.println("Depth: 1");
        client.println("Content-Length: 0");
        client.println("Connection: close");
        client.println();

        // 读取响应
        unsigned long timeout = millis() + 8000;
        String responseLine = "";
        while (millis() < timeout && client.connected()) {
            if (client.available()) {
                responseLine = client.readStringUntil('\n');
                break;
            }
            delay(1);
        }

        int httpCode = 0;
        if (responseLine.startsWith("HTTP/1.1 ")) {
            httpCode = responseLine.substring(9, 12).toInt();
        } else {
            Serial.println("[DAV] Invalid response: " + responseLine);
            client.stop();
            return 0;
        }

        result = "";
        if (httpCode == 207) {
            timeout = millis() + 8000;
            while ((client.connected() || client.available()) && millis() < timeout) {
                if (client.available()) {
                    result += client.readString();
                } else {
                    delay(10);
                }
            }
        } else {
            while (client.available()) {
                result += client.readString();
            }
            Serial.println("[DAV] Error response: " + result);
        }

        client.stop();
        return httpCode;
    }

    void end() {}

private:
    String _baseUrl;
    String _user;
    String _pass;
    int _timeout;
    String _basePath;  // 新增：自定义基础路径
};


// ===================== 内嵌ESPWebDAV核心代码（结束）=====================

namespace {
constexpr int PAGE_ITEMS = 23;
constexpr int SKIP_PAGE_MS = 700;
}  // namespace

// 静态成员变量初始化
std::vector<WebDAVEntry> JianGuoBrowserActivity::entries;
std::string JianGuoBrowserActivity::currentPath = "";
std::vector<std::string> JianGuoBrowserActivity::navigationHistory;
int JianGuoBrowserActivity::selectorIndex = 0;
std::string JianGuoBrowserActivity::errorMessage;
std::string JianGuoBrowserActivity::statusMessage;
JianGuoBrowserActivity::BrowserState JianGuoBrowserActivity::state;
SemaphoreHandle_t JianGuoBrowserActivity::renderingMutex = nullptr;
TaskHandle_t JianGuoBrowserActivity::displayTaskHandle = nullptr;
bool JianGuoBrowserActivity::updateRequired = false;

// 修复：把endsWith的定义放在这里（头文件只留声明）
bool JianGuoBrowserActivity::endsWith(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

void JianGuoBrowserActivity::taskTrampoline(void* param) {
  auto* self = static_cast<JianGuoBrowserActivity*>(param);
  self->displayTaskLoop();
}

void JianGuoBrowserActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = BrowserState::CHECK_WIFI;
  entries.clear();
  navigationHistory.clear();
  currentPath = "";
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = "檢查WiFi...";
  updateRequired = true;



  xTaskCreate(&JianGuoBrowserActivity::taskTrampoline, "JianGuoBookBrowserTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );

  // 检查WiFi后加载目录
  checkAndConnectWifi();
}

void JianGuoBrowserActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // 关闭WiFi
  WiFi.mode(WIFI_OFF);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  entries.clear();
  navigationHistory.clear();
}

void JianGuoBrowserActivity::loop() {
  // 处理WiFi选择子页面
  if (state == BrowserState::WIFI_SELECTION) {
    ActivityWithSubactivity::loop();
    return;
  }

  // 错误状态：重试/返回
  if (state == BrowserState::ERROR) {
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= goHomeMs) {
      if (WiFi.status() == WL_CONNECTED) {
        state = BrowserState::LOADING;
        statusMessage = "載入中...";
        updateRequired = true;
        fetchFeed(currentPath);
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  // 检查WiFi/加载中：仅返回键可用
  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      exitActivity(); 
    }
    return;
  }

  // 浏览目录状态（核心）
  if (state == BrowserState::BROWSING) {
    const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

    // 确认键：打开文件夹（跳过下载逻辑）
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (!entries.empty()) {
            const auto& entry = entries[selectorIndex];
            if (entry.type == WebDAVEntry::FOLDER) {
                navigateToEntry(entry); // 进入文件夹
            } else if (entry.type == WebDAVEntry::BOOK_FILE) {
                downloadBook(entry); // ← 新增：下载文件
            }
        }
    }
    // 返回键：返回上一级
    else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    } 
    // 上下键：切换选中项
    else if (prevReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS - 1) * PAGE_ITEMS + entries.size()) % entries.size();
      } else {
        selectorIndex = (selectorIndex + entries.size() - 1) % entries.size();
      }
      updateRequired = true;
    } else if (nextReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS + 1) * PAGE_ITEMS) % entries.size();
      } else {
        selectorIndex = (selectorIndex + 1) % entries.size();
      }
      updateRequired = true;
    }
  }
}

void JianGuoBrowserActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void JianGuoBrowserActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // 标题：坚果云文件目录
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "堅果雲檔案目錄", true, EpdFontFamily::BOLD);

  // 检查WiFi状态
  if (state == BrowserState::CHECK_WIFI) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels("返回", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // 加载中状态
  if (state == BrowserState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels("返回", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
 //下载成功
  if (state == BrowserState::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, "下載中...");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, statusMessage.c_str());
    if (downloadTotal > 0) {
        const int barWidth = renderer.getScreenWidth() - 100;
        constexpr int barHeight = 20;
        constexpr int barX = 50;
        const int barY = renderer.getScreenHeight() / 2 + 20;
        GUI.drawProgressBar(renderer, Rect{barX, barY, barWidth, barHeight}, downloadProgress, downloadTotal);
    }
    renderer.displayBuffer();
    return;
}

  // 错误状态
  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "失敗:");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels("返回", "重試", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // 浏览目录状态（核心）
  const auto labels = mappedInput.mapLabels("返回", "開啟", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "當前目錄無檔案/資料夾");
    renderer.displayBuffer();
    return;
  }

  // 绘制目录列表
  const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
  // 选中项高亮
  renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

  // 遍历显示当前页条目
  for (size_t i = pageStartIndex; i < entries.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
    const auto& entry = entries[i];

    // 显示文本：文件夹加>标识，文件直接显示名称
    std::string displayText;
    if (entry.type == WebDAVEntry::FOLDER) {
      displayText = "> " + entry.title;
    } else {
      displayText = entry.title;
    }

    // 截断过长文本
    auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), renderer.getScreenWidth() - 40);
    renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                      i != static_cast<size_t>(selectorIndex));
  }

  renderer.displayBuffer();
}

// 核心：列目录逻辑（仅保留PROPFIND+XML解析）
// 核心：列目录逻辑（支持双文件夹）
void JianGuoBrowserActivity::fetchFeed(const std::string& subPath) {
    std::string username = SETTINGS.jgUsername;
    std::string appPwd = SETTINGS.jgAppPassword;

    if (username.empty() || appPwd.empty()) {
        state = BrowserState::ERROR;
        errorMessage = "請先配置堅果雲賬號/應用密碼";
        updateRequired = true;
        return;
    }

    entries.clear();

    // === 文件夹1：原文件夹 (SETTINGS.jgBookFolder) ===
    {
        ESPWebDAV dav1;
        dav1.begin("https://dav.jianguoyun.com/dav/", username.c_str(), appPwd.c_str());
        dav1.setTimeout(8000);

        Serial.printf("\n[========== 資料夾1: %s ==========]\n", SETTINGS.jgBookFolder);
        String xmlResult1;
        int responseCode1 = dav1.propfind(subPath.c_str(), xmlResult1);
        if (responseCode1 == 207) {
            parseXmlEntries(xmlResult1, subPath, "jg", entries);
            Serial.printf("[JG] 資料夾1 成功，當前共 %d 個條目\n", entries.size());
        } else {
            Serial.printf("[JG] 資料夾1 失敗（碼：%d）\n", responseCode1);
        }
        dav1.end();
    }

    // === 文件夹2：ebooks/ (或其他路径) ===
    {
        ESPWebDAV dav2;
        dav2.begin("https://dav.jianguoyun.com/dav/", username.c_str(), appPwd.c_str());
        dav2.setTimeout(8000);
        dav2.setBasePath("legado/books");  // 新增：设置第二个文件夹路径

        Serial.printf("\n[========== 資料夾2: legado/books ==========]\n");
        String xmlResult2;
        int responseCode2 = dav2.propfind(subPath.c_str(), xmlResult2);
        if (responseCode2 == 207) {
            parseXmlEntries(xmlResult2, subPath, "legado", entries);
            Serial.printf("[JG] 資料夾2 成功，當前共 %d 個條目\n", entries.size());
        } else {
            Serial.printf("[JG] 資料夾2 失敗（碼：%d）\n", responseCode2);
        }
        dav2.end();
    }

    Serial.printf("[================================]\n");
    Serial.printf("[%lu] [JG] 共找到 %d 個條目\n", millis(), entries.size());

    selectorIndex = 0;
    state = BrowserState::BROWSING;
    updateRequired = true;
}

// 新增：XML 解析辅助函数（避免代码重复）
void JianGuoBrowserActivity::parseXmlEntries(const String& xmlResult, 
                                              const std::string& basePath,
                                              const std::string& sourceFolder,
                                              std::vector<WebDAVEntry>& outEntries) {
    int xmlPos = 0;
    while (true) {
        int nameStart = xmlResult.indexOf("<d:displayname>", xmlPos);
        if (nameStart == -1) break;
        nameStart += 15;
        int nameEnd = xmlResult.indexOf("</d:displayname>", nameStart);
        if (nameEnd == -1) break;
        std::string fileName = xmlResult.substring(nameStart, nameEnd).c_str();
        xmlPos = nameEnd;

        if (fileName == "." || fileName == "..") continue;

        int isFolder = xmlResult.indexOf("<d:collection>", nameStart - 200);
        if (isFolder == -1 || isFolder > nameStart + 200) {
            isFolder = -1;
        }

        WebDAVEntry entry;
        entry.title = fileName;
        entry.path = basePath.empty() ? fileName : basePath + "/" + fileName;
        entry.type = (isFolder != -1) ? WebDAVEntry::FOLDER : WebDAVEntry::BOOK_FILE;
        entry.sourceFolder = sourceFolder;  // 标记来源

        // 文件类型过滤
        if (entry.type == WebDAVEntry::BOOK_FILE) {
            if (!endsWith(entry.title, ".epub") && !endsWith(entry.title, ".txt")
                && !endsWith(entry.title, ".xtc") && !endsWith(entry.title, ".xtch")
                && !endsWith(entry.title, ".pngtxt") && !endsWith(entry.title, ".epdfont")) {
                continue;
            }
        }
        outEntries.push_back(entry);
    }
}

// 文件夹跳转逻辑
void JianGuoBrowserActivity::navigateToEntry(const WebDAVEntry& entry) {
  if (entry.type != WebDAVEntry::FOLDER) return;

  // 记录历史路径
  navigationHistory.push_back(currentPath);
  currentPath = entry.path;

  // 加载子目录
  state = BrowserState::LOADING;
  statusMessage = "載入中...";
  entries.clear();
  selectorIndex = 0;
  updateRequired = true;

  fetchFeed(currentPath);
}

// 返回上一级目录
void JianGuoBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    exitActivity(); // 根目录返回直接退出
  } else {
    // 回到上一级
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();

    state = BrowserState::LOADING;
    statusMessage = "載入中...";
    entries.clear();
    selectorIndex = 0;
    updateRequired = true;

    fetchFeed(currentPath);
  }
}

// WiFi检查逻辑
void JianGuoBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = "載入中...";
    updateRequired = true;
    fetchFeed(currentPath);
    return;
  }

  // 未连接则启动WiFi选择
  launchWifiSelection();
}

// 启动WiFi选择页面
void JianGuoBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  updateRequired = true;

  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

// WiFi选择完成回调
void JianGuoBrowserActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();

  if (connected) {
    Serial.printf("[%lu] [JG] WiFi已連線，載入目錄\n", millis());
    state = BrowserState::LOADING;
    statusMessage = "載入中...";
    updateRequired = true;
    fetchFeed(currentPath);
  } else {
    Serial.printf("[%lu] [JG] WiFi連線失敗\n", millis());
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    state = BrowserState::ERROR;
    errorMessage = "WiFi連線失敗";
    updateRequired = true;
  }
}


void JianGuoBrowserActivity::downloadBook(const WebDAVEntry& book) {
    state = BrowserState::DOWNLOADING;
    statusMessage = book.title;
    downloadProgress = 0;
    downloadTotal = 0;
    updateRequired = true;

    // === 根据来源文件夹构建 URL ===
    std::string downloadUrl;
    if (book.sourceFolder == "legado") {
        downloadUrl = "https://dav.jianguoyun.com/dav/legado/books/";
    } else {
        downloadUrl = std::string("https://dav.jianguoyun.com/dav/") + SETTINGS.jgBookFolder+"/";
    }
    
    // 添加子路径
    std::string folderPath = book.path;
    size_t lastSlash = folderPath.rfind('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        folderPath = folderPath.substr(0, lastSlash);
        downloadUrl += folderPath + "/";
    }
    
    downloadUrl += urlEncode(book.title);
    Serial.printf("[%lu] [JG] 準備下載: %s (來源:%s)\n", millis(), 
                  downloadUrl.c_str(), book.sourceFolder.c_str());

    // === 根据扩展名选择本地保存目录 ===
    std::string targetDir;
    if (endsWith(book.title, ".pngtxt")) {
        targetDir = "/sleep_mask/";
    } else if (endsWith(book.title, ".epdfont")) {
        targetDir = "/fonts";
    } else {
        targetDir = "/堅果雲";
    }

    if (!targetDir.empty()) {
        SdMan.mkdir(targetDir.c_str());
    }

    std::string safeFilename = StringUtils::sanitizeFilename(book.title,200);
    std::string localPath;
    if (targetDir.empty()) {
        localPath = "/" + safeFilename;
    } else {
        localPath = targetDir + "/" + safeFilename;
    }

    Serial.printf("[%lu] [JG] Downloading: %s -> %s\n", millis(), 
                  downloadUrl.c_str(), localPath.c_str());

    const auto result = HttpDownloader::downloadToFile_jg(
        downloadUrl,
        localPath,
        [this](size_t downloaded, size_t total) {
            downloadProgress = downloaded;
            downloadTotal = total;
            updateRequired = true;
        }
    );

    if (result == HttpDownloader::OK) {
        Serial.printf("[%lu] [JG] 下載完成: %s\n", millis(), localPath.c_str());

        if (endsWith(book.title, ".epub")) {
            Epub epub(localPath, "/.crosspoint");
            epub.clearCache();
        }

        state = BrowserState::BROWSING;
        updateRequired = true;
    } else {
        state = BrowserState::ERROR;
        errorMessage = "下載失敗";
        updateRequired = true;
    }
}