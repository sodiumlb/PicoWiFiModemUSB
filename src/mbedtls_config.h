/*
 * mbedTLS configuration for PicoWiFiModemUSB — TLS-terminating proxy.
 *
 * Client-side TLS 1.2 only (the modem dials out; it never serves TLS to the
 * Oric). Entropy comes from the Pico SDK's mbedtls_hardware_poll (ROSC-based
 * get_rand_64), selected by MBEDTLS_ENTROPY_HARDWARE_ALT.
 *
 * Derived from the Pico SDK test config (pico-sdk/test/kitchen_sink), which is
 * known to build and run against mbedTLS 2.28 on RP2040. Memory note: the TLS
 * output record is capped at 2 KB; the input record keeps the mbedTLS default
 * (16 KB) so large certificate chains in the handshake still fit. Trimming RAM
 * further (and dropping the unused server side) is deferred to a later sprint.
 */
#ifndef _MBEDTLS_CONFIG_PICOWIFI_H
#define _MBEDTLS_CONFIG_PICOWIFI_H

/* Workaround for some mbedtls source files using INT_MAX without including limits.h */
#include <limits.h>

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

#define MBEDTLS_SSL_OUT_CONTENT_LEN    2048
/* RAM tuning (sprint 3): cap the TLS receive record at 8 KB instead of the 16 KB
 * default — halves the largest per-session buffer. A server that emits a single
 * record larger than 8 KB (rare; most fragment the certificate flight) would
 * fail; raise this back toward 16384 if such a peer must be supported. TCP_WND
 * in lwipopts.h is kept >= this value to avoid an RX stall. */
#define MBEDTLS_SSL_IN_CONTENT_LEN     8192

#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_HAVE_TIME
// NB: we do NOT enable MBEDTLS_HAVE_TIME_DATE. Enabling mbedTLS's *internal* date
// verification during the handshake (with the lazyCaCb trust callback present) froze
// the modem loop (v0.3.0 experimental). Instead, the date is checked *after* the
// handshake, on the peer certificate, by certDateInvalid() in tcp_support.h, using
// the SNTP clock (time_support.h). Simpler, and without touching mbedTLS internals.

#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_OID_C
#define MBEDTLS_PKCS5_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
/* E-lazy (v0.2.1): on-demand trusted-CA lookup via mbedtls_ssl_conf_ca_cb().
 * Lets us verify the server chain against a multi-CA bundle stored in LittleFS
 * while keeping only ONE CA parsed in RAM at a time — the RP2040 cannot hold the
 * full Mozilla store parsed (~200–400 KB). See docs/design-proxy-tls-ssh.md §10. */
#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
/* PEM parsing of the user-provided CA (AT$CA=). Without these, mbedtls_x509_crt_parse
 * only accepts DER, so a PEM CA fails to load and certificate verification (AT$CV1)
 * never creates a TLS config (every secure dial returns NO CARRIER). BASE64_C is a
 * hard dependency of PEM_PARSE_C. (Server-sent handshake certs are DER, hence CV0
 * worked without this.) */
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_AES_FEWER_TABLES

/* TLS 1.2 */
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_GCM_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_WRITE_C

#endif /* _MBEDTLS_CONFIG_PICOWIFI_H */
