// admin-backend/internal/api/auth.go
// 登录与令牌
package api

import (
	"fmt"
	"net/http"
	"time"

	"admin-backend/internal/auth"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type loginReq struct {
	Username string `json:"username" binding:"required"`
	Password string `json:"password" binding:"required"`
}

// POST /auth/login 后台账号登录
func (s *Server) login(c *gin.Context) {
	var req loginReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	var user storage.User
	if err := s.db.Where("username = ? AND status = 1", req.Username).First(&user).Error; err != nil {
		if err == gorm.ErrRecordNotFound {
			Fail(c, http.StatusUnauthorized, CodeUnauth, "用户名或密码错误")
			return
		}
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	if !auth.VerifyPassword(user.PasswordHash, req.Password) {
		Fail(c, http.StatusUnauthorized, CodeUnauth, "用户名或密码错误")
		return
	}

	role := roleName(user.Role)
	token, jti, err := auth.Sign(s.secret(), int64(user.ID), role, false, s.cfg.TokenTTLUserSec)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	s.recordToken(jti, "user", int64(user.ID), role, s.cfg.TokenTTLUserSec)
	s.audit(c, "login", "user", int64(user.ID), "username="+user.Username)
	OK(c, gin.H{
		"token":      token,
		"token_type": "Bearer",
		"expires_in": s.cfg.TokenTTLUserSec,
		"user":       gin.H{"id": user.ID, "username": user.Username, "role": roleName(user.Role)},
	})
}

// recordToken 记录令牌到库（供吊销/管理；失败不阻塞发 token，仅记日志）
func (s *Server) recordToken(jti, kind string, sub int64, role string, ttl int64) {
	now := time.Now().Unix()
	if err := s.db.Create(&storage.TokenRecord{
		Jti: jti, SubjectKind: kind, SubjectID: sub, Role: role,
		IssuedAt: now, ExpiresAt: now + ttl,
	}).Error; err != nil {
		s.logf("record token failed: %v", err)
	}
}

// logf 简易日志（避免引入 logger 依赖）
func (s *Server) logf(format string, args ...any) {
	gin.DefaultWriter.Write([]byte("[api] " + fmt.Sprintf(format, args...) + "\n"))
}

func roleName(role int8) string {
	switch role {
	case 0:
		return "admin"
	case 1:
		return "operator"
	default:
		return "viewer"
	}
}

// EnsureAdmin 启动时确保存在默认管理员（仅当 users 表为空）
func (s *Server) EnsureAdmin() error {
	var cnt int64
	if err := s.db.Model(&storage.User{}).Count(&cnt).Error; err != nil {
		return err
	}
	if cnt > 0 {
		return nil
	}
	hash, err := auth.HashPassword(s.cfg.AdminPassword)
	if err != nil {
		return err
	}
	admin := storage.User{Username: s.cfg.AdminUser, PasswordHash: hash, Role: 0, Status: 1}
	return s.db.Create(&admin).Error
}
