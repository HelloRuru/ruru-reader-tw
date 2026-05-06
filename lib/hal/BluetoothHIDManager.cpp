#include "BluetoothHIDManager.h"
#include <NimBLEDevice.h>
#include <HalGPIO.h>
#include <WiFi.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <utility>
#include "../../src/CrossPointSettings.h"

// HID Service and characteristic UUIDs
static const char* HID_SERVICE_UUID = "1812";
static const char* HID_REPORT_UUID = "2A4D";
static const char* HID_INFO_UUID = "2A4A";

// Global static for singleton
static BluetoothHIDManager* g_instance = nullptr;

// Scan callbacks for NimBLE 2.x - keep as static to ensure it stays alive
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    Serial.printf("BT onResult callback triggered!\n");
    if (g_instance) {
      // onScanResult expects non-const pointer, need to cast
      g_instance->onScanResult(const_cast<NimBLEAdvertisedDevice*>(advertisedDevice));
    } else {
      Serial.printf("BT onResult called but g_instance is NULL!");
    }
  }
  
  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    Serial.printf("BT onScanEnd callback: %d devices, reason: %d", results.getCount(), reason);
    // 配合非阻塞掃描：scan 結束時清 flag，UI 可以判斷掃描完成
    if (g_instance) {
      g_instance->_setScanningFinished();
    }
  }
};

// Static instance to keep callbacks alive during scan
static ScanCallbacks scanCallbacks;

// Client connection callbacks
class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) override {
    Serial.printf("BT Client connected: %s", pClient->getPeerAddress().toString().c_str());
  }
  
  void onDisconnect(NimBLEClient* pClient, int reason) override {
    Serial.printf("BT Client disconnected: %s (reason: %d)", pClient->getPeerAddress().toString().c_str(), reason);
    if (g_instance) {
      g_instance->onClientDisconnected(pClient->getPeerAddress().toString());
    }
  }
};

BluetoothHIDManager& BluetoothHIDManager::getInstance() {
  if (!g_instance) {
    g_instance = new BluetoothHIDManager();
    Serial.printf("BT BluetoothHIDManager instance created");
  }
  return *g_instance;
}

BluetoothHIDManager::BluetoothHIDManager() {
  Serial.printf("BT BluetoothHIDManager constructor");
  // 方案 B：預留空間避免 push_back 觸發 realloc 移動 ConnectedDevice，
  // ConnectedDevice 含裸指標（NimBLEClient*、reportChars vector），
  // realloc 時 NimBLE 異步 callback 可能 access 到舊位置
  _connectedDevices.reserve(4);
  _discoveredDevices.reserve(16);
}

BluetoothHIDManager::~BluetoothHIDManager() {
  cleanup();
}

void BluetoothHIDManager::cleanup() {
  if (_enabled) {
    disable();
  }
}

bool BluetoothHIDManager::enable() {
  if (_enabled) {
    Serial.printf("BT Already enabled");
    return true;
  }
  
  Serial.printf("BT Enabling Bluetooth...");
  
  // CRITICAL: Disable WiFi when enabling Bluetooth
  // ESP32-C3 cannot have both WiFi and BLE enabled simultaneously
  if (WiFi.getMode() != WIFI_OFF) {
    Serial.printf("BT Disabling WiFi to enable Bluetooth (mutual exclusion)");
    WiFi.disconnect(true);  // true = turn off WiFi radio
    WiFi.mode(WIFI_OFF);
    delay(100);  // Brief delay to ensure WiFi is fully powered down
  }
  
  try {
    // Initialize NimBLE stack
    NimBLEDevice::init("CrossPoint");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // +9dBm
    NimBLEDevice::setSecurityAuth(true, false, true);
    
    _enabled = true;
    lastError = "";
    
    Serial.printf("BT Bluetooth enabled successfully");
    loadState();
    
    // NOTE: background auto‑connect was removed in order to conserve memory
    // and avoid multiple concurrent connection attempts.  callers such as
    // main.cpp or the settings UI should invoke connectToDeviceWithRetries()
    // explicitly when appropriate.
    
    return true;
  } catch (const std::exception& e) {
    Serial.printf("BT Failed to enable Bluetooth: %s", e.what());
    lastError = std::string("Init failed: ") + e.what();
    _enabled = false;
    return false;
  } catch (...) {
    Serial.printf("BT Failed to enable Bluetooth: unknown error");
    lastError = "Init failed: unknown error";
    _enabled = false;
    return false;
  }
}

