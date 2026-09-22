// admin-backend/internal/api/tokens.go
// 令牌管理：查看已签发令牌、吊销令牌（对应需求：缺少令牌管理）。
// 令牌签发时已落库 tokens 表（见 auth.go / devices.go）；吊销通过置 revoked_at 生效，
// 中间件校验时拒绝已吊销令牌（见 middleware.go）。
package api

import (
	"fmt"
	"net/http"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

const tokenListLimit = 200

// GET /tokens?kind=user|device&status=active|revoked|all
// 权限：admin / operator（只读）
func (s *Server) listTokens(c *gin.Context) {
	kind := c.Query("kind") // user / device / "" = 全部
	status := c.DefaultQuery("status", "active")

	tx := s.db.Model(&storage.TokenRecord{})
	if kind == "user" || kind == "device" {
		tx = tx.Where("subject_kind = ?", kind)
	}
	switch status {
	case "active":
		tx = tx.Where("revoked_at IS NULL")
	case "revoked":
		tx = tx.Where("revoked_at IS NOT NULL")
	}

	var recs []storage.TokenRecord
	if err := tx.Order("issued_at DESC").Limit(tokenListLimit).Find(&recs).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	// 附主体名称（user.username / device.name）便于管理员识别
	names := s.subjectNames(recs)
	items := make([]gin.H, 0, len(recs))
	for _, r := range recs {
		name := names[fmt.Sprintf("%s:%d", r.SubjectKind, r.SubjectID)]
		items = append(items, gin.H{
			"jti":          r.Jti,
			"subject_kind": r.SubjectKind,
			"subject_id":   r.SubjectID,
			"subject_name": name,
			"role":         r.Role,
			"issued_at":    r.IssuedAt,
			"expires_at":   r.ExpiresAt,
			"revoked_at":   r.RevokedAt,
			"revoked_by":   r.RevokedBy,
			"last_used_at": r.LastUsedAt,
		})
	}
	OK(c, gin.H{"tokens": items, "total": len(items)})
}

// POST /tokens/:jti/revoke
// 权限：admin。吊销后该 JWT 立即失效（中间件查台账拒绝）；幂等。
func (s *Server) revokeToken(c *gin.Context) {
	jti := c.Param("jti")
	var rec storage.TokenRecord
	if err := s.db.First(&rec, "jti = ?", jti).Error; err != nil {
		Fail(c, http.StatusNotFound, CodeParam, "token not found")
		return
	}
	if rec.RevokedAt != nil {
		OK(c, gin.H{"revoked": true, "already": true, "jti": jti})
		return
	}

	now := time.Now().Unix()
	actor := c.GetInt64(ctxUserID)
	if err := s.db.Model(&storage.TokenRecord{}).Where("jti = ?", jti).Updates(map[string]any{
		"revoked_at": now,
		"revoked_by": actor,
	}).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	s.audit(c, "revoke_token", "token", 0,
		fmt.Sprintf("jti=%s subject=%s:%d", jti, rec.SubjectKind, rec.SubjectID))
	OK(c, gin.H{"revoked": true, "already": false, "jti": jti})
}

// subjectNames 批量查询主体显示名（user→username，device→name）
func (s *Server) subjectNames(recs []storage.TokenRecord) map[string]string {
	out := map[string]string{}
	var userIDs, devIDs []int64
	for _, r := range recs {
		switch r.SubjectKind {
		case "user":
			userIDs = append(userIDs, r.SubjectID)
		case "device":
			devIDs = append(devIDs, r.SubjectID)
		}
	}
	if len(userIDs) > 0 {
		var users []storage.User
		s.db.Where("id IN ?", userIDs).Find(&users)
		for _, u := range users {
			out[fmt.Sprintf("user:%d", u.ID)] = u.Username
		}
	}
	if len(devIDs) > 0 {
		var devs []storage.Device
		s.db.Where("id IN ?", devIDs).Find(&devs)
		for _, d := range devs {
			out[fmt.Sprintf("device:%d", d.ID)] = d.Name
		}
	}
	return out
}