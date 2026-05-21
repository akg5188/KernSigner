#include "error.h"

const char *kern_error_str(kern_error_t err) {
  switch (err) {
  case KERN_OK:
    return "成功";
  case KERN_ERR_INVALID_INPUT:
    return "输入无效";
  case KERN_ERR_OUT_OF_MEMORY:
    return "内存不足";
  case KERN_ERR_CRYPTO_FAILURE:
    return "加密错误";
  case KERN_ERR_QR_PARSE_FAILED:
    return "二维码解析失败";
  case KERN_ERR_MNEMONIC_INVALID:
    return "助记词无效";
  case KERN_ERR_PSBT_INVALID:
    return "交易数据无效";
  case KERN_ERR_NOT_INITIALIZED:
    return "尚未初始化";
  case KERN_ERR_TIMEOUT:
    return "超时";
  case KERN_ERR_CANCELLED:
    return "已取消";
  case KERN_ERR_IO:
    return "读写错误";
  case KERN_ERR_NOT_FOUND:
    return "未找到";
  case KERN_ERR_ALREADY_EXISTS:
    return "已经存在";
  case KERN_ERR_BUFFER_TOO_SMALL:
    return "缓冲区太小";
  case KERN_ERR_UNSUPPORTED:
    return "不支持";
  default:
    return "未知错误";
  }
}
