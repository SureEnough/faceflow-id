package api

import (
	"bytes"
	"context"
	"encoding/base64"
	"fmt"
	"net/http"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

type recogRecord struct {
	TrackID      string `json:"track_id" binding:"required"`
	CustomerID   *uint64 `json:"customer_id"`
	PersonType   int8   `json:"person_type"`
	FaceFeature  string `json:"face_feature"`
	Snapshot     string `json:"snapshot"`
	SnapshotMime string `json:"snapshot_mime"`
	Similarity   float32 `json:"similarity"`
	Direction    int8   `json:"direction"`
	CameraID     string `json:"camera_id"`
	CreatedAt    string `json:"created_at" binding:"required"`
}

type batchRecognitionReq struct {
	DeviceID uint64        `json:"device_id" binding:"required"`
	Records  []recogRecord `json:"records" binding:"required,min=1,max=200"`
}

// POST /records/recognition/batch 批量上报识别记录（幂等：device_id+track_id+camera_id+created_at）
func (s *Server) batchRecognition(c *gin.Context) {
	var req batchRecognitionReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}

	// 防越权：设备 token 只能上报自己的数据（用户 token 不受限）
	if devID := c.GetInt64(ctxDeviceID); devID > 0 && devID != int64(req.DeviceID) {
		Fail(c, http.StatusForbidden, CodeForbid, "forbidden: device token cannot report for another device")
		return
	}

	accepted, skipped := 0, 0
	for _, r := range req.Records {
		// 快照：对象存储打开时写入（minio/s3），否则仅保留文本（开发）
		if s.obj != nil && r.Snapshot != "" {
			if b, err := base64.StdEncoding.DecodeString(r.Snapshot); err == nil && len(b) > 0 {
				key := fmt.Sprintf("snapshots/%d/%s.jpg", time.Now().Unix(), r.TrackID)
				ct := r.SnapshotMime
				if ct == "" {
					if len(b) > 2 && b[0] == 'B' && b[1] == 'M' {
						ct = "image/bmp"
					} else {
						ct = "image/jpeg"
					}
				}
				if err := s.obj.Put(context.Background(), key, bytes.NewReader(b), ct); err == nil {
					r.Snapshot = key // 入库存对象 key（前端经 URL(key) 取图）
				}
			}
		}
		ts, err := time.Parse(time.RFC3339, r.CreatedAt)
		if err != nil {
			skipped++
			continue
		}
		createdAt := ts.Unix()
		feat, err := decodeFeature(r.FaceFeature)
		if err != nil {
			feat = nil // 匿名轨迹特征缺失时仍入库（可配置是否拒绝）
		}
		log := storage.RecognitionLog{
			DeviceID:    req.DeviceID,
			TrackID:     r.TrackID,
			CustomerID:  r.CustomerID,
			PersonType:  r.PersonType,
			FaceFeature: feat,
			Snapshot:    r.Snapshot,
			SnapshotMime: r.SnapshotMime,
			Similarity:  r.Similarity,
			Direction:   r.Direction,
			CameraID:    r.CameraID,
			CreatedAt:   createdAt,
		}
		// 幂等：先查后插（唯一索引兜底）
		var cnt int64
		s.db.Model(&storage.RecognitionLog{}).
			Where("device_id = ? AND track_id = ? AND camera_id = ? AND created_at = ?",
				log.DeviceID, log.TrackID, log.CameraID, log.CreatedAt).Count(&cnt)
		if cnt > 0 {
			skipped++
			continue
		}
		if err := s.db.Create(&log).Error; err != nil {
			skipped++
			continue
		}
		accepted++
	}
	OK(c, gin.H{"accepted": accepted, "skipped": skipped})
}

type verifyReq struct {
	CustomerID   uint64  `json:"customer_id" binding:"required"`
	IDCardNo     string  `json:"id_card_no"`
	VerifyResult int8    `json:"verify_result" binding:"min=0,max=2"`
	Similarity   float32 `json:"similarity"`
	Liveness     float32 `json:"liveness_score"`
	LivePhoto    string  `json:"live_photo"`
	DeviceID     uint64  `json:"device_id"`
	Operator     string  `json:"operator"`
}

// POST /records/verify 上报核验记录
func (s *Server) createVerify(c *gin.Context) {
	var req verifyReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	// 防越权：设备 token 只能上报自己的核验记录（用户 token 不受限）
	if devID := c.GetInt64(ctxDeviceID); devID > 0 && devID != int64(req.DeviceID) {
		Fail(c, http.StatusForbidden, CodeForbid, "forbidden: device token cannot report for another device")
		return
	}
	enc, err := s.cip.Encrypt(req.IDCardNo)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	vr := storage.VerifyRecord{
		CustomerID:    req.CustomerID,
		IDCardNoEnc:   enc,
		VerifyResult:  req.VerifyResult,
		Similarity:    req.Similarity,
		LivenessScore: req.Liveness,
		LivePhotoPath: s.saveImage(c.Request.Context(), req.LivePhoto, "verify_photos"),
		DeviceID:      req.DeviceID,
		Operator:      req.Operator,
	}
	if err := s.db.Create(&vr).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	OK(c, gin.H{"verify_record_id": vr.ID})
}