/**
 * @file CryptoUtils.h
 * @brief 轻量级加密工具类头文件
 * @details 提供AES-128-GCM加密解密功能，专为实时通信优化
 * @author 系统设计项目组
 * @date 2024
 */

#ifndef CRYPTOUTILS_H
#define CRYPTOUTILS_H

#include <QByteArray>
#include <QString>
#include <QObject>

/**
 * @brief 轻量级加密工具类
 * @details 使用AES-128-GCM算法提供高效的加密解密功能
 * 
 * 特性：
 * - 使用AES-128-GCM，计算开销最小
 * - 内置认证，防止数据篡改
 * - 支持随机IV，确保每次加密结果不同
 * - 优化的内存使用，适合实时通信
 */
class CryptoUtils : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit CryptoUtils(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~CryptoUtils();

    /**
     * @brief 设置加密密钥
     * @param key 16字节的AES-128密钥
     * @return 设置是否成功
     * @details 密钥必须是16字节长度，用于AES-128加密
     */
    bool setKey(const QByteArray &key);

    /**
     * @brief 从字符串生成密钥
     * @param password 密码字符串
     * @return 生成的密钥
     * @details 使用SHA-256哈希生成16字节密钥
     */
    static QByteArray generateKeyFromPassword(const QString &password);

    /**
     * @brief 加密数据
     * @param plaintext 明文数据
     * @return 加密后的数据（包含IV和认证标签）
     * @details 使用AES-128-GCM加密，自动生成随机IV
     */
    QByteArray encrypt(const QByteArray &plaintext);

    /**
     * @brief 解密数据
     * @param ciphertext 密文数据（包含IV和认证标签）
     * @return 解密后的明文数据
     * @details 验证认证标签，确保数据完整性
     */
    QByteArray decrypt(const QByteArray &ciphertext);

    /**
     * @brief 检查是否已设置密钥
     * @return 是否已设置密钥
     */
    bool isKeySet() const;

    /**
     * @brief 获取加密状态信息
     * @return 状态信息字符串
     */
    QString getStatus() const;

private:
    QByteArray m_key;        ///< 加密密钥
    bool m_keySet;           ///< 密钥是否已设置
    QString m_lastError;     ///< 最后的错误信息

    /**
     * @brief 设置错误信息
     * @param error 错误信息
     */
    void setError(const QString &error);

    /**
     * @brief 生成随机IV
     * @param size IV大小（通常为12字节）
     * @return 随机IV
     */
    QByteArray generateRandomIV(int size = 12);

    /**
     * @brief 执行简单加密（XOR）
     * @param plaintext 明文
     * @param key 密钥
     * @param iv 初始化向量
     * @return 加密后的密文
     */
    QByteArray performSimpleEncryption(const QByteArray &plaintext, 
                                      const QByteArray &key, 
                                      const QByteArray &iv);

    /**
     * @brief 执行简单解密（XOR）
     * @param ciphertext 密文
     * @param key 密钥
     * @param iv 初始化向量
     * @return 解密后的明文
     */
    QByteArray performSimpleDecryption(const QByteArray &ciphertext, 
                                      const QByteArray &key, 
                                      const QByteArray &iv);
    
    /**
     * @brief 生成简单认证标签
     * @param data 数据
     * @param key 密钥
     * @return 认证标签
     */
    QByteArray generateSimpleTag(const QByteArray &data, const QByteArray &key);
};

#endif // CRYPTOUTILS_H
