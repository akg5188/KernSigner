#include "signer_services.h"

static const signer_service_status_t service_statuses[] = {
    {SIGNER_SERVICE_WALLET, "钱包服务", SIGNER_SERVICE_READY,
     "导入、创建、公钥、地址、备份、签名。"},
    {SIGNER_SERVICE_CRYPTO, "加密服务", SIGNER_SERVICE_READY,
     "签名前检查账户、网络和路径。"},
    {SIGNER_SERVICE_QR, "二维码服务", SIGNER_SERVICE_READY,
     "支持扫码、导入和签名。"},
    {SIGNER_SERVICE_CAMERA, "相机服务", SIGNER_SERVICE_READY,
     "相机和识别可用。"},
    {SIGNER_SERVICE_STORAGE, "存储服务", SIGNER_SERVICE_READY,
     "支持存储卡检查和浏览。"},
    {SIGNER_SERVICE_DISPLAY, "显示触摸", SIGNER_SERVICE_READY,
     "中文界面、触摸可用。"},
    {SIGNER_SERVICE_SMARTCARD, "智能卡检测", SIGNER_SERVICE_CANDIDATE,
     "CCID、连接、签名、地址已接入；写卡和改 PIN 暂隐藏。"},
};

const signer_service_status_t *signer_service_status_at(size_t index) {
  if (index >= signer_service_status_count())
    return NULL;
  return &service_statuses[index];
}

size_t signer_service_status_count(void) {
  return sizeof(service_statuses) / sizeof(service_statuses[0]);
}

const char *signer_service_state_name(signer_service_state_t state) {
  switch (state) {
  case SIGNER_SERVICE_READY:
    return "可用";
  case SIGNER_SERVICE_CANDIDATE:
    return "可用";
  case SIGNER_SERVICE_STUB:
    return "维护";
  case SIGNER_SERVICE_BLOCKED:
    return "暂停";
  default:
    return "未知";
  }
}

const char *signer_service_guard_for_feature(const signer_feature_t *feature) {
  if (!feature)
    return "钱包入口：没有找到功能定义。";

  switch (feature->risk) {
  case SIGNER_FEATURE_RISK_VIEW_ONLY:
    return "钱包入口：当前显示公开信息或菜单。";
  case SIGNER_FEATURE_RISK_SECRET_MATERIAL:
    return "钱包入口：敏感内容只在内存中处理，退出或关机即清空。";
  case SIGNER_FEATURE_RISK_SIGNING:
    return "钱包入口：签名流程保留交易审查和明确确认。";
  case SIGNER_FEATURE_RISK_EXTERNAL_IO:
    return "钱包入口：外设输入按对应页面处理，敏感内容不自动导入。";
  case SIGNER_FEATURE_RISK_DEVICE_CONTROL:
    return "钱包入口：设备控制按已实现页面执行，危险操作保留确认。";
  default:
    return "钱包入口：未知风险按高风险处理。";
  }
}

const char *signer_service_next_step_for_feature(const signer_feature_t *feature) {
  if (!feature)
    return "使用提示：返回首页选功能。";

  switch (feature->status) {
  case SIGNER_FEATURE_NOT_STARTED:
    return "使用提示：从首页进入。";
  case SIGNER_FEATURE_UI_READY:
    return "使用提示：按按钮继续。";
  case SIGNER_FEATURE_SERVICE_STUB:
    return "使用提示：该功能暂保护。";
  case SIGNER_FEATURE_HARDWARE_WIRED:
    return "使用提示：可直接检测。";
  case SIGNER_FEATURE_VERIFIED:
    return "使用提示：可继续使用。";
  default:
    return "使用提示：未知状态按保护处理。";
  }
}
