package api

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"time"

	"admin-backend/internal/search"
	"admin-backend/internal/service"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

// jsonUnmarshal / jsonRaw 等辅助
func jsonUnmarshal(s string, v any) error { return json.Unmarshal([]byte(s), v) }

// strPtr 字符串指针辅助（空串存 NULL）
func strPtr(s string) *string {
	if s == "" {
		return nil
	}
	return &s
}

// staffHash 内部人员工号哈希（边缘盒仅持有哈希，不下发明文）
func staffHash(staffNo string) string {
	h := sha256.Sum256([]byte(staffNo))
	return hex.EncodeToString(h[:])
}

type customerReq struct {
	PersonType    int8   `json:"person_type" binding:"oneof=0 1"`
	Name          string `json:"name" binding:"required"`
	IDCardNo      string `json:"id_card_no"`
	StaffNo       string `json:"staff_no"`
	Department    string `json:"department"`
	Gender        int8   `json:"gender"`
	BirthDate     string `json:"birth_date"`
	Address       string `json:"address"`
	IDPhoto       string `json:"id_photo"`       // base64 jpg
	LivePhoto     string `json:"live_photo"`     // base64 jpg
	FaceFeatureB64 string `json:"face_feature"` // base64 512*float32
}

// GET /customers 分页查询人员库
func (s *Server) listCustomers(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("page_size", "20"))
	if page < 1 {
		page = 1
	}
	if pageSize < 1 || pageSize > 200 {
		pageSize = 20
	}

	q := s.db.Model(&storage.Customer{})
	if v := c.Query("person_type"); v != "" {
		if n, err := strconv.ParseInt(v, 10, 64); err == nil {
			q = q.Where("person_type = ?", n)
		}
	}
	if v := c.Query("status"); v != "" {
		if n, err := strconv.ParseInt(v, 10, 64); err == nil {
			q = q.Where("status = ?", n)
		}
	}
	if v := c.Query("name"); v != "" {
		q = q.Where("name LIKE ?", "%"+v+"%") // 注意：真实实现需按解密后检索或走服务端解密索引
	}
	if v := c.Query("staff_no"); v != "" {
		q = q.Where("staff_no = ?", v)
	}

	var total int64
	q.Count(&total)
	var rows []storage.Customer
	if err := q.Order("id DESC").Offset((page - 1) * pageSize).Limit(pageSize).Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	for i := range rows {
		s.decryptCustomer(&rows[i])
	}
	OK(c, gin.H{"total": total, "items": rows})
}

// POST /customers 新增人员档案（顾客 person_type=0 / 内部人员 person_type=1）
// 顾客录入后同步触发历史来访回查并返回聚合结果。
func (s *Server) createCustomer(c *gin.Context) {
	var req customerReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}

	feat, err := decodeFeature(req.FaceFeatureB64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "face_feature invalid: "+err.Error())
		return
	}

	if req.PersonType == storage.PersonTypeCustomer && req.IDCardNo == "" {
		Fail(c, http.StatusBadRequest, CodeParam, "id_card_no is required for customer")
		return
	}
	if req.PersonType == storage.PersonTypeStaff && req.StaffNo == "" {
		Fail(c, http.StatusBadRequest, CodeParam, "staff_no is required for staff")
		return
	}

	now := time.Now().Unix()
	cust := storage.Customer{
		PersonType:    req.PersonType,
		StaffNo:       strPtr(req.StaffNo),
		Department:    req.Department,
		Gender:        req.Gender,
		IDPhotoPath:   req.IDPhoto,   // 生产：转存对象存储保存路径
		LivePhotoPath: req.LivePhoto, // 生产：转存对象存储保存路径
		FaceFeature:   feat,
		Status:        storage.PersonStatusNormal,
		Version:       1,
		CreatedBy:     c.GetString(ctxUserID),
		CreatedAt:     now,
		UpdatedAt:     now,
	}
	if req.BirthDate != "" {
		if t, err := time.Parse("2006-01-02", req.BirthDate); err == nil {
			cust.BirthDate = t.Unix() // 存 Unix 秒（当日 00:00Z）
		}
	}
	// 加密敏感字段（姓名/身份证/住址）
	if cust.NameEnc, err = s.cip.Encrypt(req.Name); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	if cust.IDCardNoEnc, err = s.cip.Encrypt(req.IDCardNo); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	if cust.AddressEnc, err = s.cip.Encrypt(req.Address); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	if err := s.db.Create(&cust).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	s.decryptCustomer(&cust)

	// 顾客：录入后同步历史回查
	var history *storage.Visits
	if req.PersonType == storage.PersonTypeCustomer {
		f := make(search.Feature, len(feat))
		if n, err := search.Decode(feat); err == nil {
			copy(f, n)
		}
		h, _, herr := service.HistorySearch(c.Request.Context(), s.db, service.HistorySearchReq{
			FaceFeature: f,
			Threshold:   s.cfg.HistoryThr,
			TopK:        s.cfg.HistoryTopK,
		})
		if herr != nil {
			Fail(c, http.StatusInternalServerError, CodeServer, herr.Error())
			return
		}
		history = h
		cust.History = history
	}

	// history 输出统一为 ISO8601（存储为 Unix 秒）
	resp := gin.H{"customer_id": cust.ID, "version": cust.Version}
	if history != nil {
		resp["history"] = gin.H{
			"total_visits":   history.TotalVisits,
			"visit_days":     history.VisitDays,
			"first_visit_at": formatTime(history.FirstVisitAt),
			"last_visit_at":  formatTime(history.LastVisitAt),
		}
	}
	s.audit(c, "create_customer", "customer", int64(cust.ID), "name="+req.Name+" type="+itoa(int(req.PersonType)))
	auditDone(c)
	OK(c, resp)
}

