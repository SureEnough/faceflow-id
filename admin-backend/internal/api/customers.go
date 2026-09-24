package api

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"strings"
	"time"

	"admin-backend/internal/faceservice"
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
	PersonType     int8   `json:"person_type" binding:"oneof=0 1"`
	StoreID        uint64 `json:"store_id"`
	Name           string `json:"name" binding:"required"`
	IDCardNo       string `json:"id_card_no"`
	StaffNo        string `json:"staff_no"`
	Department     string `json:"department"`
	Gender         int8   `json:"gender"`
	BirthDate      string `json:"birth_date"`
	Address        string `json:"address"`
	IDPhoto        string `json:"id_photo"`        // base64 jpg/png
	LivePhoto      string `json:"live_photo"`      // base64 jpg/png
	FaceFeatureB64 string `json:"face_feature"`    // base64 512*float32（可空：有照片时后台自动提取）
}

// extractFaceFeature 由 base64 头像调用 face-service 提取人脸特征。
// 未检测到人脸 / 服务不可用 / 图片无效时返回明确错误。
func (s *Server) extractFaceFeature(ctx context.Context, b64 string) ([]byte, error) {
	if strings.TrimSpace(b64) == "" {
		return nil, errors.New("empty photo")
	}
	img, err := base64.StdEncoding.DecodeString(b64)
	if err != nil || len(img) == 0 {
		return nil, errors.New("invalid image base64")
	}
	cli := faceservice.New(s.faceServiceCfg())
	feat, err := cli.Extract(ctx, img, "image/jpeg")
	if err != nil {
		if errors.Is(err, faceservice.ErrNoFace) {
			return nil, errors.New("头像中未检测到人脸，请上传清晰正脸照片")
		}
		return nil, errors.New("人脸识别服务不可用：" + err.Error())
	}
	if len(feat) == 0 {
		return nil, errors.New("人脸识别服务返回空特征")
	}
	return feat, nil
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
	if v := c.Query("store_id"); v != "" {
		if n, err := strconv.ParseUint(v, 10, 64); err == nil && n > 0 {
			q = q.Where("store_id = ?", n)
		}
	}
	if v := c.Query("staff_no"); v != "" {
		q = q.Where("staff_no = ?", v)
	}
	// 姓名按 AES-256-GCM 加密存储（name_enc），无法直接 SQL LIKE 检索。
	// 这里先按其他条件取回候选（id + 密文），解密后在内存中模糊过滤，再按 id 分页。
	// 门店规模（万级以下）可接受；大数据量需引入可搜索加密 / 确定性索引。
	if v := c.Query("name"); v != "" {
		var candidates []struct {
			ID      uint64
			NameEnc []byte
		}
		if err := q.Find(&candidates).Error; err != nil {
			Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
			return
		}
		ids := make([]uint64, 0, len(candidates))
		for _, cd := range candidates {
			plain, err := s.cip.Decrypt(cd.NameEnc)
			if err == nil && strings.Contains(plain, v) {
				ids = append(ids, cd.ID)
			}
		}
		if len(ids) == 0 {
			OK(c, gin.H{"total": 0, "items": []storage.Customer{}})
			return
		}
		q = s.db.Model(&storage.Customer{}).Where("id IN ?", ids)
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
		s.fillCustomerPhotoURL(c.Request.Context(), &rows[i])
	}
	OK(c, gin.H{"total": total, "items": rows})
}

