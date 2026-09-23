package api

import (
	"net/http"
	"strconv"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// GET /stores 门店列表
func (s *Server) listStores(c *gin.Context) {
	q := s.db.Model(&storage.Store{}).Order("id ASC")
	if v := c.Query("name"); v != "" {
		q = q.Where("name LIKE ?", "%"+v+"%")
	}
	var rows []storage.Store
	if err := q.Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	OK(c, gin.H{"items": rows})
}

type storeReq struct {
	Name    string `json:"name" binding:"required,max=64"`
	Address string `json:"address"`
	Status  int8   `json:"status" binding:"oneof=0 1"`
}

// POST /stores 新增门店（admin / operator）
func (s *Server) createStore(c *gin.Context) {
	var req storeReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	now := time.Now().Unix()
	st := storage.Store{Name: req.Name, Address: req.Address, Status: 1, CreatedAt: now, UpdatedAt: now}
	if req.Status == 1 || req.Status == 0 {
		st.Status = req.Status
	}
	if err := s.db.Create(&st).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	s.audit(c, "create_store", "store", int64(st.ID), "name="+st.Name)
	auditDone(c)
	OK(c, gin.H{"store_id": st.ID})
}

// PUT /stores/:id 更新门店（admin / operator）
func (s *Server) updateStore(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid store id")
		return
	}
	var req storeReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	updates := map[string]any{"name": req.Name, "address": req.Address, "updated_at": time.Now().Unix()}
	if req.Status == 0 || req.Status == 1 {
		updates["status"] = req.Status
	}
	res := s.db.Model(&storage.Store{}).Where("id = ?", id).Updates(updates)
	if res.Error != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, res.Error.Error())
		return
	}
	if res.RowsAffected == 0 {
		Fail(c, http.StatusNotFound, CodeNotFound, "store not found")
		return
	}
	s.audit(c, "update_store", "store", int64(id), "name="+req.Name)
	auditDone(c)
	OK(c, gin.H{"store_id": id})
}

// DELETE /stores/:id 删除门店（admin）
// 门店下存在设备时拒绝删除（防孤儿设备）；无设备引用才允许。
func (s *Server) deleteStore(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid store id")
		return
	}
	var cnt int64
	if err := s.db.Model(&storage.Device{}).Where("store_id = ?", id).Count(&cnt).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	if cnt > 0 {
		Fail(c, http.StatusBadRequest, CodeParam, "store has devices, cannot delete")
		return
	}
	res := s.db.Delete(&storage.Store{}, id)
	if res.Error != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, res.Error.Error())
		return
	}
	if res.RowsAffected == 0 {
		Fail(c, http.StatusNotFound, CodeNotFound, "store not found")
		return
	}
	s.audit(c, "delete_store", "store", int64(id), "")
	auditDone(c)
	OK(c, gin.H{"store_id": id})
}