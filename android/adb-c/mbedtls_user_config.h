#ifndef MS912X_MBEDTLS_USER_CONFIG_H
#define MS912X_MBEDTLS_USER_CONFIG_H
/* Pairing and a retained helper can use PSA concurrently. */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
#endif
