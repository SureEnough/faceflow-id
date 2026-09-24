package storage

import (
	"time"

	"gorm.io/gorm"
)

// GetSystemConfig 读取系统配置项；不存在时返回空串。
func GetSystemConfig(db *gorm.DB, key string) (string, error) {
	var row SystemConfig
	if err := db.Where("key = ?", key).First(&row).Error; err != nil {
		if err == gorm.ErrRecordNotFound {
			return "", nil
		}
		return "", err
	}
	return row.Value, nil
}

// SetSystemConfig 写入系统配置项（存在则更新，不存在则插入）。
func SetSystemConfig(db *gorm.DB, key, value string) error {
	row := SystemConfig{Key: key, Value: value, UpdatedAt: time.Now().Unix()}
	return db.Save(&row).Error
}