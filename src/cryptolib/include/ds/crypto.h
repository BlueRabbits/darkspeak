#ifndef CRYPTO_H
#define CRYPTO_H

#include <QByteArray>


namespace ds {
namespace crypto {


// Initializes openssl library. We need one instance in  main().
class Crypto
{
public:
    Crypto();
    ~Crypto();

    static QByteArray getHmacSha256(const QByteArray& key, std::initializer_list<const QByteArray*> data);

};

}} // namespaces

#endif // CRYPTO_H
