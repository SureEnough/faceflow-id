// admin-backend/internal/auth/password.go
// 密码哈希：SHA-256 + 随机盐（格式 salt:hash，hex）
package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"strings"
)

// HashPassword 生成 salt:hash
func HashPassword(password string) (string, error) {
	salt := make([]byte, 16)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	s := hex.EncodeToString(salt)
	h := sha256.Sum256([]byte(s + ":" + password))
	return s + ":" + hex.EncodeToString(h[:]), nil
}

// VerifyPassword 校验明文密码与存储值
func VerifyPassword(hashed, password string) bool {
	parts := strings.SplitN(hashed, ":", 2)
	if len(parts) != 2 {
		return false
	}
	decoded, err := hex.DecodeString(parts[0])
	if err != nil {
		return false
	}
	salt := hex.EncodeToString(decoded)
	h := sha256.Sum256([]byte(salt + ":" + password))
	return parts[1] == hex.EncodeToString(h[:])
}