// PUT /customers/:id 更新档案
func (s *Server) updateCustomer(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid customer id")
		return
	}
	var req customerReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	updates := map[string]any{"updated_at": time.Now().Unix()}
	if req.Name != "" {
		if enc, err := s.cip.Encrypt(req.Name); err == nil {
			updates["name_enc"] = enc
		}
	}
	if req.Department != "" {
		updates["department"] = req.Department
	}
	if req.FaceFeatureB64 != "" {
		if feat, err := decodeFeature(req.FaceFeatureB64); err == nil {
			updates["face_feature"] = feat
			updates["version"] = gorm.Expr("version + 1")
		}
	}
	if err := s.db.Model(&storage.Customer{}).Where("id = ?", id).Updates(updates).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	var cust storage.Customer
	if err := s.db.First(&cust, id).Error; err != nil {
		Fail(c, http.StatusNotFound, CodeNotFound, "customer not found")
		return
	}
	s.decryptCustomer(&cust)
	OK(c, gin.H{"customer_id": cust.ID, "version": cust.Version})
}

// DELETE /customers/:id 软删
func (s *Server) deleteCustomer(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid customer id")
		return
	}
	st := storage.PersonStatusDisabled
	if v := c.Query("staff"); v == "1" {
		st = storage.PersonStatusLeft
	}
	if err := s.db.Model(&storage.Customer{}).Where("id = ?", id).
		Updates(map[string]any{"status": st, "updated_at": time.Now().Unix(), "version": gorm.Expr("version + 1")}).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	s.audit(c, "delete_customer", "customer", int64(id), "status="+itoa(int(st)))
	auditDone(c)
	OK(c, gin.H{"customer_id": id})
}

// POST /customers/:id/features 追加特征（多特征支持）
func (s *Server) appendCustomerFeature(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid customer id")
		return
	}
	var req struct {
		FaceFeatureB64 string `json:"face_feature" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	feat, err := decodeFeature(req.FaceFeatureB64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "face_feature invalid")
		return
	}
	cf := storage.CustomerFeature{CustomerID: id, FaceFeature: feat, Source: 1}
	if err := s.db.Create(&cf).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	// 同时更新主特征版本
	if err := s.db.Model(&storage.Customer{}).Where("id = ?", id).
		Updates(map[string]any{"version": gorm.Expr("version + 1"), "updated_at": time.Now().Unix()}).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	OK(c, gin.H{"feature_id": cf.ID})
}

// GET /customers/features/sync 增量拉取（边缘盒）
func (s *Server) syncFeatures(c *gin.Context) {
	since, _ := strconv.ParseUint(c.DefaultQuery("since_version", "0"), 10, 64)
	var rows []storage.Customer
	if err := s.db.Where("version > ? AND status IN ?", since, []int8{0, 1}).
		Order("version ASC").Limit(5000).Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	var maxVersion uint64 = since
	items := make([]gin.H, 0, len(rows))
	for _, r := range rows {
		if r.Version > maxVersion {
			maxVersion = r.Version
		}
		item := gin.H{
			"customer_id":  r.ID,
			"person_type":  r.PersonType,
			"version":      r.Version,
			"face_feature": base64.StdEncoding.EncodeToString(r.FaceFeature),
			"status":       r.Status,
		}
		if r.PersonType == storage.PersonTypeStaff && r.StaffNo != nil {
			item["staff_no_hash"] = staffHash(*r.StaffNo) // 仅工号哈希，不下发明文
		}
		items = append(items, item)
	}
	OK(c, gin.H{"base_version": since, "new_version": maxVersion, "items": items})
}

// --- 辅助 ---

func decodeFeature(b64 string) ([]byte, error) {
	if b64 == "" {
		return nil, errors.New("empty feature")
	}
	b, err := base64.StdEncoding.DecodeString(b64)
	if err != nil {
		return nil, err
	}
	if len(b) != search.Dim*4 {
		return nil, errors.New("feature length != 512*4 bytes")
	}
	return b, nil
}

// decryptCustomer 解密敏感字段到响应辅助字段
func (s *Server) decryptCustomer(c *storage.Customer) {
	c.Name, _ = s.cip.Decrypt(c.NameEnc)
	c.IDCardNo, _ = s.cip.Decrypt(c.IDCardNoEnc)
	c.Address, _ = s.cip.Decrypt(c.AddressEnc)
	c.NameEnc, c.IDCardNoEnc, c.AddressEnc = nil, nil, nil
}