bool BluetoothHIDManager::disable() {
  if (!_enabled) {
    Serial.printf("BT Already disabled");
    return true;
  }
  
  Serial.printf("BT Disabling Bluetooth...");
  
  if (_scanning) {
    stopScan();
  }
  
  // Disconnect all devices
  while (!_connectedDevices.empty()) {
    disconnectFromDevice(_connectedDevices[0].address);
  }
  
  // Deinitialize NimBLE stack
  NimBLEDevice::deinit(true);
  
  _enabled = false;
  lastError = "";
  
  Serial.printf("BT Bluetooth disabled");
  return true;
}

void BluetoothHIDManager::startScan(uint32_t durationMs) {
  if (!_enabled || _scanning) {
    Serial.printf("BT Cannot scan: enabled=%d scanning=%d", _enabled, _scanning);
    return;
  }
  
  Serial.printf("BT Starting BLE scan for %lu ms", durationMs);
  _scanning = true;
  _discoveredDevices.clear();
  
  try {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (!pScan) {
      Serial.printf("BT Failed to get scan object");
      _scanning = false;
      return;
    }
    
    Serial.printf("BT Setting up scan callbacks...");
    // Use static callbacks object to ensure it stays alive
    pScan->setScanCallbacks(&scanCallbacks, false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    // 改成非阻塞掃描：NimBLE 自己用 duration 計時、callback 回到 onScanEnd
    // 原本 delay(durationMs) 會卡住 main loop 10 秒，UI 期間都不能操作
    Serial.printf("BT Starting non-blocking scan for %lu ms...", durationMs);
    bool started = pScan->start(durationMs, false);

    if (!started) {
      Serial.printf("BT Failed to start scan!");
      _scanning = false;
      return;
    }

    Serial.printf("BT Scan started in background (duration handled by NimBLE)");
    // 不再 delay 阻塞，UI 可以即時更新
    // _scanning 會在 onScanEnd callback 中被清為 false（如果需要）
    // 這裡先不清，避免 UI 端誤判已完成
  } catch (const std::exception& e) {
    Serial.printf("BT Scan failed: %s", e.what());
    _scanning = false;
    lastError = std::string("Scan failed: ") + e.what();
  }
}


void BluetoothHIDManager::stopScan() {
  if (!_scanning) return;
  
  Serial.printf("BT Stopping scan");
  
  try {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan) {
      pScan->stop();
    }
  } catch (...) {
    Serial.printf("BT Error stopping scan");
  }
  
  _scanning = false;
}

void BluetoothHIDManager::onScanResult(NimBLEAdvertisedDevice* advertisedDevice) {
  if (!advertisedDevice) return;
  
  std::string address = advertisedDevice->getAddress().toString();
  std::string name = advertisedDevice->getName();
  int rssi = advertisedDevice->getRSSI();
  
  // Check if device advertises HID service
  bool isHID = advertisedDevice->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID));
  
  // Check if we already have this device
  for (auto& dev : _discoveredDevices) {
    if (dev.address == address) {
      dev.rssi = rssi; // Update RSSI
      if (isHID) dev.isHID = true;
      return;
    }
  }
  
  // Add new device
  BluetoothDevice device;
  device.address = address;
  device.name = name.empty() ? "Unknown" : name;
  device.rssi = rssi;
  device.isHID = isHID;
  // 方案 A：從掃描取得實際地址類型（避免 connect 時寫死 RANDOM 連不上 public 裝置）
  device.addressType = advertisedDevice->getAddressType();

  _discoveredDevices.push_back(device);

    const std::string prefix = (address.size() >= 8) ? address.substr(0, 8) : address;
    Serial.printf("BT Scan device: %s (%s) prefix=%s RSSI:%d HID:%d",
      device.name.c_str(), device.address.c_str(), prefix.c_str(), rssi, isHID);
  
  Serial.printf("BT Found device: %s (%s) RSSI:%d HID:%d", 
          device.name.c_str(), device.address.c_str(), rssi, isHID);
}



