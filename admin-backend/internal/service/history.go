package service

import (
	"context"
	"time"

	"admin-backend/internal/search"
	"admin-backend/internal/storage"
	"gorm.io/gorm"
)

// HistorySearchReq 历史来访回查请求（时间范围为 Unix 秒，0=不限制）
type HistorySearchReq struct {
	FaceFeature search.Feature // 现场/证件照特征（已解码）
	CustomerID  uint64         // 可空：已注册则双路检索
	Threshold   float32        // 相似度阈值
	TopK        int            // 最多聚合条数
	StoreIDs    []uint64       // 门店范围，空=全部
	StartAt     int64          // Unix 秒
	EndAt       int64          // Unix 秒
}

// HistorySearch 核心：匿名轨迹 1:N 检索 → 聚合来访统计
// 说明：仅统计顾客（person_type=0，含匿名），内部人员不参与。
func HistorySearch(ctx context.Context, db *gorm.DB, req HistorySearchReq) (*storage.Visits, []HitDetail, error) {
	if len(req.FaceFeature) == 0 {
		return nil, nil, errEmptyFeature
	}
	q := make(search.Feature, len(req.FaceFeature))
	copy(q, req.FaceFeature)
	q.NormalizeL2()

	// 1. 召回候选（person_type=0 + 时间/门店范围）
	query := db.WithContext(ctx).
		Select("id, device_id, track_id, customer_id, face_feature, similarity, direction, camera_id, snapshot, snapshot_mime, created_at").
		Where("person_type = ?", storage.PersonTypeCustomer)
	if req.StartAt > 0 {
		query = query.Where("created_at >= ?", req.StartAt)
	}
	if req.EndAt > 0 {
		query = query.Where("created_at <= ?", req.EndAt)
	}
	if len(req.StoreIDs) > 0 {
		// 通过 devices 表关联门店（性能敏感时改为冗余 store_id 字段）
		query = query.Where("device_id IN (SELECT id FROM devices WHERE store_id IN ?)", req.StoreIDs)
	}

	var logs []storage.RecognitionLog
	if err := query.Order("created_at ASC").Find(&logs).Error; err != nil {
		return nil, nil, err
	}

	// 2. 构建可检索项并线性扫描（超大数据量切换 search.SearchIndex/FAISS）
	items := make([]search.Indexed, 0, len(logs))
	for i := range logs {
		f, err := search.Decode(logs[i].FaceFeature)
		if err != nil {
			continue
		}
		items = append(items, search.Indexed{
			ID:        logs[i].ID,
			Feature:   f,
			Threshold: req.Threshold,
			Extra: map[string]any{
				"created_at":    logs[i].CreatedAt, // Unix 秒
				"device_id":     logs[i].DeviceID,
				"camera_id":     logs[i].CameraID,
				"direction":     logs[i].Direction,
				"snapshot":      logs[i].Snapshot,      // 对象 key 或 base64（对象存储未开启时）
				"snapshot_mime": logs[i].SnapshotMime,  // image/jpeg / image/bmp
			},
		})
	}
	hits := search.LinearSearch(q, items)
	if req.TopK > 0 && len(hits) > req.TopK {
		hits = hits[:req.TopK]
	}

	// 3. 聚合统计（按天去重；同一天多摄像头重复轨迹由 Extra 时间窗口聚类去重）
	visits := &storage.Visits{}
	daySet := make(map[string]struct{})
	details := make([]HitDetail, 0, len(hits))
	for _, h := range hits {
		visits.TotalVisits++
		if created, ok := h.Extra["created_at"].(int64); ok && created > 0 {
			daySet[time.Unix(created, 0).UTC().Format("2006-01-02")] = struct{}{}
			if visits.FirstVisitAt == 0 || created < visits.FirstVisitAt {
				visits.FirstVisitAt = created
			}
			if visits.LastVisitAt == 0 || created > visits.LastVisitAt {
				visits.LastVisitAt = created
			}
		}
		details = append(details, HitDetail{LogID: h.ID, Similarity: h.Sim, Extra: h.Extra})
	}
	visits.VisitDays = int64(len(daySet))

	// 4. 写入/更新 visit_stats 物化表（幂等）
	if visits.TotalVisits > 0 {
		_ = db.WithContext(ctx).Save(visits).Error
	}
	return visits, details, nil
}

// HitDetail 匹配明细
type HitDetail struct {
	LogID      uint64
	Similarity float32
	Extra      map[string]any
}

var errEmptyFeature = &AppError{Code: 40001, Msg: "face_feature is required"}

// AppError 业务错误
type AppError struct {
	Code int
	Msg  string
}

func (e *AppError) Error() string { return e.Msg }