package api

import (
	"net/http"
	"strconv"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// GET /records/recognition 识别记录分页查询（只读；设备 token 不开放）
func (s *Server) listRecognitionRecords(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("page_size", "20"))
	if page < 1 {
		page = 1
	}
	if pageSize < 1 || pageSize > 200 {
		pageSize = 20
	}

	q := s.db.Model(&storage.RecognitionLog{})
	if v := c.Query("device_id"); v != "" {
		if n, err := strconv.ParseUint(v, 10, 64); err == nil {
			q = q.Where("device_id = ?", n)
		}
	}
	if v := c.Query("camera_id"); v != "" {
		q = q.Where("camera_id = ?", v)
	}
	if v := c.Query("direction"); v == "0" || v == "1" {
		q = q.Where("direction = ?", v)
	}
	if v := c.Query("person_type"); v == "0" || v == "1" {
		q = q.Where("person_type = ?", v)
	}
	if v := c.Query("track_id"); v != "" {
		q = q.Where("track_id LIKE ?", "%"+v+"%")
	}
	if v := c.Query("start_at"); v != "" {
		if t, err := time.Parse(time.RFC3339, v); err == nil {
			q = q.Where("created_at >= ?", t.Unix())
		}
	}
	if v := c.Query("end_at"); v != "" {
		if t, err := time.Parse(time.RFC3339, v); err == nil {
			q = q.Where("created_at <= ?", t.Unix())
		}
	}

	var total int64
	q.Count(&total)
	var rows []storage.RecognitionLog
	if err := q.Order("id DESC").Offset((page - 1) * pageSize).Limit(pageSize).Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	// 批量取命中人员名称（customer_id → name 解密）
	names := s.customerNames(rows)

	items := make([]gin.H, 0, len(rows))
	for _, r := range rows {
		item := gin.H{
			"id":            r.ID,
			"device_id":     r.DeviceID,
			"track_id":      r.TrackID,
			"customer_id":   r.CustomerID,
			"customer_name": names[r.ID],
			"person_type":   r.PersonType,
			"similarity":    r.Similarity,
			"direction":     r.Direction,
			"camera_id":     r.CameraID,
			"created_at":    formatTime(r.CreatedAt),
			"snapshot_mime": r.SnapshotMime,
		}
		// 快照：对象存储开启时为对象 key（配公网前缀输出 URL），否则为 base64 文本
		if r.Snapshot != "" {
			if s.obj != nil {
				if u := s.obj.URL(c.Request.Context(), r.Snapshot); u != "" {
					item["snapshot_url"] = u
				}
			}
			item["snapshot"] = r.Snapshot
		}
		items = append(items, item)
	}
	OK(c, gin.H{"total": total, "items": items})
}

// customerNames 批量查询识别记录命中人员名（logID -> 解密姓名）
func (s *Server) customerNames(rows []storage.RecognitionLog) map[uint64]string {
	out := map[uint64]string{}
	ids := make([]uint64, 0, len(rows))
	for i := range rows {
		if rows[i].CustomerID != nil {
			ids = append(ids, *rows[i].CustomerID)
		}
	}
	if len(ids) == 0 {
		return out
	}
	var custs []storage.Customer
	s.db.Where("id IN ?", ids).Find(&custs)
	for i := range custs {
		s.decryptCustomer(&custs[i])
	}
	for i := range rows {
		if rows[i].CustomerID != nil {
			for j := range custs {
				if custs[j].ID == *rows[i].CustomerID {
					out[rows[i].ID] = custs[j].Name
					break
				}
			}
		}
	}
	return out
}

// GET /records/verify 人证核验记录分页查询（只读；设备 token 不开放）
func (s *Server) listVerifyRecords(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("page_size", "20"))
	if page < 1 {
		page = 1
	}
	if pageSize < 1 || pageSize > 200 {
		pageSize = 20
	}

	q := s.db.Model(&storage.VerifyRecord{})
	if v := c.Query("device_id"); v != "" {
		if n, err := strconv.ParseUint(v, 10, 64); err == nil {
			q = q.Where("device_id = ?", n)
		}
	}
	if v := c.Query("verify_result"); v == "0" || v == "1" || v == "2" {
		q = q.Where("verify_result = ?", v)
	}
	if v := c.Query("start_at"); v != "" {
		if t, err := time.Parse(time.RFC3339, v); err == nil {
			q = q.Where("created_at >= ?", t.Unix())
		}
	}
	if v := c.Query("end_at"); v != "" {
		if t, err := time.Parse(time.RFC3339, v); err == nil {
			q = q.Where("created_at <= ?", t.Unix())
		}
	}

	var total int64
	q.Count(&total)
	var rows []storage.VerifyRecord
	if err := q.Order("id DESC").Offset((page - 1) * pageSize).Limit(pageSize).Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	items := make([]gin.H, 0, len(rows))
	for _, r := range rows {
		item := gin.H{
			"id":            r.ID,
			"customer_id":   r.CustomerID,
			"verify_result": r.VerifyResult,
			"similarity":    r.Similarity,
			"liveness_score": r.LivenessScore,
			"device_id":     r.DeviceID,
			"operator":      r.Operator,
			"created_at":    formatTime(r.CreatedAt),
		}
		if r.LivePhotoPath != "" {
			if s.obj != nil {
				if u := s.obj.URL(c.Request.Context(), r.LivePhotoPath); u != "" {
					item["live_photo_url"] = u
				}
			}
			item["live_photo"] = r.LivePhotoPath
		}
		items = append(items, item)
	}
	OK(c, gin.H{"total": total, "items": items})
}