bool BluetoothHIDManager::connectToDevice(const std::string& address) {
  if (!_enabled) {
    Serial.printf("BT Cannot connect: Bluetooth not enabled");
    lastError = "Bluetooth not enabled";
    return false;
  }
  if (address.empty()) {
    Serial.printf("BT Invalid address (empty) passed to connectToDevice");
    lastError = "Invalid address";
    return false;
  }
  
  // Check if already connected
  if (isConnected(address)) {
    Serial.printf("BT Already connected to %s", address.c_str());
    return true;
  }
  
  // 必须在扫描列表中，順便取得地址類型（方案 A）
  bool seen = false;
  uint8_t scannedAddrType = 0;
  for (const auto& dev : _discoveredDevices) {
    if (dev.address == address) {
      seen = true;
      if (!dev.isHID) {
        Serial.printf("BT Device %s not advertising HID, aborting connect", address.c_str());
        lastError = "Not HID device";
        return false;
      }
      scannedAddrType = dev.addressType;
      break;
    }
  }
  if (!seen) {
    Serial.printf("BT Device %s not in scan results (skipping requirement)", address.c_str());
    return false;
  }
  
  Serial.printf("BT Connecting to device %s", address.c_str());
  
  NimBLEClient* pClient = nullptr;
  try {
    pClient = NimBLEDevice::createClient();
    if (!pClient) {
      lastError = "Failed to create client";
      Serial.printf("BT Failed to create BLE client");
      return false;
    }
    // 方案 C：讓 NimBLE 自己管理 client 生命週期
    // 原 setSelfDelete(false, false) + 手動 deleteClient 會造成 double-free，
    // 因為 NimBLE 在 onDisconnect 仍會 cleanup pClient，再呼叫 deleteClient 就崩。
    // 改成 setSelfDelete(true, true)，並移除所有手動 deleteClient 呼叫。
    pClient->setSelfDelete(true, true);
    
    static ClientCallbacks clientCallbacks;
    // clientCallbacks 是靜態物件，不能交給 NimBLE delete，否則 client 自刪時會 free 到非 heap 記憶體
    pClient->setClientCallbacks(&clientCallbacks, false);

    // 方案 A：用掃描時記下的真實地址類型（原寫死 BLE_ADDR_RANDOM 對 public 地址裝置會走進失敗路徑、觸發 cleanup race）
    // 修正：NimBLE 的 getAddressType() 對某些廣播器會回 0 (PUBLIC) 但實際是 random
    // 從 MAC 第一個 byte bit[7:6] 自己判：11=static random, 01=RPA, 00=NRPA, 其他=public
    uint8_t finalAddrType = scannedAddrType;
    if (address.size() >= 2) {
      // 解析第一個 byte（hex string "41:42:..." -> 0x41）
      auto hexNibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int hi = hexNibble(address[0]);
      int lo = hexNibble(address[1]);
      if (hi >= 0 && lo >= 0) {
        uint8_t firstByte = (uint8_t)((hi << 4) | lo);
        uint8_t topBits = firstByte >> 6;
        uint8_t inferredType;
        if (topBits == 0b11) {
          inferredType = BLE_ADDR_RANDOM;        // static random
        } else if (topBits == 0b01) {
          inferredType = BLE_ADDR_RANDOM;        // resolvable private (RPA) - NimBLE 用 RANDOM 處理
        } else if (topBits == 0b00) {
          inferredType = BLE_ADDR_RANDOM;        // non-resolvable private
        } else {
          inferredType = BLE_ADDR_PUBLIC;        // public address
        }
        if (inferredType != scannedAddrType) {
          Serial.printf("BT addrType override: scan said %d but MAC top bits 0x%02x suggest %d",
                        scannedAddrType, firstByte, inferredType);
          finalAddrType = inferredType;
        }
      }
    }
    NimBLEAddress bleAddress(address, finalAddrType);
    Serial.printf("BT Using addrType=%d for %s", finalAddrType, address.c_str());
    
    if (!pClient->connect(bleAddress)) {
      lastError = "Connection failed";
      Serial.printf("BT Failed to connect to %s", address.c_str());
      // 方案 C：setSelfDelete(true, true) 後，NimBLE 自動 cleanup，不再手動 delete
      return false;
    }
    
    Serial.printf("BT Connected, discovering services...");
    
    NimBLERemoteService* pService = pClient->getService(HID_SERVICE_UUID);
    if (!pService) {
      lastError = "HID service not found";
      Serial.printf("BT Device %s doesn't have HID service", address.c_str());
      pClient->disconnect();
      // 方案 C：disconnect 後 NimBLE 自動 cleanup
      return false;
    }

    Serial.printf("BT Found HID service, enumerating report characteristics...");
    
    auto pCharacteristics = pService->getCharacteristics(true);
    NimBLERemoteCharacteristic* pReportChar = nullptr;
    
    int reportCount = 0;
    std::vector<NimBLERemoteCharacteristic*> reportChars;
    
    for (auto it = pCharacteristics.begin(); it != pCharacteristics.end(); ++it) {
      auto* pChar = *it;
      Serial.printf("BT Characteristic UUID: %s, canRead:%d canWrite:%d canNotify:%d canIndicate:%d",
              pChar->getUUID().toString().c_str(),
              pChar->canRead(), pChar->canWrite(), pChar->canNotify(), pChar->canIndicate());
      
      if (pChar->getUUID().equals(NimBLEUUID(HID_REPORT_UUID))) {
        reportCount++;
        Serial.printf("BT Found Report char #%d, notify:%d indicate:%d UUID:%s", 
                reportCount, pChar->canNotify(), pChar->canIndicate(),
                pChar->getUUID().toString().c_str());
        
        if (pChar->canNotify() || pChar->canIndicate()) {
          reportChars.push_back(pChar);
          Serial.printf("BT Added Report char #%d for subscription", reportCount);
        }
      }
    }
    
    if (reportChars.empty()) {
      lastError = "No input report characteristic found";
      Serial.printf("BT No Report characteristic with notify/indicate found");
      pClient->disconnect();
      // 方案 C：disconnect 後 NimBLE 自動 cleanup
      return false;
    }
    
    Serial.printf("BT Subscribing to %d Report characteristics...", reportChars.size());
    
    for (size_t i = 0; i < reportChars.size(); i++) {
      auto* pChar = reportChars[i];
      Serial.printf("BT Subscribing to Report char #%d...", i + 1);
      bool subResult = pChar->subscribe(true, onHIDNotify);
      Serial.printf("BT Report char #%d subscribe result: %d", i + 1, subResult);
      if (!subResult) {
        Serial.printf("BT Failed to subscribe to Report char #%d (continuing)", i + 1);
      }
    }
    
    ConnectedDevice connDev;
    connDev.address = address;
    connDev.client = pClient;
    connDev.reportChars = reportChars;
    connDev.subscribed = true;
    connDev.lastActivityTime = millis();
    connDev.wasConnected = true;
    
    bool foundInScan = false;
    for (const auto& dev : _discoveredDevices) {
      if (dev.address == address) {
        connDev.name = dev.name;
        foundInScan = true;
        Serial.printf("BT Device found in scan results: %s (%s)", dev.name.c_str(), address.c_str());
        break;
      }
    }
    
    if (!foundInScan) {
      Serial.printf("BT Device not in scan results (may be previously paired): %s", address.c_str());
    }
    
    connDev.profile = DeviceProfiles::findDeviceProfile(address.c_str(), connDev.name.c_str());
    
    if (connDev.profile) {
      Serial.printf("BT ✓ Using device profile: %s (byte[%d] for keycode)", 
              connDev.profile->name, connDev.profile->reportByteIndex);
    } else {
      Serial.printf("BT No known profile matched for %s, will auto-detect from HID codes", address.c_str());
    }
    
    _connectedDevices.push_back(connDev);
    
    Serial.printf("BT Successfully connected to %s", address.c_str());
    lastError = "Connected";
    
    saveLastConnectedDevice(address, connDev.name);
    
    return true;
    
  } catch (const std::exception& e) {
    lastError = std::string("Connection error: ") + e.what();
    Serial.printf("BT %s", lastError.c_str());
    if (pClient) {
      try {
        pClient->disconnect();  // 方案 C：disconnect 後 NimBLE 自動 cleanup
      } catch (...) {
        Serial.printf("BT Warning: failed to disconnect after exception");
      }
    }
    return false;
  } catch (...) {
    lastError = "Unknown connection error";
    Serial.printf("BT %s", lastError.c_str());
    if (pClient) {
      try {
        pClient->disconnect();  // 方案 C：disconnect 後 NimBLE 自動 cleanup
      } catch (...) {
        Serial.printf("BT Warning: failed to disconnect after unknown exception");
      }
    }
    return false;
  }
}

