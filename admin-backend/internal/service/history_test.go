package service_test

import (
	"context"
	"encoding/binary"
	"math"
	"testing"
	"time"

	"admin-backend/internal/search"
	"admin-backend/internal/service"
	"admin-backend/internal/storage"

	"github.com/glebarez/sqlite"
	"gorm.io/gorm"
)

// featureBytes 特征 → 数据库 blob（little-endian float32）
func featureBytes(t *testing.T, f search.Feature) []byte {
	t.Helper()
	out := make([]byte, len(f)*4)
	for i, v := range f {
		binary.LittleEndian.PutUint32(out[i*4:], math.Float32bits(v))
	}
	return out
}

func newFeature() search.Feature {
	f := make(search.Feature, search.Dim)
	for i := range f {
		f[i] = float32(math.Sin(float64(i))) // 确定性非零特征
	}
	f.NormalizeL2()
	return f
}

func setupDB(t *testing.T) *gorm.DB {
	t.Helper()
	db, err := gorm.Open(sqlite.Open(":memory:"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open memory db: %v", err)
	}
	if err := db.AutoMigrate(&storage.Customer{}, &storage.RecognitionLog{}, &storage.Visits{}); err != nil {
		t.Fatalf("migrate: %v", err)
	}
	return db
}

func insertLog(t *testing.T, db *gorm.DB, track string, feat search.Feature, personType int8, ts int64) {
	t.Helper()
	log := storage.RecognitionLog{
		DeviceID: 1, TrackID: track, PersonType: personType,
		FaceFeature: featureBytes(t, feat), Direction: 0,
		CameraID: "cam-01", CreatedAt: ts,
	}
	if err := db.Create(&log).Error; err != nil {
		t.Fatalf("insert log: %v", err)
	}
}

func TestHistorySearch_MatchesAnonymousCustomerTracks(t *testing.T) {
	db := setupDB(t)
	featA := newFeature()
	// 顾客 A 匿名轨迹：两天各 1 条（person_type=0, customer_id 为空）
	insertLog(t, db, "A-1", featA, storage.PersonTypeCustomer, 1700000000)
	insertLog(t, db, "A-2", featA, storage.PersonTypeCustomer, 1700000000+86400)

	// 内部人员 B 轨迹（不应参与顾客回查）
	featB := newFeature()
	featB[10] = -featB[10] // sin(10)!=0，确保与 featA 不同
	insertLog(t, db, "B-1", featB, storage.PersonTypeStaff, 1700000000+3600)

	stats, _, err := service.HistorySearch(context.Background(), db, service.HistorySearchReq{
		FaceFeature: featA,
		Threshold:   0.4,
		TopK:        50,
	})
	if err != nil {
		t.Fatalf("HistorySearch: %v", err)
	}
	if stats.TotalVisits != 2 {
		t.Fatalf("expected 2 visits, got %d", stats.TotalVisits)
	}
	if stats.VisitDays != 2 {
		t.Fatalf("expected 2 visit days, got %d", stats.VisitDays)
	}
	if stats.FirstVisitAt == 0 || stats.LastVisitAt == 0 || stats.LastVisitAt < stats.FirstVisitAt {
		t.Fatalf("bad first/last: %d/%d", stats.FirstVisitAt, stats.LastVisitAt)
	}
}

func TestHistorySearch_IgnoresStaff(t *testing.T) {
	db := setupDB(t)
	featB := newFeature()
	insertLog(t, db, "B-1", featB, storage.PersonTypeStaff, 1700000000)

	stats, _, err := service.HistorySearch(context.Background(), db, service.HistorySearchReq{
		FaceFeature: featB,
		Threshold:   0.4,
		TopK:        50,
	})
	if err != nil {
		t.Fatalf("HistorySearch: %v", err)
	}
	if stats.TotalVisits != 0 {
		t.Fatalf("staff tracks must be excluded, got %d visits", stats.TotalVisits)
	}
}

func TestHistorySearch_TimeRangeFilter(t *testing.T) {
	db := setupDB(t)
	featA := newFeature()
	insertLog(t, db, "A-1", featA, storage.PersonTypeCustomer, 1700000000)
	insertLog(t, db, "A-2", featA, storage.PersonTypeCustomer, 1700000000+86400)

	stats, _, err := service.HistorySearch(context.Background(), db, service.HistorySearchReq{
		FaceFeature: featA,
		Threshold:   0.4,
		TopK:        50,
		StartAt:     1700000000 + 86400, // 只覆盖第二天的轨迹
		EndAt:       1700000000 + 86400 + 1,
	})
	if err != nil {
		t.Fatalf("HistorySearch: %v", err)
	}
	if stats.TotalVisits != 1 {
		t.Fatalf("expected 1 visit in range, got %d", stats.TotalVisits)
	}
}

func TestHistorySearch_DifferentFeatureMisses(t *testing.T) {
	db := setupDB(t)
	featA := newFeature()
	insertLog(t, db, "A-1", featA, storage.PersonTypeCustomer, 1700000000)

	// 与 featA 正交的另一确定性特征（cos 与 sin 内积近似 0）
	other := make(search.Feature, search.Dim)
	for i := range other {
		other[i] = float32(math.Cos(float64(i)))
	}
	other.NormalizeL2()
	stats, _, err := service.HistorySearch(context.Background(), db, service.HistorySearchReq{
		FaceFeature: other,
		Threshold:   0.99, // 高阈值 → 不命中
		TopK:        50,
	})
	if err != nil {
		t.Fatalf("HistorySearch: %v", err)
	}
	if stats.TotalVisits != 0 {
		t.Fatalf("different feature should miss, got %d", stats.TotalVisits)
	}
}

var _ = time.Now // 保留 time 引用（人数较少的导入内使用）