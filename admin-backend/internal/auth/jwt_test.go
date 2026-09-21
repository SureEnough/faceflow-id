package auth

import (
	"strings"
	"testing"
	"time"
)

func TestSignVerify(t *testing.T) {
	secret := []byte("test-secret")
	token, err := Sign(secret, 1, "admin", false, 3600)
	if err != nil {
		t.Fatalf("Sign: %v", err)
	}
	if strings.Count(token, ".") != 2 {
		t.Fatalf("invalid token format: %s", token)
	}
	claims, err := Verify(secret, token)
	if err != nil {
		t.Fatalf("Verify: %v", err)
	}
	if claims.Sub != 1 || claims.Role != "admin" || claims.Dev {
		t.Fatalf("unexpected claims: %+v", claims)
	}
	if claims.Exp <= time.Now().Unix() {
		t.Fatal("exp should be in future")
	}
}

func TestVerifyWrongSecret(t *testing.T) {
	token, _ := Sign([]byte("secret-a"), 2, "viewer", false, 3600)
	if _, err := Verify([]byte("secret-b"), token); err == nil {
		t.Fatal("expected error with wrong secret")
	}
}

func TestVerifyTamperedToken(t *testing.T) {
	token, _ := Sign([]byte("secret"), 3, "admin", false, 3600)
	parts := strings.Split(token, ".")
	// 篡改 payload 中的 sub
	parts[1] = "eyJzdWIiOjk5OTksInJvbGUiOiJhZG1pbiJ9"
	tampered := strings.Join(parts, ".")
	if _, err := Verify([]byte("secret"), tampered); err == nil {
		t.Fatal("expected error with tampered payload")
	}
}

func TestVerifyExpired(t *testing.T) {
	secret := []byte("secret")
	token, err := Sign(secret, 1, "admin", false, -10) // 已过期
	if err != nil {
		t.Fatalf("Sign: %v", err)
	}
	if _, err := Verify(secret, token); err == nil || !strings.Contains(err.Error(), "expired") {
		t.Fatalf("expected expired error, got %v", err)
	}
}

func TestDeviceTokenClaim(t *testing.T) {
	token, _ := Sign([]byte("secret"), 7, "device", true, 3600)
	claims, err := Verify([]byte("secret"), token)
	if err != nil {
		t.Fatalf("Verify: %v", err)
	}
	if !claims.Dev || claims.Sub != 7 {
		t.Fatalf("expected device token, got %+v", claims)
	}
}

func TestHashVerifyPassword(t *testing.T) {
	hash, err := HashPassword("admin123")
	if err != nil {
		t.Fatalf("HashPassword: %v", err)
	}
	if !VerifyPassword(hash, "admin123") {
		t.Fatal("VerifyPassword should pass for correct password")
	}
	if VerifyPassword(hash, "wrong") {
		t.Fatal("VerifyPassword should fail for wrong password")
	}
	// 相同密码哈希不同（随机盐）
	hash2, _ := HashPassword("admin123")
	if hash == hash2 {
		t.Fatal("salt should make hashes differ")
	}
}