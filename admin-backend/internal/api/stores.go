package api

import (
	"net/http"
	"strconv"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// GET /stores 门店列表（支持 name / status 筛选）
func (s *Server) listStores(c *gin.Context) {
	q := s.db.Model(&storage.Store{}).Order("id ASC")
	if v := c.Query("name"); v != "" {
		q = q.Where("name LIKE ?", "%"+v+"%")
	}
	if v := c.Query("status"); v == "0" || v == "1" {
		q = q.Where("status = ?", v)
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
	Status  *int8  `json:"status" binding:"omitempty,oneof=0 1"` // 未传=保持默认
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
	if req.Status != nil {
		st.Status = *req.Status
	}
	if err := s.db.Create(&st).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	// 注意：GORM 对带 default 标签的零值字段在 Create 时忽略写入（由 DB 填默认）。
	// 显式传 status=0 时需补一次 update 落库，否则停用门店会被默认成营业中。
	if req.Status != nil && *req.Status != 1 {
		s.db.Model(&storage.Store{}).Where("id = ?", st.ID).Update("status", *req.Status)
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
	if req.Status != nil {
		updates["status"] = *req.Status
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