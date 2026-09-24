package api

import (
	"net/http"

	"admin-backend/internal/faceservice"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// faceServiceCfg 组装 face-service 客户端配置：
// 系统配置（SystemConfig 表）优先，未配置时回退默认（env FACE_SERVICE_URL / FACE_SERVICE_KEY）。
func (s *Server) faceServiceCfg() faceservice.Config {
	cfg := faceservice.Config{
		BaseURL: s.cfg.FaceServiceURL,
		APIKey:  s.cfg.FaceServiceKey,
	}
	if v, err := storage.GetSystemConfig(s.db, storage.SysKeyFaceServiceURL); err == nil && v != "" {
		cfg.BaseURL = v
	}
	if v, err := storage.GetSystemConfig(s.db, storage.SysKeyFaceServiceKey); err == nil && v != "" {
		cfg.APIKey = v
	}
	return cfg
}

// GET /system/config 读取全局系统配置（admin）
// 返回：face_service_url（已保存值，可能为空）、face_service_key_set（是否配置密钥）、
// effective（当前实际生效的地址/密钥提示），不返回密钥明文。
func (s *Server) getSystemConfig(c *gin.Context) {
	urlV, _ := storage.GetSystemConfig(s.db, storage.SysKeyFaceServiceURL)
	keyV, _ := storage.GetSystemConfig(s.db, storage.SysKeyFaceServiceKey)

	eff := s.faceServiceCfg()
	keyConfigured := keyV != "" || s.cfg.FaceServiceKey != ""

	OK(c, gin.H{
		"face_service_url":      urlV,
		"face_service_key_set":  keyConfigured,
		"effective_url":         eff.BaseURL,
		"effective_key_used":    eff.APIKey != "",
		"face_service_default":  faceservice.DefaultBaseURL,
	})
}

type systemConfigReq struct {
	FaceServiceURL string `json:"face_service_url"`
	FaceServiceKey string `json:"face_service_key"` // 空=不修改；传 "***" 或省略则保留原值
}

// PUT /system/config 保存全局系统配置（admin）
func (s *Server) updateSystemConfig(c *gin.Context) {
	var req systemConfigReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	// 地址：空串 = 恢复默认 face-service
	if err := storage.SetSystemConfig(s.db, storage.SysKeyFaceServiceURL, req.FaceServiceURL); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	// 密钥：空串视为不修改；"***" 亦不修改；其余写入（AES 不加密系统配置，因为需要明文调用）
	if req.FaceServiceKey != "" && req.FaceServiceKey != "***" {
		if err := storage.SetSystemConfig(s.db, storage.SysKeyFaceServiceKey, req.FaceServiceKey); err != nil {
			Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
			return
		}
	}
	s.audit(c, "update_system_config", "system", 0, "face_service_url="+maskURL(req.FaceServiceURL))
	auditDone(c)
	OK(c, gin.H{"updated": true})
}

// maskURL 地址脱敏（仅审计用，避免完整地址落入日志）
func maskURL(u string) string {
	if u == "" {
		return "(default)"
	}
	if len(u) <= 12 {
		return "***"
	}
	return u[:6] + "***" + u[len(u)-4:]
}