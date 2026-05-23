#include "error.h"

const char *ksig_error_str(ksig_error_t err) {
  switch (err) {
  case KSIG_OK:
    return "成功";
  case KSIG_ERR_INVALID_INPUT:
    return "输入无效";
  case KSIG_ERR_OUT_OF_MEMORY:
    return "内存不足";
  case KSIG_ERR_CRYPTO_FAILURE:
    return "加密错误";
  case KSIG_ERR_QR_PARSE_FAILED:
    return "二维码解析失败";
  case KSIG_ERR_MNEMONIC_INVALID:
    return "助记词无效";
  case KSIG_ERR_PSBT_INVALID:
    return "交易数据无效";
  case KSIG_ERR_NOT_INITIALIZED:
    return "尚未初始化";
  case KSIG_ERR_TIMEOUT:
    return "超时";
  case KSIG_ERR_CANCELLED:
    return "已取消";
  case KSIG_ERR_IO:
    return "读写错误";
  case KSIG_ERR_NOT_FOUND:
    return "未找到";
  case KSIG_ERR_ALREADY_EXISTS:
    return "已经存在";
  case KSIG_ERR_BUFFER_TOO_SMALL:
    return "缓冲区太小";
  case KSIG_ERR_UNSUPPORTED:
    return "不支持";
  default:
    return "未知错误";
  }
}
