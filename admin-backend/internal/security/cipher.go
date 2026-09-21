package security

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"io"
)

// Cipher 敏感字段 AES-256-GCM 加解密；key 为空时退化为明文直通（开发模式）
type Cipher struct {
	aead cipher.AEAD
	on   bool
}

func NewCipher(keyHex string) (*Cipher, error) {
	if keyHex == "" {
		return &Cipher{on: false}, nil
	}
	key, err := hex.DecodeString(keyHex)
	if err != nil {
		return nil, errors.New("AES_KEY must be 64 hex chars (32 bytes)")
	}
	if len(key) != 32 {
		return nil, errors.New("AES_KEY must be 32 bytes")
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	aead, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	return &Cipher{aead: aead, on: true}, nil
}

func (c *Cipher) Encrypt(plain string) ([]byte, error) {
	if !c.on || plain == "" {
		return []byte(plain), nil
	}
	nonce := make([]byte, c.aead.NonceSize())
	if _, err := io.ReadFull(rand.Reader, nonce); err != nil {
		return nil, err
	}
	// 输出格式：nonce || ciphertext
	return c.aead.Seal(nonce, nonce, []byte(plain), nil), nil
}

func (c *Cipher) Decrypt(data []byte) (string, error) {
	if !c.on || len(data) == 0 {
		return string(data), nil
	}
	ns := c.aead.NonceSize()
	if len(data) < ns {
		return "", errors.New("ciphertext too short")
	}
	plain, err := c.aead.Open(nil, data[:ns], data[ns:], nil)
	if err != nil {
		return "", err
	}
	return string(plain), nil
}

// Enabled 是否启用真加密
func (c *Cipher) Enabled() bool { return c.on }