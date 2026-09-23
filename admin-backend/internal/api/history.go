package api

import (
	"net/http"
	"strconv"
	"time"

	"admin-backend/internal/search"
	"admin-backend/internal/service"

	"github.com/gin-gonic/gin"
)

type historySearchReq struct {
	FaceFeature string   `json:"face_feature" binding:"required"`
	CustomerID  uint64   `json:"customer_id"`
	Threshold   *float32 `json:"similarity_threshold"`
	TopK        *int     `json:"top_k"`
	Scope       *struct {
		StoreIDs []uint64 `json:"store_ids"`
		StartAt  string   `json:"start_at"`
		EndAt    string   `json:"end_at"`
	} `json:"scope"`
}

// POST /history/search 历史来访回查：1:N 检索 → 聚合来访次数/天数/首次/最近
func (s *Server) historySearch(c *gin.Context) {
	var req historySearchReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	featBytes, err := decodeFeature(req.FaceFeature)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "face_feature invalid")
		return
	}
	feat, err := search.Decode(featBytes)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "face_feature decode failed")
		return
	}

	thr := s.cfg.HistoryThr
	if req.Threshold != nil {
		thr = *req.Threshold
	}
	topK := s.cfg.HistoryTopK
	if req.TopK != nil && *req.TopK > 0 {
		topK = *req.TopK
	}

	svcReq := service.HistorySearchReq{
		FaceFeature: feat,
		CustomerID:  req.CustomerID,
		Threshold:   thr,
		TopK:        topK,
	}
	if req.Scope != nil {
		svcReq.StoreIDs = req.Scope.StoreIDs
		if req.Scope.StartAt != "" {
			if t, err := time.Parse(time.RFC3339, req.Scope.StartAt); err == nil {
				svcReq.StartAt = t.Unix()
			}
		}
		if req.Scope.EndAt != "" {
			if t, err := time.Parse(time.RFC3339, req.Scope.EndAt); err == nil {
				svcReq.EndAt = t.Unix()
			}
		}
	}

	visits, details, err := service.HistorySearch(c.Request.Context(), s.db, svcReq)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	records := make([]gin.H, 0, len(details))
	for _, d := range details {
		rec := gin.H{"log_id": d.LogID, "similarity": d.Similarity}
		if ts, ok := d.Extra["created_at"].(int64); ok && ts > 0 {
			rec["created_at"] = time.Unix(ts, 0).UTC().Format(time.RFC3339)
		}
		if v, ok := d.Extra["device_id"].(uint64); ok {
			rec["device_id"] = v
		}
		if v, ok := d.Extra["camera_id"].(string); ok {
			rec["camera_id"] = v
		}
		if v, ok := d.Extra["direction"].(int8); ok {
			rec["direction"] = v
		}
		// 快照：对象存储 key → 可访问 URL（配了 ObjectPublicURL 时）；
		// 对象存储未开启时原样输出（base64 开发模式）
		if v, ok := d.Extra["snapshot"].(string); ok && v != "" {
			if s.obj != nil {
				if u := s.obj.URL(c.Request.Context(), v); u != "" {
					rec["snapshot_url"] = u
				} else {
					rec["snapshot"] = v // 本地对象存储未配公网前缀，先给 key
				}
			} else {
				rec["snapshot"] = v
			}
		}
		if v, ok := d.Extra["snapshot_mime"].(string); ok && v != "" {
			rec["snapshot_mime"] = v
		}
		records = append(records, rec)
	}

	OK(c, gin.H{
		"total_visits":    visits.TotalVisits,
		"visit_days":      visits.VisitDays,
		"first_visit_at":  formatTime(visits.FirstVisitAt),
		"last_visit_at":   formatTime(visits.LastVisitAt),
		"matched_records": records,
	})
}

func formatTime(ts int64) string {
	if ts <= 0 {
		return ""
	}
	return time.Unix(ts, 0).UTC().Format(time.RFC3339)
}

func parseUintOrZero(s string) uint64 {
	n, _ := strconv.ParseUint(s, 10, 64)
	return n
}