// Simple retry wrapper around connectToDevice.  It invokes the single-
// attempt function up to `maxAttempts` times with a short delay between
// tries.  This keeps higher-level code (UI) from having to loop itself.
bool BluetoothHIDManager::connectToDeviceWithRetries(const std::string& address, int maxAttempts) {
  if (!_enabled) {
    Serial.printf("BT Cannot connect (retries): Bluetooth not enabled");
    lastError = "Bluetooth not enabled";
    return false;
  }
  if (address.empty()) {
    Serial.printf("BT Invalid address (empty) passed to connectToDeviceWithRetries");
    lastError = "Invalid address";
    return false;
  }
  if (isConnected(address)) {
    Serial.printf("BT Already connected to %s (retries)", address.c_str());
    return true;
  }
  if (maxAttempts <= 0) maxAttempts = 1;
  for (int i = 0; i < maxAttempts; ++i) {
    Serial.printf("BT retry %d/%d for %s", i + 1, maxAttempts, address.c_str());
    if (connectToDevice(address)) {
      return true;
    }
    delay(200);
  }
  return false;
}

bool BluetoothHIDManager::disconnectFromDevice(const std::string& address) {
  Serial.printf("BT Disconnecting from device %s", address.c_str());
  
  auto it = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
    [&address](const ConnectedDevice& dev) { return dev.address == address; });
  
  if (it != _connectedDevices.end()) {
    NimBLEClient* client = it->client;
    _connectedDevices.erase(it);
    _manualDisconnectSuppressUntil = millis() + 10000;

    // 先從清單移除，再做 disconnect，避免 selfDelete 後留下野指標
    if (client && client->isConnected()) {
      try {
        Serial.printf("BT Calling disconnect on client...");
        client->disconnect();
      } catch (const std::exception& e) {
        Serial.printf("BT Error during disconnect: %s", e.what());
      } catch (...) {
        Serial.printf("BT Unknown error during disconnect");
      }
    }

    Serial.printf("BT Disconnected from %s", address.c_str());
    return true;
  }
  
  Serial.printf("BT Device %s not in connected list", address.c_str());
  return false;
}

