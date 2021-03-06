/*
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root.
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <base64.h>
#include <derenc.h>
#include <x509bldr.h>

#define ASRT(_X)    if(!(_X))      {goto Error;}
#define CHK(_X)     if(((_X)) < 0) {goto Error;}

// OIDs.  Note that the encoder expects a -1 sentinel.
// TODO (DJM): Add const qualifiers here.
static int riotOID[] = { 2,23,133,5,4,1,-1 };
static int ecdsaWithSHA256OID[] = { 1,2,840,10045,4,3,2,-1 };
static int ecPublicKeyOID[] = { 1,2,840,10045, 2,1,-1 };
static int keyUsageOID[] = { 2,5,29,15,-1 };
static int extKeyUsageOID[] = { 2,5,29,37,-1 };
static int extAuthKeyIdentifierOID[] = { 2,5,29,35,-1 };
static int clientAuthOID[] = { 1,3,6,1,5,5,7,3,2,-1 };
static int sha256OID[] = { 2,16,840,1,101,3,4,2,1,-1 };
static int commonNameOID[] = { 2,5,4,3,-1 };
static int countryNameOID[] = { 2,5,4,6,-1 };
static int orgNameOID[] = { 2,5,4,10,-1 };
static int basicConstraintsOID[] = { 2,5,29,19,-1 };
#if defined(RIOTSECP256R1)
static int prime256v1OID[] = { 1,2,840,10045, 3,1,7,-1 };
static int *curveOID = prime256v1OID;
#elif defined(RIOTSECP384R1)
static int ansip384r1OID[] = { 1,3,132,0,34,-1 };
static int *curveOID = ansip384r1OID;
#elif defined(RIOTSECP521R1)
static int ansip521r1OID[] = { 1,3,132,0,35,-1 };
static int *curveOID = ansip521r1OID;
#else
#error "Must define one of RIOTSECP256R1, RIOTSECP384R1, RIOTSECP521R1"
#endif

static int
GenerateGuidFromSeed(
    char*            nameBuf,
    uint32_t        *nameBufLen,
    const uint8_t   *seed,
    size_t           seedLen
)
{
    uint8_t digest[RIOT_DIGEST_LENGTH];
    int     result;

    RiotCrypt_Hash(digest, sizeof(digest), seed, seedLen);
    result = Base64Encode(digest, 16, nameBuf, nameBufLen);
    return result;
}

static int
MpiToInt(
    const mbedtls_mpi  *X,
    uint8_t            *buf,
    size_t              bufLen
)
{
    size_t len;

    if (!X || !buf)
    {
        return -1;
    }

    // Get actual required size
    len = mbedtls_mpi_size(X);

    // Sanity check input
    if ((bufLen < len) || (RIOT_COORDMAX < len))
    {
        return -1;
    }

    // Write buffer (always go with COORDMAX here)
    return(mbedtls_mpi_write_binary(X, buf, RIOT_COORDMAX));
}

static int
X509AddExtensions(
    DERBuilderContext   *Tbs,
    uint8_t             *DevIdPub,
    uint32_t             DevIdPubLen,
    uint8_t             *Fwid,
    uint32_t             FwidLen
)
// Create the RIoT extensions.  
{
    uint8_t     authKeyIdentifier[SHA1_DIGEST_LENGTH];
    mbedtls_sha1_ret(DevIdPub, DevIdPubLen, authKeyIdentifier);
    uint8_t     keyUsage = RIOT_X509_KEY_USAGE;
    uint8_t        extLen = 1;

    CHK(DERStartExplicit(Tbs, 3));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    
    // keyUsage
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERAddOID(Tbs, keyUsageOID));
    CHK(            DERStartEnvelopingOctetString(Tbs));
    CHK(                DERAddBitString(Tbs, &keyUsage, extLen)); 
    CHK(            DERPopNesting(Tbs));
    CHK(        DERPopNesting(Tbs));

    // extendedKeyUsage
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERAddOID(Tbs, extKeyUsageOID));
    CHK(            DERStartEnvelopingOctetString(Tbs));
    CHK(                DERStartSequenceOrSet(Tbs, true));
    CHK(                    DERAddOID(Tbs, clientAuthOID));
    CHK(                DERPopNesting(Tbs));
    CHK(            DERPopNesting(Tbs));
    CHK(        DERPopNesting(Tbs));

    // authKeyIdentifier
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERAddOID(Tbs, extAuthKeyIdentifierOID));
    CHK(            DERStartEnvelopingOctetString(Tbs));
    CHK(                DERStartSequenceOrSet(Tbs, true));
    CHK(                    DERStartExplicit(Tbs, 0));
    CHK(                        DERAddOctetString(Tbs, authKeyIdentifier, 20));
    CHK(                    DERPopNesting(Tbs));
    CHK(                DERPopNesting(Tbs));
    CHK(            DERPopNesting(Tbs));
    CHK(        DERPopNesting(Tbs));
    
    // RIoT extension
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERAddOID(Tbs, riotOID));
    CHK(            DERStartEnvelopingOctetString(Tbs));
    CHK(                DERStartSequenceOrSet(Tbs, true));
    CHK(                    DERAddInteger(Tbs, 1));
    CHK(                    DERStartSequenceOrSet(Tbs, true));
    CHK(                        DERStartSequenceOrSet(Tbs, true));
    CHK(                            DERAddOID(Tbs, ecPublicKeyOID));
    CHK(                            DERAddOID(Tbs, curveOID));
    CHK(                        DERPopNesting(Tbs));
    CHK(                        DERAddBitString(Tbs, DevIdPub, DevIdPubLen));
    CHK(                    DERPopNesting(Tbs));
    CHK(                    DERStartSequenceOrSet(Tbs, true));
    CHK(                        DERAddOID(Tbs, sha256OID));
    CHK(                        DERAddOctetString(Tbs, Fwid, FwidLen));
    CHK(                    DERPopNesting(Tbs));
    CHK(                DERPopNesting(Tbs));
    CHK(            DERPopNesting(Tbs));
    CHK(        DERPopNesting(Tbs));

    CHK(    DERPopNesting(Tbs));
    CHK(DERPopNesting(Tbs));

    return 0;

Error:
    return -1;
}

static int
X509AddX501Name(
    DERBuilderContext   *Context,
    const char          *CommonName,
    const char          *OrgName,
    const char          *CountryName
)
{
    CHK(    DERStartSequenceOrSet(Context, true));
    CHK(        DERStartSequenceOrSet(Context, false));
    CHK(            DERStartSequenceOrSet(Context, true));
    CHK(                DERAddOID(Context, commonNameOID));
    CHK(                DERAddUTF8String(Context, CommonName));
    CHK(            DERPopNesting(Context));
    CHK(        DERPopNesting(Context));
    CHK(        DERStartSequenceOrSet(Context, false));
    CHK(            DERStartSequenceOrSet(Context, true));
    CHK(                DERAddOID(Context, countryNameOID));
    CHK(                DERAddUTF8String(Context, CountryName));
    CHK(            DERPopNesting(Context));
    CHK(        DERPopNesting(Context));
    CHK(        DERStartSequenceOrSet(Context, false));
    CHK(            DERStartSequenceOrSet(Context, true));
    CHK(                DERAddOID(Context, orgNameOID));
    CHK(                DERAddUTF8String(Context, OrgName));
    CHK(            DERPopNesting(Context));
    CHK(        DERPopNesting(Context));
    CHK(    DERPopNesting(Context));

    return 0;

Error:
    return -1;
}

int
X509GetDeviceCertTBS(
    DERBuilderContext   *Tbs,
    RIOT_X509_TBS_DATA  *TbsData,
    RIOT_ECC_PUBLIC     *DevIdKeyPub,
    uint8_t             *RootKeyPub,
    uint32_t             RootKeyPubLen)
{
    uint8_t     encBuffer[RIOT_COORDMAX * 2 + 1];
    uint32_t    encBufferLen = sizeof(encBuffer);
    uint8_t     keyUsage = RIOT_X509_KEY_USAGE;
    uint8_t     authKeyIdentifier[SHA1_DIGEST_LENGTH];

    CHK(RiotCrypt_ExportEccPub(DevIdKeyPub, encBuffer, &encBufferLen));

    if (RootKeyPub != NULL)
    {
        mbedtls_sha1_ret(RootKeyPub, RootKeyPubLen, authKeyIdentifier);
    }

    CHK(DERStartSequenceOrSet(Tbs, true));
    CHK(    DERAddShortExplicitInteger(Tbs, 2));
    CHK(    DERAddIntegerFromArray(Tbs, TbsData->SerialNum, RIOT_X509_SNUM_LEN));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERAddOID(Tbs, ecdsaWithSHA256OID));
    CHK(    DERPopNesting(Tbs));
    CHK(    X509AddX501Name(Tbs, TbsData->IssuerCommon, TbsData->IssuerOrg, TbsData->IssuerCountry));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERAddUTCTime(Tbs, TbsData->ValidFrom));
    CHK(        DERAddUTCTime(Tbs, TbsData->ValidTo));
    CHK(    DERPopNesting(Tbs));
    CHK(    X509AddX501Name(Tbs, TbsData->SubjectCommon, TbsData->SubjectOrg, TbsData->SubjectCountry));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERAddOID(Tbs, ecPublicKeyOID));
    CHK(            DERAddOID(Tbs, curveOID));
    CHK(        DERPopNesting(Tbs));
    CHK(        DERAddBitString(Tbs, encBuffer, encBufferLen));
    CHK(    DERPopNesting(Tbs));
    CHK(    DERStartExplicit(Tbs, 3));
    CHK(        DERStartSequenceOrSet(Tbs, true));

    CHK(            DERStartSequenceOrSet(Tbs, true));
    CHK(                DERAddOID(Tbs, keyUsageOID));
    CHK(                DERStartEnvelopingOctetString(Tbs));
                            encBufferLen = 1;
    CHK(                    DERAddBitString(Tbs, &keyUsage, encBufferLen)); // Actually 6bits
    CHK(                DERPopNesting(Tbs));
    CHK(            DERPopNesting(Tbs));
    
    CHK(            DERStartSequenceOrSet(Tbs, true));
    CHK(                DERAddOID(Tbs, basicConstraintsOID));
    CHK(                DERAddBoolean(Tbs, true));
    CHK(                DERStartEnvelopingOctetString(Tbs));
    CHK(                    DERStartSequenceOrSet(Tbs, true));
    CHK(                        DERAddBoolean(Tbs, true));
    CHK(                        DERAddInteger(Tbs, 1));
    CHK(                    DERPopNesting(Tbs));
    CHK(                DERPopNesting(Tbs));
    CHK(            DERPopNesting(Tbs));

    if(RootKeyPub!=NULL)
    {
        CHK(        DERStartSequenceOrSet(Tbs, true));
        CHK(            DERAddOID(Tbs, extAuthKeyIdentifierOID));
        CHK(            DERStartEnvelopingOctetString(Tbs));
        CHK(                DERStartSequenceOrSet(Tbs, true));
        CHK(                    DERStartExplicit(Tbs, 0));
        CHK(                        DERAddOctetString(Tbs, authKeyIdentifier, 20));
        CHK(                    DERPopNesting(Tbs));
        CHK(                DERPopNesting(Tbs));
        CHK(            DERPopNesting(Tbs));
        CHK(        DERPopNesting(Tbs));
    }

    CHK(        DERPopNesting(Tbs));
    CHK(    DERPopNesting(Tbs));
    CHK(DERPopNesting(Tbs));

    ASRT(DERGetNestingDepth(Tbs) == 0);
    return 0;

Error:
    return -1;
}

int
X509MakeDeviceCert(
    DERBuilderContext   *DeviceIDCert,
    RIOT_ECC_SIGNATURE  *TbsSig
)
// Create a Device Certificate given a ready-to-sign TBS region in the context
{
    uint8_t     encBuffer[RIOT_COORDMAX];
    uint32_t    encBufferLen = sizeof(encBuffer);

    // Elevate the "TBS" block into a real certificate,
    // i.e., copy it into an enclosing sequence.
    CHK(DERTbsToCert(DeviceIDCert));
    CHK(    DERStartSequenceOrSet(DeviceIDCert, true));
    CHK(        DERAddOID(DeviceIDCert, ecdsaWithSHA256OID));
    CHK(    DERPopNesting(DeviceIDCert));
    CHK(    DERStartEnvelopingBitString(DeviceIDCert));
    CHK(        DERStartSequenceOrSet(DeviceIDCert, true));
    CHK(            MpiToInt(&TbsSig->r, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(DeviceIDCert, encBuffer, RIOT_COORDMAX));
    CHK(            MpiToInt(&TbsSig->s, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(DeviceIDCert, encBuffer, RIOT_COORDMAX));
    CHK(        DERPopNesting(DeviceIDCert));
    CHK(    DERPopNesting(DeviceIDCert));
    CHK(DERPopNesting(DeviceIDCert));

    ASRT(DERGetNestingDepth(DeviceIDCert) == 0);
    return 0;

Error:
    return -1;
}

int
X509GetAliasCertTBS(
    DERBuilderContext   *Tbs,
    RIOT_X509_TBS_DATA  *TbsData,
    RIOT_ECC_PUBLIC     *AliasKeyPub,
    RIOT_ECC_PUBLIC     *DevIdKeyPub,
    uint8_t             *Fwid,
    uint32_t             FwidLen
)
{
    int result;
    char guidBuffer[64];
    uint8_t     encBuffer[(RIOT_COORDMAX * 2) + 1];
    uint32_t    encBufferLen = sizeof(encBuffer);

    // "*" denotes a subject common name that we'll need to fill in here.
    if (strncmp(TbsData->SubjectCommon, "*", 1) == 0)
    {
        uint32_t bufLen = sizeof(guidBuffer);

        CHK(RiotCrypt_ExportEccPub(DevIdKeyPub, encBuffer, &encBufferLen));

        // Replace the common-name with a per-device GUID derived from the DeviceID public key
        result = GenerateGuidFromSeed(guidBuffer, &bufLen, encBuffer, encBufferLen);

        if (result < 0) {
            return result;
        }

        guidBuffer[bufLen-1] = 0;
        TbsData->SubjectCommon = guidBuffer;
    }

    CHK(DERStartSequenceOrSet(Tbs, true));
    CHK(    DERAddShortExplicitInteger(Tbs, 2));
    CHK(    DERAddIntegerFromArray(Tbs, TbsData->SerialNum, RIOT_X509_SNUM_LEN));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERAddOID(Tbs, ecdsaWithSHA256OID));
    CHK(    DERPopNesting(Tbs));
    CHK(    X509AddX501Name(Tbs, TbsData->IssuerCommon, TbsData->IssuerOrg, TbsData->IssuerCountry));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERAddUTCTime(Tbs, TbsData->ValidFrom));
    CHK(        DERAddUTCTime(Tbs, TbsData->ValidTo));
    CHK(    DERPopNesting(Tbs));
    CHK(    X509AddX501Name(Tbs, TbsData->SubjectCommon, TbsData->SubjectOrg, TbsData->SubjectCountry));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERAddOID(Tbs, ecPublicKeyOID));
    CHK(            DERAddOID(Tbs, curveOID));
    CHK(        DERPopNesting(Tbs));
                encBufferLen = sizeof(encBuffer);
    CHK(        RiotCrypt_ExportEccPub(AliasKeyPub, encBuffer, &encBufferLen));
    CHK(        DERAddBitString(Tbs, encBuffer, encBufferLen));
    CHK(    DERPopNesting(Tbs));
            encBufferLen = sizeof(encBuffer);
    CHK(    RiotCrypt_ExportEccPub(DevIdKeyPub, encBuffer, &encBufferLen));
    CHK(    X509AddExtensions(Tbs, encBuffer, encBufferLen, Fwid, FwidLen));
    CHK(DERPopNesting(Tbs));
    
    ASRT(DERGetNestingDepth(Tbs) == 0);
    return 0;

Error:
    return -1;
}

int 
X509MakeAliasCert(
    DERBuilderContext   *AliasCert,
    RIOT_ECC_SIGNATURE  *TbsSig
)
// Create an Alias Certificate given a ready-to-sign TBS region in the context
{
    uint8_t     encBuffer[RIOT_COORDMAX];
    uint32_t    encBufferLen = sizeof(encBuffer);

    // Elevate the "TBS" block into a real certificate,
    // i.e., copy it into an enclosing sequence.
    CHK(DERTbsToCert(AliasCert));   
    CHK(    DERStartSequenceOrSet(AliasCert, true));
    CHK(        DERAddOID(AliasCert, ecdsaWithSHA256OID));
    CHK(    DERPopNesting(AliasCert));
    CHK(    DERStartEnvelopingBitString(AliasCert));
    CHK(        DERStartSequenceOrSet(AliasCert, true));
    CHK(            MpiToInt(&TbsSig->r, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(AliasCert, encBuffer, RIOT_COORDMAX));
    CHK(            MpiToInt(&TbsSig->s, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(AliasCert, encBuffer, RIOT_COORDMAX));
    CHK(        DERPopNesting(AliasCert));
    CHK(    DERPopNesting(AliasCert));
    CHK(DERPopNesting(AliasCert));

    ASRT(DERGetNestingDepth(AliasCert) == 0);
    return 0;

Error:
    return -1;
}

int
X509GetDEREccPub(
    DERBuilderContext   *Context,
    RIOT_ECC_PUBLIC      Pub
)
{
    uint8_t     encBuffer[RIOT_COORDMAX * 2 + 1];  // Buffer sized to fit MPI x2 + 1 (0x04)
    uint32_t    encBufferLen = sizeof(encBuffer);

    CHK(DERStartSequenceOrSet(Context, true));
    CHK(    DERStartSequenceOrSet(Context, true));
    CHK(        DERAddOID(Context, ecPublicKeyOID));
    CHK(        DERAddOID(Context, curveOID));
    CHK(    DERPopNesting(Context));
    CHK(    RiotCrypt_ExportEccPub(&Pub, encBuffer, &encBufferLen));
    CHK(    DERAddBitString(Context, encBuffer, encBufferLen));
    CHK(DERPopNesting(Context));

    ASRT(DERGetNestingDepth(Context) == 0);
    return 0;

Error:
    return -1;
}

int
X509GetDEREcc(
    DERBuilderContext   *Context,
    RIOT_ECC_PUBLIC      Pub,
    RIOT_ECC_PRIVATE     Priv
)
{
    uint8_t     encBuffer[RIOT_COORDMAX * 2 + 1];  // Buffer sized to fit MPI x2 + 1 (0x04)
    uint32_t    encBufferLen = sizeof(encBuffer);

    CHK(DERStartSequenceOrSet(Context, true));
    CHK(    DERAddInteger(Context, 1));
    CHK(    MpiToInt(&Priv, encBuffer, encBufferLen));
    CHK(    DERAddOctetString(Context, encBuffer, RIOT_COORDMAX));
    CHK(    DERStartExplicit(Context, 0));
    CHK(        DERAddOID(Context, curveOID));
    CHK(    DERPopNesting(Context));
    CHK(    DERStartExplicit(Context, 1));
    CHK(        RiotCrypt_ExportEccPub(&Pub, encBuffer, &encBufferLen));
    CHK(        DERAddBitString(Context, encBuffer, encBufferLen));
    CHK(    DERPopNesting(Context));
    CHK(DERPopNesting(Context));

    ASRT(DERGetNestingDepth(Context) == 0);
    return 0;

Error:
    return -1;
}

int
X509GetDERCsrTbs(
    DERBuilderContext   *Context,
    RIOT_X509_TBS_DATA  *TbsData,
    RIOT_ECC_PUBLIC*      DeviceIDPub
)
{
    uint8_t     encBuffer[RIOT_MAX_EBLEN * 2];
    uint32_t    encBufferLen;

    CHK(DERStartSequenceOrSet(Context, true));
    CHK(    DERAddInteger(Context, 0));
    CHK(    X509AddX501Name(Context, TbsData->IssuerCommon, TbsData->IssuerOrg, TbsData->IssuerCountry));
    CHK(    DERStartSequenceOrSet(Context, true));
    CHK(        DERStartSequenceOrSet(Context, true));
    CHK(            DERAddOID(Context, ecPublicKeyOID));
    CHK(            DERAddOID(Context, curveOID));
    CHK(        DERPopNesting(Context));
                encBufferLen = sizeof(encBuffer);
    CHK(        RiotCrypt_ExportEccPub(DeviceIDPub, encBuffer, &encBufferLen));
    CHK(        DERAddBitString(Context, encBuffer, encBufferLen));
    CHK(    DERPopNesting(Context));
    CHK(DERStartExplicit(Context,0));
    CHK(DERPopNesting(Context));
    CHK(DERPopNesting(Context));

    ASRT(DERGetNestingDepth(Context) == 0);
    return 0;

Error:
    return -1;
}

int
X509GetDERCsr(
    DERBuilderContext   *Context,
    RIOT_ECC_SIGNATURE  *Signature
)
{
    uint8_t     encBuffer[RIOT_COORDMAX];
    uint32_t    encBufferLen = sizeof(encBuffer);

    // Elevate the "TBS" block into a real certificate, i.e., copy it
    // into an enclosing sequence and then add the signature block.
    CHK(DERTbsToCert(Context));
    CHK(    DERStartSequenceOrSet(Context, true));
    CHK(        DERAddOID(Context, ecdsaWithSHA256OID));
    CHK(    DERPopNesting(Context));
    CHK(    DERStartEnvelopingBitString(Context));
    CHK(        DERStartSequenceOrSet(Context, true));
    CHK(            MpiToInt(&Signature->r, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(Context, encBuffer, RIOT_COORDMAX));
    CHK(            MpiToInt(&Signature->s, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(Context, encBuffer, RIOT_COORDMAX));
    CHK(        DERPopNesting(Context));
    CHK(    DERPopNesting(Context));
    CHK(DERPopNesting(Context));

    ASRT(DERGetNestingDepth(Context) == 0);
    return 0;

Error:
    return -1;
}

int
X509GetRootCertTBS(
    DERBuilderContext   *Tbs,
    RIOT_X509_TBS_DATA  *TbsData,
    RIOT_ECC_PUBLIC     *RootKeyPub
)
{
    uint8_t     encBuffer[RIOT_COORDMAX * 2 + 1];
    uint32_t    encBufferLen = sizeof(encBuffer);
    uint8_t     keyUsage = RIOT_X509_KEY_USAGE;

    CHK(DERStartSequenceOrSet(Tbs, true));
    CHK(    DERAddShortExplicitInteger(Tbs, 2));
    CHK(    DERAddIntegerFromArray(Tbs, TbsData->SerialNum, RIOT_X509_SNUM_LEN));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERAddOID(Tbs, ecdsaWithSHA256OID));
    CHK(    DERPopNesting(Tbs));
    CHK(    X509AddX501Name(Tbs, TbsData->IssuerCommon, TbsData->IssuerOrg, TbsData->IssuerCountry));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERAddUTCTime(Tbs, TbsData->ValidFrom));
    CHK(        DERAddUTCTime(Tbs, TbsData->ValidTo));
    CHK(    DERPopNesting(Tbs));
    CHK(    X509AddX501Name(Tbs, TbsData->SubjectCommon, TbsData->SubjectOrg, TbsData->SubjectCountry));
    CHK(    DERStartSequenceOrSet(Tbs, true));
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERAddOID(Tbs, ecPublicKeyOID));
    CHK(            DERAddOID(Tbs, curveOID));
    CHK(        DERPopNesting(Tbs));
    CHK(        RiotCrypt_ExportEccPub(RootKeyPub, encBuffer, &encBufferLen));
    CHK(        DERAddBitString(Tbs, encBuffer, encBufferLen));
    CHK(    DERPopNesting(Tbs));
    CHK(    DERStartExplicit(Tbs, 3));
    CHK(        DERStartSequenceOrSet(Tbs, true));
    CHK(            DERStartSequenceOrSet(Tbs, true));
    CHK(                DERAddOID(Tbs, keyUsageOID));
    CHK(                DERStartEnvelopingOctetString(Tbs));
                            encBufferLen = 1;
    CHK(                    DERAddBitString(Tbs, &keyUsage, encBufferLen)); // Actually 6bits
    CHK(                DERPopNesting(Tbs));
    CHK(            DERPopNesting(Tbs));
    CHK(            DERStartSequenceOrSet(Tbs, true));
    CHK(                DERAddOID(Tbs, basicConstraintsOID));
    CHK(                DERAddBoolean(Tbs, true));
    CHK(                DERStartEnvelopingOctetString(Tbs));
    CHK(                    DERStartSequenceOrSet(Tbs, true));
    CHK(                        DERAddBoolean(Tbs, true));
    CHK(                        DERAddInteger(Tbs, 2));
    CHK(                    DERPopNesting(Tbs));
    CHK(                DERPopNesting(Tbs));
    CHK(            DERPopNesting(Tbs));
    CHK(        DERPopNesting(Tbs));
    CHK(    DERPopNesting(Tbs));
    CHK(DERPopNesting(Tbs));

    ASRT(DERGetNestingDepth(Tbs) == 0);
    return 0;

Error:
    return -1;
}

int
X509MakeRootCert(
    DERBuilderContext   *RootCert,
    RIOT_ECC_SIGNATURE  *TbsSig
)
// Create an Alias Certificate given a ready-to-sign TBS region in the context
{
    uint8_t     encBuffer[RIOT_COORDMAX];
    uint32_t    encBufferLen = sizeof(encBuffer);

    // Elevate the "TBS" block into a real certificate,
    // i.e., copy it into an enclosing sequence.
    CHK(DERTbsToCert(RootCert));
    CHK(    DERStartSequenceOrSet(RootCert, true));
    CHK(        DERAddOID(RootCert, ecdsaWithSHA256OID));
    CHK(    DERPopNesting(RootCert));
    CHK(    DERStartEnvelopingBitString(RootCert));
    CHK(        DERStartSequenceOrSet(RootCert, true));
    CHK(            MpiToInt(&TbsSig->r, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(RootCert, encBuffer, RIOT_COORDMAX));
    CHK(            MpiToInt(&TbsSig->s, encBuffer, encBufferLen));
    CHK(            DERAddIntegerFromArray(RootCert, encBuffer, RIOT_COORDMAX));
    CHK(        DERPopNesting(RootCert));
    CHK(    DERPopNesting(RootCert));
    CHK(DERPopNesting(RootCert));

    ASRT(DERGetNestingDepth(RootCert) == 0);
    return 0;

Error:
    return -1;
}

