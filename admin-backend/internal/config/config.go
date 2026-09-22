package config

import "os"

// Config 应用配置（环境变量覆盖默认值）
type Config struct {
	HTTPAddr     string // 监听地址
	DBDSN        string // 数据库 DSN
	AESKey       string // 敏感字段加密密钥（32 字节 hex；为空则禁用加密）
	DevicePSK    string // 设备注册预共享密钥（骨架默认值）
	HistoryTopK  int    // 历史回查 Top-K
	HistoryThr   float32 // 历史回查相似度阈值（默认 0.40）
	RetentionDay int    // 识别记录留存天数（默认 365）

	JWTSecret    string // JWT 签名密钥
	AdminUser    string // 初始管理员账号
	AdminPassword string // 初始管理员密码

	ObjectRoot      string // 对象存储本地根目录（生产可接 MinIO/S3，见 docs/deployment.md）
	ObjectPublicURL string // 对象公网访问前缀（static.example.com）
}

func Load() Config {
	cfg := Config{
		HTTPAddr:     getenv("HTTP_ADDR", ":8080"),
		DBDSN:        getenv("DB_DSN", "sqlite:./data/admin.db"),
		AESKey:       os.Getenv("AES_KEY"), // 32 字节 hex；为空不加密（开发模式）
		DevicePSK:    getenv("DEVICE_PSK", "dev-psk-change-me"),
		HistoryTopK:  atoi(getenv("HISTORY_TOP_K", "50")),
		HistoryThr:   atof(getenv("HISTORY_THRESHOLD", "0.40")),
		RetentionDay: atoi(getenv("RETENTION_DAYS", "365")),

		JWTSecret:     getenv("JWT_SECRET", "dev-jwt-secret-change-me"),
		AdminUser:     getenv("ADMIN_USER", "admin"),
		AdminPassword: getenv("ADMIN_PASSWORD", "admin123"),

		ObjectRoot:      os.Getenv("OBJECT_ROOT"),
		ObjectPublicURL: os.Getenv("OBJECT_PUBLIC_URL"),
	}
	return cfg
}

func getenv(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func atoi(s string) int {
	n := 0
	for _, c := range s {
		if c < '0' || c > '9' {
			break
		}
		n = n*10 + int(c-'0')
	}
	return n
}

func atof(s string) float32 {
	f := 0.0
	dec := 0.1
	inFrac := false
	for _, c := range s {
		if c == '.' {
			inFrac = true
			continue
		}
		if c < '0' || c > '9' {
			break
		}
		if inFrac {
			f += float64(c-'0') * dec
			dec /= 10
		} else {
			f = f*10 + float64(c-'0')
		}
	}
	return float32(f)
}