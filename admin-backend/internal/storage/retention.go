package storage

import (
	"time"

	"gorm.io/gorm"
)

// CleanupExpired 清理过期识别/核验记录（created_at 早于 now - retentionDays 天），返回删除条数。
// retentionDays <= 0 表示不清理。
func CleanupExpired(db *gorm.DB, retentionDays int) (int64, error) {
	if retentionDays <= 0 {
		return 0, nil
	}
	cutoff := time.Now().Unix() - int64(retentionDays)*86400
	var total int64
	res := db.Where("created_at < ?", cutoff).Delete(&RecognitionLog{})
	if res.Error != nil {
		return total, res.Error
	}
	total += res.RowsAffected
	res = db.Where("created_at < ?", cutoff).Delete(&VerifyRecord{})
	if res.Error != nil {
		return total, res.Error
	}
	total += res.RowsAffected
	return total, nil
}