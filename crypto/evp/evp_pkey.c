/*
 * Copyright 1999-2021 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include "internal/cryptlib.h"
#include <openssl/x509.h>
#include <openssl/rand.h>
#include <openssl/encoder.h>
#include <openssl/decoder.h>
#include "internal/provider.h"
#include "crypto/asn1.h"
#include "crypto/evp.h"
#include "crypto/x509.h"

/* Extract a private key from a PKCS8 structure */

EVP_PKEY *evp_pkcs82pkey_legacy(const PKCS8_PRIV_KEY_INFO *p8, OSSL_LIB_CTX *libctx,
                                const char *propq)
{
    EVP_PKEY *pkey = NULL;
    const ASN1_OBJECT *algoid;
    char obj_tmp[80];

    if (!PKCS8_pkey_get0(&algoid, NULL, NULL, NULL, p8))
        return NULL;

    if ((pkey = EVP_PKEY_new()) == NULL) {
        ERR_raise(ERR_LIB_EVP, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    if (!EVP_PKEY_set_type(pkey, OBJ_obj2nid(algoid))) {
        i2t_ASN1_OBJECT(obj_tmp, 80, algoid);
        ERR_raise_data(ERR_LIB_EVP, EVP_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM,
                       "TYPE=%s", obj_tmp);
        goto error;
    }

    if (pkey->ameth->priv_decode_ex != NULL) {
        if (!pkey->ameth->priv_decode_ex(pkey, p8, libctx, propq))
            goto error;
    } else if (pkey->ameth->priv_decode != NULL) {
        if (!pkey->ameth->priv_decode(pkey, p8)) {
            ERR_raise(ERR_LIB_EVP, EVP_R_PRIVATE_KEY_DECODE_ERROR);
            goto error;
        }
    } else {
        ERR_raise(ERR_LIB_EVP, EVP_R_METHOD_NOT_SUPPORTED);
        goto error;
    }

    return pkey;

 error:
    EVP_PKEY_free(pkey);
    return NULL;
}

EVP_PKEY *EVP_PKCS82PKEY_ex(const PKCS8_PRIV_KEY_INFO *p8, OSSL_LIB_CTX *libctx,
                            const char *propq)
{
    EVP_PKEY *pkey = NULL;
    const unsigned char *p8_data = NULL;
    unsigned char *encoded_data = NULL;
    int encoded_len;
    size_t len;
    OSSL_DECODER_CTX *dctx = NULL;

    if ((encoded_len = i2d_PKCS8_PRIV_KEY_INFO(p8, &encoded_data)) <= 0)
        goto end;

    p8_data = encoded_data;
    len = encoded_len;
    dctx = OSSL_DECODER_CTX_new_for_pkey(&pkey, "DER", "pkcs8", EVP_PKEY_NONE,
                                         0, libctx, propq);
    if (dctx == NULL
        || !OSSL_DECODER_from_data(dctx, &p8_data, &len))
        /* try legacy */
        pkey = evp_pkcs82pkey_legacy(p8, libctx, propq);

 end:
    OPENSSL_clear_free(encoded_data, encoded_len);
    OSSL_DECODER_CTX_free(dctx);
    return pkey;
}

EVP_PKEY *EVP_PKCS82PKEY(const PKCS8_PRIV_KEY_INFO *p8)
{
    return EVP_PKCS82PKEY_ex(p8, NULL, NULL);
}

/* Turn a private key into a PKCS8 structure */

PKCS8_PRIV_KEY_INFO *EVP_PKEY2PKCS8(const EVP_PKEY *pkey)
{
    PKCS8_PRIV_KEY_INFO *p8 = NULL;
    OSSL_ENCODER_CTX *ctx = NULL;

    /*
     * The implementation for provider-native keys is to encode the
     * key to a DER encoded PKCS#8 structure, then convert it to a
     * PKCS8_PRIV_KEY_INFO with good old d2i functions.
     */
    if (evp_pkey_is_provided(pkey)) {
        int selection = OSSL_KEYMGMT_SELECT_ALL;
        unsigned char *der = NULL;
        size_t derlen = 0;
        const unsigned char *pp;

        if ((ctx = OSSL_ENCODER_CTX_new_for_pkey(pkey, selection,
                                                 "DER", "pkcs8",
                                                 NULL)) == NULL
            || !OSSL_ENCODER_to_data(ctx, &der, &derlen))
            goto error;

        pp = der;
        p8 = d2i_PKCS8_PRIV_KEY_INFO(NULL, &pp, (long)derlen);
        OPENSSL_free(der);
        if (p8 == NULL)
            goto error;
    } else {
        p8 = PKCS8_PRIV_KEY_INFO_new();
        if (p8  == NULL) {
            ERR_raise(ERR_LIB_EVP, ERR_R_MALLOC_FAILURE);
            return NULL;
        }

        if (pkey->ameth != NULL) {
            if (pkey->ameth->priv_encode != NULL) {
                if (!pkey->ameth->priv_encode(p8, pkey)) {
                    ERR_raise(ERR_LIB_EVP, EVP_R_PRIVATE_KEY_ENCODE_ERROR);
                    goto error;
                }
            } else {
                ERR_raise(ERR_LIB_EVP, EVP_R_METHOD_NOT_SUPPORTED);
                goto error;
            }
        } else {
            ERR_raise(ERR_LIB_EVP, EVP_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM);
            goto error;
        }
    }
    goto end;
 error:
    PKCS8_PRIV_KEY_INFO_free(p8);
    p8 = NULL;
 end:
    OSSL_ENCODER_CTX_free(ctx);
    return p8;

}

/* EVP_PKEY attribute functions */

int EVP_PKEY_get_attr_count(const EVP_PKEY *key)
{
    return X509at_get_attr_count(key->attributes);
}

int EVP_PKEY_get_attr_by_NID(const EVP_PKEY *key, int nid, int lastpos)
{
    return X509at_get_attr_by_NID(key->attributes, nid, lastpos);
}

int EVP_PKEY_get_attr_by_OBJ(const EVP_PKEY *key, const ASN1_OBJECT *obj,
                             int lastpos)
{
    return X509at_get_attr_by_OBJ(key->attributes, obj, lastpos);
}

X509_ATTRIBUTE *EVP_PKEY_get_attr(const EVP_PKEY *key, int loc)
{
    return X509at_get_attr(key->attributes, loc);
}

X509_ATTRIBUTE *EVP_PKEY_delete_attr(EVP_PKEY *key, int loc)
{
    return X509at_delete_attr(key->attributes, loc);
}

int EVP_PKEY_add1_attr(EVP_PKEY *key, X509_ATTRIBUTE *attr)
{
    if (X509at_add1_attr(&key->attributes, attr))
        return 1;
    return 0;
}

int EVP_PKEY_add1_attr_by_OBJ(EVP_PKEY *key,
                              const ASN1_OBJECT *obj, int type,
                              const unsigned char *bytes, int len)
{
    if (X509at_add1_attr_by_OBJ(&key->attributes, obj, type, bytes, len))
        return 1;
    return 0;
}

int EVP_PKEY_add1_attr_by_NID(EVP_PKEY *key,
                              int nid, int type,
                              const unsigned char *bytes, int len)
{
    if (X509at_add1_attr_by_NID(&key->attributes, nid, type, bytes, len))
        return 1;
    return 0;
}

int EVP_PKEY_add1_attr_by_txt(EVP_PKEY *key,
                              const char *attrname, int type,
                              const unsigned char *bytes, int len)
{
    if (X509at_add1_attr_by_txt(&key->attributes, attrname, type, bytes, len))
        return 1;
    return 0;
}

const char *EVP_PKEY_get0_first_alg_name(const EVP_PKEY *key)
{
    const EVP_PKEY_ASN1_METHOD *ameth;
    const char *name = NULL;

    if (key->keymgmt != NULL)
        return EVP_KEYMGMT_get0_first_name(key->keymgmt);

    /* Otherwise fallback to legacy */
    ameth = EVP_PKEY_get0_asn1(key);
    if (ameth != NULL)
        EVP_PKEY_asn1_get0_info(NULL, NULL,
                                NULL, NULL, &name, ameth);

    return name;
}