bool BluetoothHIDManager::isConnected(const std::string& address) const {
  return std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
    [&address](const ConnectedDevice& dev) { return dev.address == address; }) != _connectedDevices.end();
}

std::vector<std::string> BluetoothHIDManager::getConnectedDevices() const {
  std::vector<std::string> addresses;
  for (const auto& dev : _connectedDevices) {
    addresses.push_back(dev.address);
  }
  return addresses;
}

ConnectedDevice* BluetoothHIDManager::findConnectedDevice(const std::string& address) {
  auto it = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
    [&address](const ConnectedDevice& dev) { return dev.address == address; });
  
  if (it != _connectedDevices.end()) {
    return &(*it);
  }
  return nullptr;
}

void BluetoothHIDManager::onClientDisconnected(const std::string& address) {
  if (address.empty()) {
    return;
  }

  const size_t before = _connectedDevices.size();
  _connectedDevices.erase(std::remove_if(_connectedDevices.begin(), _connectedDevices.end(),
                                         [&address](const ConnectedDevice& dev) {
                                           return dev.address == address;
                                         }),
                          _connectedDevices.end());

  if (_connectedDevices.size() != before) {
    Serial.printf("BT Removed stale connection state for %s", address.c_str());
  }
}

void BluetoothHIDManager::pruneDisconnectedDevices(bool logDetails) {
  for (auto it = _connectedDevices.begin(); it != _connectedDevices.end();) {
    const bool disconnected = (!it->client) || (!it->client->isConnected());
    if (disconnected) {
      if (logDetails) {
        Serial.printf("BT Pruning disconnected device entry: %s", it->address.c_str());
      }
      it = _connectedDevices.erase(it);
    } else {
      ++it;
    }
  }
}

