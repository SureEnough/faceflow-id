package api

import (
	"encoding/json"
	"net/http"
	"strconv"
	"strings"
	"time"

	"admin-backend/internal/auth"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

func nowUTC() string { return time.Now().UTC().Format(time.RFC3339) }

type deviceLoginReq struct {
	DeviceID uint64 `json:"device_id" binding:"required"`
	PSK      string `json:"psk" binding:"required"`
}

// POST /auth/device/login 设备登录（重新签发设备 token，dev=true claim）
// 校验：psk == 后台 DEVICE_PSK（与设备注册同约定；生产可改为按设备 psk_hash 校验）
func (s *Server) deviceLogin(c *gin.Context) {
	var req deviceLoginReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	if req.PSK != s.cfg.DevicePSK {
		Fail(c, http.StatusUnauthorized, CodeUnauth, "invalid psk")
		return
	}
	var dev storage.Device
	if err := s.db.First(&dev, req.DeviceID).Error; err != nil {
		Fail(c, http.StatusUnauthorized, CodeUnauth, "device not found")
		return
	}
	if dev.ParentID != nil {
		Fail(c, http.StatusForbidden, CodeParam, "sub-device cannot login")
		return
	}
	// 刷新在线状态
	now := time.Now().Unix()
	if err := s.db.Model(&storage.Device{}).Where("id = ?", dev.ID).
		Updates(map[string]any{"status": 1, "last_heartbeat": now, "updated_at": now}).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	token, err := auth.Sign(s.secret(), int64(dev.ID), "device", true, tokenTTLDevice)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	OK(c, gin.H{"token": token, "token_type": "Bearer", "expires_in": tokenTTLDevice, "device_id": dev.ID})
}


// --- 注册 ---

type registerReq struct {
	DeviceType int8   `json:"device_type" binding:"required,oneof=1 2 3 4 5"`
	ParentID   *uint64 `json:"parent_id"`
	DeviceKey  string `json:"device_key"`
	Name       string `json:"name" binding:"required"`
	StoreID    uint64 `json:"store_id" binding:"required"`
	PSK        string `json:"psk" binding:"required"`
}

// POST /devices/register 设备注册（子设备由父设备代为注册）
func (s *Server) registerDevice(c *gin.Context) {
	var req registerReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	if req.PSK != s.cfg.DevicePSK { // 生产：按设备预分配 psk_hash 校验
		Fail(c, http.StatusUnauthorized, CodeUnauth, "invalid psk")
		return
	}
	// 子设备必须关联父级，且父类型匹配（validParent）
	if req.DeviceType >= storage.DeviceTypeRTSPCam {
		if req.ParentID == nil {
			Fail(c, http.StatusBadRequest, CodeParam, "parent_id is required for sub-device")
			return
		}
		var parent storage.Device
		if err := s.db.First(&parent, *req.ParentID).Error; err != nil {
			Fail(c, http.StatusBadRequest, CodeParam, "parent device not found")
			return
		}
		if !validParent(req.DeviceType, parent.DeviceType) {
			Fail(c, http.StatusBadRequest, CodeParam, "invalid parent device_type for sub-device")
			return
		}
	}

	dev := storage.Device{
		DeviceType: req.DeviceType,
		ParentID:   req.ParentID,
		DeviceKey:  req.DeviceKey,
		Name:       req.Name,
		StoreID:    req.StoreID,
		Status:     1,
	}
	dev.LastHeartbeat = time.Now().Unix()

	// 幂等 upsert：主设备按 (store_id, name)；子设备按 (parent_id, device_type, device_key)
	query := s.db.Where("store_id = ? AND name = ?", dev.StoreID, dev.Name)
	if req.ParentID != nil {
		query = s.db.Where("parent_id = ? AND device_type = ? AND device_key = ?", *req.ParentID, dev.DeviceType, dev.DeviceKey)
	}
	if err := query.FirstOrCreate(&dev).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	// 设备 token：JWT（dev=true claim）
	token, err := auth.Sign(s.secret(), int64(dev.ID), "device", true, tokenTTLDevice)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	OK(c, gin.H{
		"device_id":  dev.ID,
		"token":      token,
		"expires_at": time.Now().Add(24 * time.Hour).UTC().Format(time.RFC3339),
	})
}

// validParent 校验子设备与父设备类型匹配
func validParent(child, parent int8) bool {
	switch child {
	case storage.DeviceTypeRTSPCam:
		return parent == storage.DeviceTypeEdgeBox
	case storage.DeviceTypeUSBCam, storage.DeviceTypeIDCardRead:
		return parent == storage.DeviceTypeEnrollPC
	}
	return false
}

// --- 心跳 ---

type subDeviceState struct {
	DeviceKey string `json:"device_key"`
	Type      int8   `json:"type" binding:"required,oneof=3 4 5"`
	Online    bool   `json:"online"`
}

type heartbeatReq struct {
	Status      int8             `json:"status"`
	CPU         float64          `json:"cpu"`
	Mem         float64          `json:"mem"`
	Disk        float64          `json:"disk"`
	FPS         float64          `json:"fps"`
	SubDevices  []subDeviceState `json:"sub_devices"`
}

// POST /devices/:id/heartbeat 主设备心跳；子设备在线状态由父设备托管
func (s *Server) deviceHeartbeat(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid device id")
		return
	}
	var req heartbeatReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}

	now := time.Now().Unix()
	status := int8(1)
	if req.Status == 0 {
		status = 0
	}
	// 更新主设备在线状态
	if err := s.db.Model(&storage.Device{}).
		Where("id = ?", id).
		Updates(map[string]any{"status": status, "last_heartbeat": now}).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	// 更新子设备状态
	for _, sd := range req.SubDevices {
		s.db.Model(&storage.Device{}).
			Where("parent_id = ? AND device_type = ? AND device_key = ?", id, sd.Type, sd.DeviceKey).
			Updates(map[string]any{"status": boolToInt8(sd.Online), "last_heartbeat": now})
	}
	OK(c, gin.H{"device_id": id, "status": status})
}

