/**
 * @file CryptoUtils.cpp
 * @brief 轻量级加密工具类实现文件
 * @details 使用Qt内置加密功能提供高效的加密解密功能
 * @author 系统设计项目组
 * @date 2024
 */

#include "CryptoUtils.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>
#include <QLoggingCategory>
#include <QDataStream>
#include <QIODevice>
#include <QByteArray>
#include <QString>

Q_LOGGING_CATEGORY(crypto, "crypto")

CryptoUtils::CryptoUtils(QObject *parent)
    : QObject(parent)
    , m_keySet(false)
{
    qCDebug(crypto) << "加密工具初始化完成";
}

CryptoUtils::~CryptoUtils()
{
    qCDebug(crypto) << "加密工具已销毁";
}

bool CryptoUtils::setKey(const QByteArray &key)
{
    if (key.size() != 16) {
        setError("密钥长度必须为16字节（AES-128）");
        return false;
    }
    
    m_key = key;
    m_keySet = true;
    qCDebug(crypto) << "加密密钥已设置，长度:" << key.size() << "字节";
    return true;
}

QByteArray CryptoUtils::generateKeyFromPassword(const QString &password)
{
    // 使用SHA-256生成32字节哈希，然后取前16字节作为AES-128密钥
    QByteArray hash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
    return hash.left(16);
}

QByteArray CryptoUtils::encrypt(const QByteArray &plaintext)
{
    if (!m_keySet) {
        setError("密钥未设置");
        return QByteArray();
    }
    
    if (plaintext.isEmpty()) {
        setError("明文数据为空");
        return QByteArray();
    }
    
    // 生成随机IV（12字节用于GCM模式）
    QByteArray iv = generateRandomIV(12);
    if (iv.isEmpty()) {
        setError("生成随机IV失败");
        return QByteArray();
    }
    
    // 使用简化的XOR加密（实际项目中应使用AES）
    QByteArray ciphertext = performSimpleEncryption(plaintext, m_key, iv);
    if (ciphertext.isEmpty()) {
        setError("加密失败");
        return QByteArray();
    }
    
    // 生成简单的认证标签（实际项目中应使用HMAC）
    QByteArray tag = generateSimpleTag(ciphertext, m_key);
    
    // 组合数据：IV + 认证标签 + 密文
    QByteArray result;
    result.append(iv);
    result.append(tag);
    result.append(ciphertext);
    
    qCDebug(crypto) << "加密成功，原始大小:" << plaintext.size() 
                    << "字节，加密后大小:" << result.size() << "字节";
    
    return result;
}

QByteArray CryptoUtils::decrypt(const QByteArray &ciphertext)
{
    if (!m_keySet) {
        setError("密钥未设置");
        return QByteArray();
    }
    
    if (ciphertext.size() < 28) { // 12字节IV + 16字节标签 + 至少1字节密文
        setError("密文数据太短");
        return QByteArray();
    }
    
    // 分离IV、认证标签和密文
    QByteArray iv = ciphertext.left(12);
    QByteArray tag = ciphertext.mid(12, 16);
    QByteArray encryptedData = ciphertext.mid(28);
    
    // 验证认证标签
    QByteArray expectedTag = generateSimpleTag(encryptedData, m_key);
    if (tag != expectedTag) {
        setError("认证标签验证失败，数据可能被篡改");
        return QByteArray();
    }
    
    // 解密数据
    QByteArray plaintext = performSimpleDecryption(encryptedData, m_key, iv);
    if (plaintext.isEmpty()) {
        setError("解密失败");
        return QByteArray();
    }
    
    qCDebug(crypto) << "解密成功，密文大小:" << ciphertext.size() 
                    << "字节，明文大小:" << plaintext.size() << "字节";
    
    return plaintext;
}

bool CryptoUtils::isKeySet() const
{
    return m_keySet;
}

QString CryptoUtils::getStatus() const
{
    if (!m_keySet) {
        return "加密未初始化";
    }
    return QString("加密已就绪，密钥长度: %1字节").arg(m_key.size());
}

void CryptoUtils::setError(const QString &error)
{
    m_lastError = error;
    qCWarning(crypto) << "加密错误:" << error;
}

QByteArray CryptoUtils::generateRandomIV(int size)
{
    QByteArray iv(size, 0);
    
    // 使用Qt的随机数生成器
    for (int i = 0; i < size; ++i) {
        iv[i] = QRandomGenerator::global()->bounded(256);
    }
    
    return iv;
}

QByteArray CryptoUtils::performSimpleEncryption(const QByteArray &plaintext, 
                                               const QByteArray &key, 
                                               const QByteArray &iv)
{
    QByteArray ciphertext = plaintext;
    
    // 简单的XOR加密（实际项目中应使用AES）
    for (int i = 0; i < ciphertext.size(); ++i) {
        ciphertext[i] = ciphertext[i] ^ key[i % key.size()] ^ iv[i % iv.size()];
    }
    
    return ciphertext;
}

QByteArray CryptoUtils::performSimpleDecryption(const QByteArray &ciphertext, 
                                               const QByteArray &key, 
                                               const QByteArray &iv)
{
    QByteArray plaintext = ciphertext;
    
    // 简单的XOR解密（实际项目中应使用AES）
    for (int i = 0; i < plaintext.size(); ++i) {
        plaintext[i] = plaintext[i] ^ key[i % key.size()] ^ iv[i % iv.size()];
    }
    
    return plaintext;
}

QByteArray CryptoUtils::generateSimpleTag(const QByteArray &data, const QByteArray &key)
{
    // 简单的认证标签生成（实际项目中应使用HMAC）
    QByteArray combined = data + key;
    QByteArray hash = QCryptographicHash::hash(combined, QCryptographicHash::Sha256);
    return hash.left(16); // 取前16字节作为标签
}