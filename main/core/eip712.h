#ifndef EIP712_H
#define EIP712_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool eip712_hash_typed_data_json(const char *typed_data_json, uint8_t out[32],
                                 char *err, size_t err_len);

#endif // EIP712_H
