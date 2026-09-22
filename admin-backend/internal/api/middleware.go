package api

import (
	"net/http"
	"strings"
	"time"

	"admin-backend/internal/auth"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

const (
	ctxUserID   = "user_id"
	ctxUserRole = "user_role"
	ctxDeviceID = "device_id"
)

// authMiddleware 鉴权：验证 JWT 签名/有效期 + 查询令牌记录确认未吊销。
// 注意：令牌必须已在 tokens 表登记（签发时落库）；升级前存量 token 无记录会被拒绝，
// 两端（边缘盒/录入端）均具备 401 自动重登，无需人工干预。
func (s *Server) authMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		h := c.GetHeader("Authorization")
		if !strings.HasPrefix(h, "Bearer ") || len(h) <= 7 {
			Fail(c, 401, CodeUnauth, "missing bearer token")
			c.Abort()
			return
		}
		claims, err := auth.Verify(s.secret(), strings.TrimPrefix(h, "Bearer "))
		if err != nil {
			Fail(c, 401, CodeUnauth, "invalid token: "+err.Error())
			c.Abort()
			return
		}

		// 吊销校验：记录必须存在且未吊销（JWT 无状态，靠台账吊销）
		var rec storage.TokenRecord
		if err := s.db.First(&rec, "jti = ?", claims.Jti).Error; err != nil {
			Fail(c, 401, CodeUnauth, "invalid token: not registered")
			c.Abort()
			return
		}
		if rec.RevokedAt != nil {
			Fail(c, 401, CodeUnauth, "invalid token: revoked")
			c.Abort()
			return
		}

		// 节流更新最近使用时间（≥60s 才写，避免热路径写放大）
		now := time.Now().Unix()
		if rec.LastUsedAt < now-60 {
			s.db.Model(&storage.TokenRecord{}).Where("jti = ?", claims.Jti).Update("last_used_at", now)
		}

		if claims.Dev {
			c.Set(ctxDeviceID, claims.Sub)
		} else {
			c.Set(ctxUserID, claims.Sub)
			c.Set(ctxUserRole, claims.Role)
		}
		c.Next()
	}
}

// requireRole 角色权限控制：仅允许指定角色的用户 token 访问（设备 token 无角色，拒绝）
func requireRole(roles ...string) gin.HandlerFunc {
	return func(c *gin.Context) {
		role := c.GetString(ctxUserRole)
		if role == "" {
			Fail(c, http.StatusForbidden, CodeForbid, "forbidden: user token required")
			c.Abort()
			return
		}
		for _, r := range roles {
			if role == r {
				c.Next()
				return
			}
		}
		Fail(c, http.StatusForbidden, CodeForbid, "forbidden: insufficient role")
		c.Abort()
	}
}
