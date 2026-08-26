#include <windows.h>
#include <wincrypt.h>
#include <iostream>
#include <vector>
#include <string>
#pragma comment(lib, "crypt32.lib")

int main() {
    const char* pem =
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAw43FiFHMzl4TP9jRJ1j2\n"
        "psH1ZaF0/RNiV22rc2iJ1HNGFySuLpuVTSoaq2PDAnp2+JEsobkd/qva38gFOrcZ\n"
        "sZeMn3czY+o2vVw9IqjN8WGKSAhL6c7jLcwmj5myZ56pRuoIfTJA3ScKqF09CnbS\n"
        "woR9SKLGHDgykU+usqRudAeK8uyxmpyzbhye0y6ZKyiidZpsCKbW1pIEmdbffvMq\n"
        "uUn6iAXQoMZo/5fmnF0C5KAorSZdtAwQwMjCytYhwqADn/Ief0fxrQr4E88AZF8C\n"
        "Mxl3O/OrrhCyDc0RMblYPONEBdrxnRYFWMreFEQeh9KTiDfpGHuY6aeU26rcsdtQ\n"
        "UQIDAQAB\n"
        "-----END PUBLIC KEY-----\n";

    DWORD derLen = 0;
    CryptStringToBinaryA(pem, 0, CRYPT_STRING_BASE64HEADER, nullptr, &derLen, nullptr, nullptr);
    std::vector<BYTE> der(derLen);
    CryptStringToBinaryA(pem, 0, CRYPT_STRING_BASE64HEADER, der.data(), &derLen, nullptr, nullptr);

    CERT_PUBLIC_KEY_INFO* pInfo = nullptr;
    DWORD infoLen = 0;
    CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, X509_PUBLIC_KEY_INFO, der.data(), derLen, 0, nullptr, nullptr, &infoLen);
    pInfo = (CERT_PUBLIC_KEY_INFO*)malloc(infoLen);
    CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, X509_PUBLIC_KEY_INFO, der.data(), derLen, 0, nullptr, pInfo, &infoLen);

    HCRYPTPROV hProv = 0;
    CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);

    HCRYPTKEY hKey = 0;
    BOOL imported = CryptImportPublicKeyInfo(hProv, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pInfo, &hKey);
    std::cout << "CryptImportPublicKeyInfo: " << (imported ? "SUCCESS" : "FAILED") << " err=" << GetLastError() << std::endl;

    DWORD blobLen = 0;
    CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, nullptr, &blobLen);
    std::vector<BYTE> blob(blobLen);
    CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, blob.data(), &blobLen);

    std::cout << "CSP Public Key Blob (" << blobLen << " bytes):\n";
    for (size_t i = 0; i < blob.size(); ++i) {
        printf("0x%02X, ", blob[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    return 0;
}