void BluetoothHIDManager::processInputEvents() {
  // Input events are processed via notifications callback
  // This method is kept for potential polling-based implementations
}

void BluetoothHIDManager::setInputCallback(std::function<void(uint16_t)> callback) {
  _inputCallback = callback;
  Serial.printf("BT Input callback registered");
}

void BluetoothHIDManager::setButtonInjector(std::function<void(uint8_t)> injector) {
  _buttonInjector = injector;
  Serial.printf("BT Button injector registered");
}

bool BluetoothHIDManager::hasRecentActivity() const {
  // Check if any connected device has had activity in the last 4 minutes
  // This prevents power sleep while using BLE controller
  unsigned long now = millis();
  for (const auto& device : _connectedDevices) {
    if (device.lastActivityTime > 0) {
      unsigned long timeSinceActivity = now - device.lastActivityTime;
      if (timeSinceActivity < 240000) {  // 4 minute (240 second) threshold to keep BLE alive
        return true;
      }
    }
  }
  return false;
}

// Static callback for HID notifications
void BluetoothHIDManager::onHIDNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (!g_instance || !pData || length == 0) return;
  
  // Log raw data
  char hexStr[128] = {0};
  int offset = 0;
  for (size_t i = 0; i < length && i < 16; i++) {
    offset += snprintf(hexStr + offset, sizeof(hexStr) - offset, "%02X ", pData[i]);
  }
  Serial.printf("BT HID Report (%d bytes): %s", length, hexStr);
  
  // Get device
  ConnectedDevice* device = nullptr;
  if (pChar && pChar->getRemoteService()) {
    auto client = pChar->getRemoteService()->getClient();
    if (client) {
      std::string deviceAddr = client->getPeerAddress().toString();
      device = g_instance->findConnectedDevice(deviceAddr);
    }
  }
  if (!device) return;

  device->lastActivityTime = millis();

  uint8_t  keycode = 0x00;
  bool    isPressed = false;

  if (length < 2) return;

  if (device->profile) {
    // 原有设备逻辑不变
    if (length >= device->profile->reportByteIndex + 1) {
      keycode = pData[device->profile->reportByteIndex];
    }
    if (strcmp(device->profile->name, "IINE Game Brick") == 0) {
      isPressed = (pData[0] & 0x01) != 0;
    } else {
      isPressed = (keycode != 0x00);
    }
  } else {
    if (length == 3) {
      uint8_t modifier = pData[0];
      keycode = pData[2];
      if (modifier == 0x01 || keycode == 0x4B) {
        keycode = 0x4B; isPressed = true;
      } else if (modifier == 0x02 || keycode == 0x4E) {
        keycode = 0x4E; isPressed = true;
      } else {
        isPressed = (keycode != 0x00);
      }
    }
    // ====================== 6字节翻页器（正确边沿触发版）======================
    else if (length == 6) {
      uint8_t key = pData[0];
      
      if (key == 0x01) {
        keycode = 0x4B;
        isPressed = true;
        Serial.printf("BT 6键翻页器 [上一页] 按下\n");
      }
      else if (key == 0x02) {
        keycode = 0x4E;
        isPressed = true;
        Serial.printf("BT 6键翻页器 [下一页] 按下\n");
      }
      else {
        keycode = 0x00;
        isPressed = false;
      }
    }
    // ======================================================================
    else if (length >= 5) {
      uint8_t byte4 = pData[4];
      if (byte4 == 0x07 || byte4 == 0x09) {
        keycode = byte4;
        isPressed = (pData[0] & 0x01) != 0;
      } else {
        keycode = (length > 2) ? pData[2] : 0;
        isPressed = (keycode != 0);
      }
    } else {
      keycode = (length > 2) ? pData[2] : 0;
      isPressed = (keycode != 0);
    }
  }

  // ====================== 【保留边沿触发】======================
  bool wasPressedPrevious = device->lastButtonState;

  // 松开 → 更新状态，不动作
  if (!isPressed) {
    device->lastButtonState = false;
    device->lastHIDKeycode = 0x00;
    return;
  }

  // 已经按住 → 不重复触发
  if (wasPressedPrevious) {
    return;
  }

  // ====================== 【唯一一次触发：按下瞬间】======================
  device->lastButtonState = true;
  device->lastHIDKeycode = keycode;

  Serial.printf("BT >>> BUTTON PRESSED: keycode=0x%02X <<<\n", keycode);

  if (g_instance->_buttonInjector) {
    uint8_t btn = g_instance->mapKeycodeToButton(keycode, device->profile);
    if (btn != 0xFF) {
      unsigned long now = millis();
      if (now - device->lastInjectionTime >= 150) {
        String buttonName = (btn == HalGPIO::BTN_DOWN) ? "PageForward" : "PageBack";
        Serial.printf("BT Mapped key -> %s\n", buttonName.c_str());
        Serial.printf("BT Injecting button: %d\n", btn);
        g_instance->_buttonInjector(btn);
        device->lastInjectionTime = now;
      }
    }
  }

  if (g_instance->_inputCallback) {
    g_instance->_inputCallback(keycode);
  }
}


