package api

import (
	"net/http"
	"strings"

	"admin-backend/internal/auth"

	"github.com/gin-gonic/gin"
)

const (
	ctxUserID   = "user_id"
	ctxUserRole = "user_role"
	ctxDeviceID = "device_id"
)

func authMiddlewareWithSecret(secret []byte) gin.HandlerFunc {
	return func(c *gin.Context) {
		h := c.GetHeader("Authorization")
		if !strings.HasPrefix(h, "Bearer ") || len(h) <= 7 {
			Fail(c, 401, CodeUnauth, "missing bearer token")
			c.Abort()
			return
		}
		claims, err := auth.Verify(secret, strings.TrimPrefix(h, "Bearer "))
		if err != nil {
			Fail(c, 401, CodeUnauth, "invalid token: "+err.Error())
			c.Abort()
			return
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
