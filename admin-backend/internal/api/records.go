package api

import (
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

	accepted, skipped := 0, 0
	for _, r := range req.Records {
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
		LivePhotoPath: req.LivePhoto,
		DeviceID:      req.DeviceID,
		Operator:      req.Operator,
	}
	if err := s.db.Create(&vr).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	OK(c, gin.H{"verify_record_id": vr.ID})
}