// --- 设备树 ---

// GET /devices 设备树查询（含父子层级与在线状态）
func (s *Server) deviceTree(c *gin.Context) {
	q := s.db.Order("store_id ASC, parent_id ASC, device_type ASC")
	if v := c.Query("store_id"); v != "" {
		if n, err := strconv.ParseUint(v, 10, 64); err == nil {
			q = q.Where("store_id = ?", n)
		}
	}
	if v := c.Query("device_type"); v != "" {
		if n, err := strconv.ParseInt(v, 10, 64); err == nil {
			q = q.Where("device_type = ?", n)
		}
	}
	if v := c.Query("online"); v == "1" {
		q = q.Where("status = 1")
	} else if v == "0" {
		q = q.Where("status = 0")
	}

	var devs []*storage.Device
	if err := q.Find(&devs).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	OK(c, gin.H{"items": buildTree(devs)})
}

// buildTree 组装 门店→主设备→子设备 树
func buildTree(devs []*storage.Device) []*storage.Device {
	byID := make(map[uint64]*storage.Device, len(devs))
	for _, d := range devs {
		d.Children = nil
		byID[d.ID] = d
	}
	roots := make([]*storage.Device, 0, len(devs))
	for _, d := range devs {
		if d.ParentID != nil {
			if p, ok := byID[*d.ParentID]; ok {
				p.Children = append(p.Children, d)
				continue
			}
		}
		roots = append(roots, d)
	}
	return roots
}

// --- 配置 ---

// GET /devices/:id/config 拉取主设备配置（原样返回 config_json）
func (s *Server) deviceConfig(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid device id")
		return
	}
	var dev storage.Device
	if err := s.db.First(&dev, id).Error; err != nil {
		if err == gorm.ErrRecordNotFound {
			Fail(c, http.StatusNotFound, CodeNotFound, "device not found")
			return
		}
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	// config_json 为 JSON 字符串；此处直接输出，生产可做结构化校验
	if strings.TrimSpace(dev.ConfigJSON) == "" {
		OK(c, gin.H{"device_id": dev.ID, "config": nil})
		return
	}
	OK(c, gin.H{"device_id": dev.ID, "config": jsonRaw(dev.ConfigJSON)})
}

// PUT /devices/:id/config 下发主设备配置（admin/operator；config 必须为 JSON 对象）
func (s *Server) updateDeviceConfig(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid device id")
		return
	}
	var req struct {
		Config json.RawMessage `json:"config" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	var obj map[string]any
	if err := json.Unmarshal(req.Config, &obj); err != nil || obj == nil {
		Fail(c, http.StatusBadRequest, CodeParam, "config must be a JSON object")
		return
	}
	compact, err := json.Marshal(obj)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	now := time.Now().Unix()
	res := s.db.Model(&storage.Device{}).Where("id = ?", id).
		Updates(map[string]any{"config_json": string(compact), "updated_at": now})
	if res.Error != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, res.Error.Error())
		return
	}
	if res.RowsAffected == 0 {
		Fail(c, http.StatusNotFound, CodeNotFound, "device not found")
		return
	}
	s.audit(c, "update_device_config", "device", int64(id), "config=object")
	auditDone(c)
	OK(c, gin.H{"device_id": id, "updated_at": now})
}

func jsonRaw(s string) any {
	var v any
	_ = jsonUnmarshal(s, &v)
	return v
}

func boolToInt8(b bool) int8 {
	if b {
		return 1
	}
	return 0
}