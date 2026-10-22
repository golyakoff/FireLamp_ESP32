#ifndef EVLAMP_CODE_HASH_H_
#define EVLAMP_CODE_HASH_H_

#include <stdint.h>

/**
 * Hash of an effect or parameter code, used instead of the code in the settings storage
 * (effect parameters, demo exclusions). Unique among the effect codes and among the parameter
 * codes of one effect: test/host/test_codes.c checks it.
 */
uint16_t code_hash(const char *code);

#endif /* EVLAMP_CODE_HASH_H_ */
