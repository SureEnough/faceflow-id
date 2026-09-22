package storage

import (
	"errors"
	"os"
	"path/filepath"

	"github.com/glebarez/sqlite"
	"gorm.io/driver/mysql"
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
		// MySQL（生产）：DSN 前缀 mysql: 后接标准 MySQL DSN
		// mysql:user:pass@tcp(host:port)/admin?charset=utf8mb4&parseTime=True&loc=Local
		db, err = gorm.Open(mysql.Open(dsn[len("mysql:"):]), &gorm.Config{
			Logger: logger.Default.LogMode(logger.Warn),
		})
	default:
		db, err = gorm.Open(sqlite.Open(dsn), &gorm.Config{
			Logger: logger.Default.LogMode(logger.Warn),
		})
	}

	return db, err
}