uint16_t BluetoothHIDManager::parseHIDReport(uint8_t* data, size_t length) {
  if (length < 3) {
    Serial.printf("BT Invalid HID report length: %d", length);
    return 0;
  }
  
  uint8_t modifier = data[0];
  uint8_t keycode = data[2]; // First key in the report
  
  // If no key pressed (all zeros), return 0
  if (keycode == 0 && modifier == 0) {
    return 0;
  }
  
  // Log non-empty reports
  Serial.printf("BT HID Report: mod=0x%02X key=0x%02X", modifier, keycode);
  
  // Combine modifier and keycode (modifier in upper byte, keycode in lower)
  uint16_t combined = (static_cast<uint16_t>(modifier) << 8) | keycode;
  
  return combined;
}

// Map HID keycodes to navigator buttons based on device profile
// Only maps keycodes that match the current device's profile to prevent
// unwanted D-pad or other button inputs from triggering page turns
uint8_t BluetoothHIDManager::mapKeycodeToButton(uint8_t keycode, const DeviceProfiles::DeviceProfile* profile) {
  // Log keycode for debugging
  if (keycode != 0x00) {
    Serial.printf("BT mapKeycodeToButton() called with keycode: 0x%02X", keycode);
  }
  
  // If we have a device profile, ONLY map keycodes specific to that profile
  if (profile) {
    if (keycode == profile->pageUpCode) {
      Serial.printf("BT Matched profile pageUpCode 0x%02X (%s) -> PageBack", keycode, profile->name);
      return HalGPIO::BTN_UP;
    } else if (keycode == profile->pageDownCode) {
      Serial.printf("BT Matched profile pageDownCode 0x%02X (%s) -> PageForward", keycode, profile->name);
      return HalGPIO::BTN_DOWN;
    } else {
      // Not a profile-mapped keycode - ignore it
      Serial.printf("BT Keycode 0x%02X not in profile %s (expecting 0x%02X/0x%02X), ignoring", 
              keycode, profile->name, profile->pageUpCode, profile->pageDownCode);
      return 0xFF;
    }
  }
  
  // No profile - fall back to generic HID consumer codes only
  switch (keycode) {
    // 翻页器核心映射（按需修改）
    case 0x01:   // 左Ctrl → 上一页
    case 0x4B:   // PageUp → 上一页
    case 0xE9:   // Consumer PageUp → 上一页
      Serial.printf("BT Mapped key 0x%02X -> PageBack", keycode);
      return HalGPIO::BTN_UP;
    
    case 0x02:   // 左Shift → 下一页
    case 0x4E:   // PageDown → 下一页
    case 0xEA:   // Consumer PageDown → 下一页
      Serial.printf("BT Mapped key 0x%02X -> PageForward", keycode);
      return HalGPIO::BTN_DOWN;

    case 0x00:   // 忽略释放事件
      return 0xFF;

    default:
      Serial.printf("BT Unmapped keycode: 0x%02X (翻页器未匹配)", keycode);
      return 0xFF;
  }
}

void BluetoothHIDManager::updateActivity() {
  pruneDisconnectedDevices(false);

  // Check inactivity timeouts every 10 seconds
  unsigned long now = millis();
  if (now - lastMaintenanceCheck < 10000) {
    return;
  }
  lastMaintenanceCheck = now;
  
  // Check for inactive connections
  for (auto& device : _connectedDevices) {
    if (device.lastActivityTime > 0) {
      unsigned long inactiveTime = now - device.lastActivityTime;
      if (inactiveTime > INACTIVITY_TIMEOUT_MS) {
        Serial.printf("BT Device %s inactive for %lu ms, disconnecting", device.address.c_str(), inactiveTime);
        disconnectFromDevice(device.address);
        break;  // List modified, exit loop
      }
    }
  }
}

