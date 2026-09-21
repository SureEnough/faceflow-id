package storage

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"github.com/glebarez/sqlite"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

// 支持 SQLite（开发/单机）与 MySQL（生产），通过 DSN 前缀自动选择。
// 示例：
//   - sqlite: ./data/admin.db
//   - mysql:  user:pass@tcp(127.0.0.1:3306)/admin?charset=utf8mb4&parseTime=True&loc=Local
func Open(dsn string) (*gorm.DB, error) {
	if dsn == "" {
		return nil, errors.New("db dsn is empty")
	}

	// SQLite 默认驱动为纯 Go 实现（无需 CGO）
	var (
		db  *gorm.DB
		err error
	)
	switch {
	case len(dsn) >= 7 && dsn[:7] == "sqlite:":
		path := dsn[len("sqlite:"):]
		if dir := filepath.Dir(path); dir != "." {
			_ = os.MkdirAll(dir, 0o755)
		}
		db, err = gorm.Open(sqlite.Open(path), &gorm.Config{
			Logger: logger.Default.LogMode(logger.Warn),
		})
	case len(dsn) >= 6 && dsn[:6] == "mysql:":
		// mysql DSN 不含前缀协议，此处要求调用方传入纯 DSN：
		// Open("user:pass@tcp(host:port)/db?charset=utf8mb4&parseTime=True&loc=Local")
		// 如需 MySQL 请引入 gorm.io/driver/mysql 后在此分支打开。
		return nil, fmt.Errorf("mysql driver not linked in this build: %s", dsn)
	default:
		db, err = gorm.Open(sqlite.Open(dsn), &gorm.Config{
			Logger: logger.Default.LogMode(logger.Warn),
		})
	}

	return db, err
}