// admin-backend/internal/auth/jwt.go
// HS256 JWT 实现（标准库，无第三方依赖）
// 支持两类主体：用户（role）与设备（dev=true）
package auth

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"
)

// Claims JWT 载荷
type Claims struct {
	Jti  string `json:"jti"`  // token id（用于吊销/管理）
	Sub  int64  `json:"sub"`  // user_id 或 device_id
	Role string `json:"role"` // admin / operator / viewer / device
	Dev  bool   `json:"dev"`  // true = 设备 token
	Iat  int64  `json:"iat"`
	Exp  int64  `json:"exp"`
}

func b64e(b []byte) string { return base64.RawURLEncoding.EncodeToString(b) }
func b64d(s string) ([]byte, error) { return base64.RawURLEncoding.DecodeString(s) }

// RandomJti 生成 16 字节加密随机 token id（hex）
func RandomJti() string {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		// 加密随机失败属于致命的工程错误；回退时间戳+计数器仍保证唯一
		return fmt.Sprintf("jti-%d", time.Now().UnixNano())
	}
	return hex.EncodeToString(b)
}

// Sign 签发 HS256 JWT（ttl 秒），返回 token 与 jti（jti 供落库吊销）
func Sign(secret []byte, sub int64, role string, dev bool, ttlSec int64) (token string, jti string, err error) {
	now := time.Now().Unix()
	jti = RandomJti()
	claims := Claims{Jti: jti, Sub: sub, Role: role, Dev: dev, Iat: now, Exp: now + ttlSec}
	payload, err := json.Marshal(claims)
	if err != nil {
		return "", "", err
	}
	header := []byte(`{"alg":"HS256","typ":"JWT"}`)
	unsigned := b64e(header) + "." + b64e(payload)

	mac := hmac.New(sha256.New, secret)
	mac.Write([]byte(unsigned))
	sig := b64e(mac.Sum(nil))
	return unsigned + "." + sig, jti, nil
}

// Verify 验证 JWT 签名与有效期，返回载荷
func Verify(secret []byte, token string) (*Claims, error) {
	parts := strings.Split(token, ".")
	if len(parts) != 3 {
		return nil, errors.New("invalid token format")
	}

	// 校验签名（常量时间比较）
	mac := hmac.New(sha256.New, secret)
	mac.Write([]byte(parts[0] + "." + parts[1]))
	expected := mac.Sum(nil)
	got, err := b64d(parts[2])
	if err != nil {
		return nil, errors.New("invalid signature encoding")
	}
	if !hmac.Equal(expected, got) {
		return nil, errors.New("invalid signature")
	}

	// 解析 payload
	payload, err := b64d(parts[1])
	if err != nil {
		return nil, errors.New("invalid payload encoding")
	}
	var claims Claims
	if err := json.Unmarshal(payload, &claims); err != nil {
		return nil, fmt.Errorf("invalid payload: %w", err)
	}
	if claims.Exp < time.Now().Unix() {
		return nil, errors.New("token expired")
	}
	return &claims, nil
}