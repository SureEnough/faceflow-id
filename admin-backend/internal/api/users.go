// admin-backend/internal/api/users.go
// 用户管理（仅管理员）：列表/创建/更新/删除 + 当前用户
package api

import (
	"net/http"
	"strconv"

	"admin-backend/internal/auth"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// GET /users 用户列表（admin；支持 username / role / status 筛选）
func (s *Server) listUsers(c *gin.Context) {
	q := s.db.Model(&storage.User{})
	if v := c.Query("username"); v != "" {
		q = q.Where("username LIKE ?", "%"+v+"%")
	}
	if v := c.Query("role"); v != "" {
		if n, err := strconv.ParseInt(v, 10, 64); err == nil {
			q = q.Where("role = ?", n)
		}
	}
	if v := c.Query("status"); v == "0" || v == "1" {
		q = q.Where("status = ?", v)
	}
	var rows []storage.User
	if err := q.Order("id ASC").Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	items := make([]gin.H, 0, len(rows))
	for _, u := range rows {
		items = append(items, gin.H{"id": u.ID, "username": u.Username, "role": roleName(u.Role), "status": u.Status, "created_at": u.CreatedAt})
	}
	OK(c, gin.H{"items": items})
}

type userCreateReq struct {
	Username string `json:"username" binding:"required,min=2,max=32"`
	Password string `json:"password" binding:"required,min=6,max=64"`
	Role     int8   `json:"role" binding:"oneof=0 1 2"`
	Status   *int8  `json:"status" binding:"omitempty,oneof=0 1"` // 未传=启用
}

// POST /users 创建用户（admin）
func (s *Server) createUser(c *gin.Context) {
	var req userCreateReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	hash, err := auth.HashPassword(req.Password)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	status := int8(1)
	if req.Status != nil {
		status = *req.Status
	}
	u := storage.User{Username: req.Username, PasswordHash: hash, Role: req.Role, Status: status}
	if err := s.db.Create(&u).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, "username 已存在或创建失败: "+err.Error())
		return
	}
	// GORM 对带 default 标签的零值字段 Create 时忽略写入（DB 填默认 1）；显式停用需补 update
	if req.Status != nil && *req.Status != 1 {
		s.db.Model(&storage.User{}).Where("id = ?", u.ID).Update("status", *req.Status)
	}
	s.audit(c, "create_user", "user", int64(u.ID), "username="+u.Username+" role="+roleName(u.Role))
	auditDone(c)
	OK(c, gin.H{"id": u.ID, "username": u.Username, "role": roleName(u.Role)})
}

type userUpdateReq struct {
	Password *string `json:"password"`
	Role     *int8   `json:"role"`
	Status   *int8   `json:"status"`
}

// PUT /users/:id 更新用户（admin）
func (s *Server) updateUser(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid user id")
		return
	}
	var req userUpdateReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	if int64(id) == c.GetInt64(ctxUserID) && (req.Role != nil || (req.Status != nil && *req.Status == 0)) {
		Fail(c, http.StatusForbidden, CodeForbid, "不能修改自己的角色或停用自己")
		return
	}
	updates := map[string]any{}
	if req.Password != nil && *req.Password != "" {
		hash, err := auth.HashPassword(*req.Password)
		if err != nil {
			Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
			return
		}
		updates["password_hash"] = hash
	}
	if req.Role != nil {
		updates["role"] = *req.Role
	}
	if req.Status != nil {
		updates["status"] = *req.Status
	}
	if len(updates) == 0 {
		OK(c, gin.H{"id": id})
		return
	}
	if err := s.db.Model(&storage.User{}).Where("id = ?", id).Updates(updates).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	s.audit(c, "update_user", "user", int64(id), "")
	auditDone(c)
	OK(c, gin.H{"id": id})
}

// DELETE /users/:id 删除用户（admin）
func (s *Server) deleteUser(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid user id")
		return
	}
	if int64(id) == c.GetInt64(ctxUserID) {
		Fail(c, http.StatusForbidden, CodeForbid, "不能删除自己")
		return
	}
	if err := s.db.Delete(&storage.User{}, id).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	s.audit(c, "delete_user", "user", int64(id), "")
	auditDone(c)
	OK(c, gin.H{"id": id})
}

// GET /me 当前登录用户信息
func (s *Server) me(c *gin.Context) {
	uid := c.GetInt64(ctxUserID)
	var u storage.User
	if err := s.db.First(&u, uid).Error; err != nil {
		Fail(c, http.StatusUnauthorized, CodeUnauth, "user not found")
		return
	}
	OK(c, gin.H{"id": u.ID, "username": u.Username, "role": roleName(u.Role), "created_at": u.CreatedAt})
}