void BluetoothHIDManager::checkAutoReconnect() {
  if (!_enabled) {
    return;
  }

  // [stage9.1] 使用者在藍芽設定 UI 操作中時，暫停 auto-reconnect
  // 避免使用者要連新裝置時 reader 卻在嘗試 connectToDeviceWithRetries 連舊裝置（30s timeout）而卡住 UI
  if (_uiBluetoothActive) {
    return;
  }

  // Check for devices that were previously connected but are now disconnected
  // Attempt to reconnect to them automatically
  static unsigned long lastReconnectCheck = 0;
  unsigned long now = millis();
  
  // Only check every 5 seconds to avoid hammering
  if (now - lastReconnectCheck < 5000) {
    return;
  }
  lastReconnectCheck = now;

  pruneDisconnectedDevices(false);

  if (!_connectedDevices.empty() || _scanning || now < _manualDisconnectSuppressUntil) {
    return;
  }

  std::string address;
  std::string name;
  if (!loadLastConnectedDevice(address, name) || address.empty()) {
    return;
  }

  auto discoveredIt = std::find_if(_discoveredDevices.begin(), _discoveredDevices.end(),
                                   [&address](const BluetoothDevice& dev) { return dev.address == address; });

  if (discoveredIt == _discoveredDevices.end()) {
    Serial.printf("BT Auto-reconnect: scanning for %s", address.c_str());
    startScan(3000);
    return;
  }

  if (!discoveredIt->isHID) {
    Serial.printf("BT Auto-reconnect skipped: %s is not advertising HID", address.c_str());
    return;
  }

  Serial.printf("BT Auto-reconnect attempting %s (%s)", name.c_str(), address.c_str());
  if (connectToDeviceWithRetries(address, 1)) {
    Serial.printf("BT Auto-reconnected to %s", address.c_str());
  } else {
    Serial.printf("BT Auto-reconnect to %s failed: %s", address.c_str(), lastError.c_str());
  }
}

void BluetoothHIDManager::saveState() {
  Serial.printf("BT Saving state (stub)");
  // Stub: would save paired devices to file
}

void BluetoothHIDManager::loadState() {
  Serial.printf("BT Loading state (stub)");
  // Stub: would load paired devices from file
}

void BluetoothHIDManager::saveLastConnectedDevice(const std::string& address, const std::string& name) {
  Serial.printf("BT Saving last connected device: %s (%s)", name.c_str(), address.c_str());
  
  // Make sure the directory exists
  SdMan.mkdir("/.crosspoint");
  
  FsFile outputFile;
  if (!SdMan.openFileForWrite("BT", "/.crosspoint/bluetooth.bin", outputFile)) {
    Serial.printf("BT Failed to open bluetooth.bin for writing");
    return;
  }
  
  // Write version
  uint8_t version = 1;
  serialization::writePod(outputFile, version);
  
  // Write device info
  serialization::writeString(outputFile, address);
  serialization::writeString(outputFile, name);
  
  outputFile.close();
  Serial.printf("BT Last connected device saved successfully");
}

bool BluetoothHIDManager::loadLastConnectedDevice(std::string& address, std::string& name) {
  FsFile inputFile;
  if (!SdMan.openFileForRead("BT", "/.crosspoint/bluetooth.bin", inputFile)) {
    Serial.printf("BT No saved bluetooth device found");
    return false;
  }
  
  // Read version
  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != 1) {
    Serial.printf("BT Unknown bluetooth.bin version: %d", version);
    inputFile.close();
    return false;
  }
  
  // Read device info
  serialization::readString(inputFile, address);
  serialization::readString(inputFile, name);
  
  inputFile.close();
  if (address.empty()) {
    Serial.printf("BT Loaded bluetooth.bin but address field is empty");
    return false;
  }
  Serial.printf("BT Loaded last connected device: %s (%s)", name.c_str(), address.c_str());
  return true;
}

