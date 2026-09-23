package storage

import (
	"testing"
	"time"
)

func TestCleanupExpired(t *testing.T) {
	db, err := Open("sqlite::memory:")
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	if err := AutoMigrate(db); err != nil {
		t.Fatalf("migrate: %v", err)
	}
	deadline := time.Now().Unix() - 400*86400 // 400 天前
	for i := 0; i < 3; i++ {
		if err := db.Create(&RecognitionLog{DeviceID: 1, TrackID: "old", CreatedAt: deadline - int64(i)}).Error; err != nil {
			t.Fatal(err)
		}
		if err := db.Create(&VerifyRecord{CustomerID: 1, CreatedAt: deadline - int64(i)}).Error; err != nil {
			t.Fatal(err)
		}
	}
	fresh := time.Now().Unix()
	if err := db.Create(&RecognitionLog{DeviceID: 1, TrackID: "new", CreatedAt: fresh}).Error; err != nil {
		t.Fatal(err)
	}

	// 保留 365 天：清掉 3 条识别 + 3 条核验，保留新鲜记录
	n, err := CleanupExpired(db, 365)
	if err != nil {
		t.Fatalf("cleanup: %v", err)
	}
	if n != 6 {
		t.Fatalf("expected 6 removed, got %d", n)
	}
	var remain int64
	db.Model(&RecognitionLog{}).Count(&remain)
	if remain != 1 {
		t.Fatalf("fresh record should remain, got %d", remain)
	}

	// retentionDays<=0 不清理
	db.Create(&RecognitionLog{DeviceID: 1, TrackID: "old2", CreatedAt: deadline})
	n, err = CleanupExpired(db, 0)
	if err != nil || n != 0 {
		t.Fatalf("retention<=0 should noop, n=%d err=%v", n, err)
	}
}
