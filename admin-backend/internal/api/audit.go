// admin-backend/internal/api/audit.go
// 审计日志：写操作自动留痕（中间件）+ 关键操作显式留痕（业务调用）+ 查询接口
package api

import (
	"net/http"
	"strconv"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// audit 记录一条操作日志（业务侧显式调用，可带 target）
func (s *Server) audit(c *gin.Context, action, targetType string, targetID int64, detail string) {
	rec := storage.AuditLog{
		UserID:     c.GetInt64(ctxUserID),
		Username:   s.actorName(c),
		Action:     action,
		TargetType: targetType,
		TargetID:   targetID,
		Detail:     detail,
		IP:         c.ClientIP(),
		CreatedAt:  time.Now().Unix(),
	}
	_ = s.db.Create(&rec).Error
}

// actorName 取当前操作者：后台用户（查库取用户名）或设备
func (s *Server) actorName(c *gin.Context) string {
	if uid := c.GetInt64(ctxUserID); uid > 0 {
		var u storage.User
		if err := s.db.First(&u, uid).Error; err == nil {
			return u.Username
		}
		return "user#" + strconv.FormatInt(uid, 10)
	}
	if dev := c.GetInt64(ctxDeviceID); dev > 0 {
		return "device#" + strconv.FormatInt(dev, 10)
	}
	return "anonymous"
}

// auditMiddleware 自动记录所有已鉴权写操作（POST/PUT/DELETE），handler 完成后写入
func (s *Server) auditMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		if c.Request.Method == "GET" {
			c.Next()
			return
		}
		c.Next()
		// 已由 handler 显式 audit 的（带 target），中间件跳过避免重复
		if c.GetBool("__audited") {
			return
		}
		rec := storage.AuditLog{
			UserID:    c.GetInt64(ctxUserID),
			Username:  s.actorName(c),
			Action:    c.Request.Method + " " + c.Request.URL.Path,
			Detail:    c.Request.URL.RawQuery,
			IP:        c.ClientIP(),
			CreatedAt: time.Now().Unix(),
		}
		_ = s.db.Create(&rec).Error
	}
}

// auditDone 在 handler 显式审计后标记（防中间件重复记录）
func auditDone(c *gin.Context) { c.Set("__audited", true) }

// GET /audit-logs 审计日志查询（admin）
func (s *Server) listAuditLogs(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("page_size", "50"))
	if page < 1 {
		page = 1
	}
	if pageSize < 1 || pageSize > 200 {
		pageSize = 50
	}
	q := s.db.Model(&storage.AuditLog{})
	if v := c.Query("username"); v != "" {
		q = q.Where("username LIKE ?", "%"+v+"%")
	}
	if v := c.Query("action"); v != "" {
		q = q.Where("action LIKE ?", "%"+v+"%")
	}
	var total int64
	q.Count(&total)
	var rows []storage.AuditLog
	if err := q.Order("id DESC").Offset((page - 1) * pageSize).Limit(pageSize).Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	items := make([]gin.H, 0, len(rows))
	for _, r := range rows {
		items = append(items, gin.H{
			"id": r.ID, "user_id": r.UserID, "username": r.Username,
			"action": r.Action, "target_type": r.TargetType, "target_id": r.TargetID,
			"detail": r.Detail, "ip": r.IP,
			"created_at": formatTime(r.CreatedAt),
		})
	}
	OK(c, gin.H{"total": total, "items": items})
}