// POST /customers 新增人员档案（顾客 person_type=0 / 内部人员 person_type=1）
// face_feature 可空：上传 id_photo/live_photo 时后台自动调用 face-service 提取特征；
// 照片与特征均为空时允许建档（无识别能力，兼容骨架/手工场景）。
// 顾客录入后同步触发历史来访回查并返回聚合结果。
func (s *Server) createCustomer(c *gin.Context) {
	var req customerReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}

	var (
		feat []byte
		err  error
	)
	if req.FaceFeatureB64 != "" {
		feat, err = decodeFeature(req.FaceFeatureB64)
		if err != nil {
			Fail(c, http.StatusBadRequest, CodeParam, "face_feature invalid: "+err.Error())
			return
		}
	} else if req.IDPhoto != "" || req.LivePhoto != "" {
		// 未提供特征但上传了照片：自动做人脸识别提取特征
		photo := req.IDPhoto
		if photo == "" {
			photo = req.LivePhoto
		}
		feat, err = s.extractFaceFeature(c.Request.Context(), photo)
		if err != nil {
			Fail(c, http.StatusBadRequest, CodeFaceSvc, err.Error())
			return
		}
	}
	// 照片与特征均为空：feat=nil 允许建档（骨架模式）

	if req.PersonType == storage.PersonTypeCustomer && req.IDCardNo == "" {
		Fail(c, http.StatusBadRequest, CodeParam, "id_card_no is required for customer")
		return
	}
	if req.PersonType == storage.PersonTypeStaff && req.StaffNo == "" {
		Fail(c, http.StatusBadRequest, CodeParam, "staff_no is required for staff")
		return
	}

	// 照片转存对象存储（开启时返回对象 key；未开启保留 base64 文本，开发模式）
	ctx := c.Request.Context()
	idPhotoKey := s.saveImage(ctx, req.IDPhoto, "id_photos")
	livePhotoKey := s.saveImage(ctx, req.LivePhoto, "live_photos")

	now := time.Now().Unix()
	cust := storage.Customer{
		PersonType:    req.PersonType,
		StoreID:       req.StoreID,
		StaffNo:       strPtr(req.StaffNo),
		Department:    req.Department,
		Gender:        req.Gender,
		IDPhotoPath:   idPhotoKey,
		LivePhotoPath: livePhotoKey,
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
	if req.IDCardNo != "" {
		if cust.IDCardNoEnc, err = s.cip.Encrypt(req.IDCardNo); err != nil {
			Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
			return
		}
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
	if req.PersonType == storage.PersonTypeCustomer && len(feat) > 0 {
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

// customerUpdateReq 更新档案请求（指针字段区分"未传"与"零值"）
type customerUpdateReq struct {
	Name           string  `json:"name"`
	IDCardNo       string  `json:"id_card_no"`
	Address        string  `json:"address"`
	StaffNo        *string `json:"staff_no"`
	Department     string  `json:"department"`
	Gender         *int8   `json:"gender"`
	BirthDate      string  `json:"birth_date"`
	Status         *int8   `json:"status"`
	StoreID        *uint64 `json:"store_id"`
	FaceFeatureB64 string  `json:"face_feature"`
	IDPhoto        string  `json:"id_photo"` // base64，上传头像时自动提取特征替换主特征
}

// PUT /customers/:id 更新档案
func (s *Server) updateCustomer(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid customer id")
		return
	}
	var req customerUpdateReq
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
	if req.IDCardNo != "" {
		if enc, err := s.cip.Encrypt(req.IDCardNo); err == nil {
			updates["id_card_no_enc"] = enc
		}
	}
	if req.Address != "" {
		if enc, err := s.cip.Encrypt(req.Address); err == nil {
			updates["address_enc"] = enc
		}
	}
	if req.StaffNo != nil {
		updates["staff_no"] = strPtr(*req.StaffNo)
	}
	if req.Department != "" {
		updates["department"] = req.Department
	}
	if req.Gender != nil {
		updates["gender"] = *req.Gender
	}
	if req.BirthDate != "" {
		if t, err := time.Parse("2006-01-02", req.BirthDate); err == nil {
			updates["birth_date"] = t.Unix()
		}
	}
	if req.Status != nil {
		updates["status"] = *req.Status
	}
	if req.StoreID != nil {
		updates["store_id"] = *req.StoreID
	}
	if req.FaceFeatureB64 != "" {
		if feat, err := decodeFeature(req.FaceFeatureB64); err == nil {
			updates["face_feature"] = feat
			updates["version"] = gorm.Expr("version + 1")
		}
	} else if req.IDPhoto != "" {
		// 上传头像：自动提取特征替换主特征
		feat, err := s.extractFaceFeature(c.Request.Context(), req.IDPhoto)
		if err != nil {
			Fail(c, http.StatusBadRequest, CodeFaceSvc, err.Error())
			return
		}
		updates["face_feature"] = feat
		updates["version"] = gorm.Expr("version + 1")
		updates["id_photo_path"] = s.saveImage(c.Request.Context(), req.IDPhoto, "id_photos")
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
	s.audit(c, "update_customer", "customer", int64(id), "")
	auditDone(c)
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
		FaceFeatureB64 string `json:"face_feature"`
		IDPhoto        string `json:"id_photo"` // base64，上传头像时自动提取特征追加
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	var feat []byte
	if req.FaceFeatureB64 != "" {
		feat, err = decodeFeature(req.FaceFeatureB64)
		if err != nil {
			Fail(c, http.StatusBadRequest, CodeParam, "face_feature invalid")
			return
		}
	} else if req.IDPhoto != "" {
		feat, err = s.extractFaceFeature(c.Request.Context(), req.IDPhoto)
		if err != nil {
			Fail(c, http.StatusBadRequest, CodeFaceSvc, err.Error())
			return
		}
		if s.obj != nil {
			// 追加照片也转存对象存储（作为现场补采照片）
			_ = s.db.Model(&storage.Customer{}).Where("id = ?", id).
				Update("live_photo_path", s.saveImage(c.Request.Context(), req.IDPhoto, "live_photos"))
		}
	} else {
		Fail(c, http.StatusBadRequest, CodeParam, "face_feature or id_photo required")
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

// fillCustomerPhotoURL 对象存储开启时，将照片 key 转为可访问 URL；
// 未开启（照片=base64 文本）时保持原样由前端直接展示。
func (s *Server) fillCustomerPhotoURL(ctx context.Context, c *storage.Customer) {
	if s.obj == nil {
		return
	}
	if c.IDPhotoPath != "" {
		c.IDPhotoURL = s.obj.URL(ctx, c.IDPhotoPath)
	}
	if c.LivePhotoPath != "" {
		c.LivePhotoURL = s.obj.URL(ctx, c.LivePhotoPath)
	}
}

// decryptCustomer 解密敏感字段到响应辅助字段
func (s *Server) decryptCustomer(c *storage.Customer) {
	c.Name, _ = s.cip.Decrypt(c.NameEnc)
	c.IDCardNo, _ = s.cip.Decrypt(c.IDCardNoEnc)
	c.Address, _ = s.cip.Decrypt(c.AddressEnc)
	c.NameEnc, c.IDCardNoEnc, c.AddressEnc = nil, nil, nil
}