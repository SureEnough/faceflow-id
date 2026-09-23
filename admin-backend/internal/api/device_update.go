package api

import (
	"net/http"
	"strconv"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// updateDeviceReq 编辑设备请求。
// 设备 token 调用时仅允许在线状态字段（status / sub_devices），并自动刷新"最后在线时间"；
// 用户 token（admin/operator）调用时可编辑 name / device_key / status 等基本信息。
type updateDeviceReq struct {
	Name       string           `json:"name"`
	DeviceKey  string           `json:"device_key"`
	Status     *int8            `json:"status"` // nil = 不变
	CPU        *float64         `json:"cpu"`    // 设备上报资源指标（%）
	Mem        *float64         `json:"mem"`
	Disk       *float64         `json:"disk"`
	FPS        *float64         `json:"fps"`
	SubDevices []subDeviceState `json:"sub_devices"`
}

// subDeviceState 子设备在线状态（由父设备代报）
type subDeviceState struct {
	DeviceKey string `json:"device_key"`
	Type      int8   `json:"type" binding:"required,oneof=3 4 5"`
	Online    bool   `json:"online"`
}

// PUT /devices/:id 编辑设备（edge-box 周期调用以更新"最后在线时间/在线状态"）
// 鉴权：
//   - 设备 token：只能更新自己，且只能更新在线状态字段（防越权改其他设备基本信息）；
//   - 用户 token：需 admin/operator，可编辑任意设备的基本信息。
func (s *Server) updateDevice(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "invalid device id")
		return
	}
	var req updateDeviceReq
	if err := c.ShouldBindJSON(&req); err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}

	now := time.Now().Unix()

	// 设备 token：自报在线（更新"最后在线时间"）+ 托管子设备状态
	if devID := c.GetInt64(ctxDeviceID); devID > 0 {
		if devID != int64(id) {
			Fail(c, http.StatusForbidden, CodeForbid, "forbidden: device token cannot update another device")
			return
		}
		status := int8(1)
		if req.Status != nil {
			status = *req.Status
		}
		updates := map[string]any{"status": status, "last_seen_at": now, "updated_at": now}
		if req.CPU != nil { updates["cpu"] = *req.CPU }
		if req.Mem != nil { updates["mem"] = *req.Mem }
		if req.Disk != nil { updates["disk"] = *req.Disk }
		if req.FPS != nil { updates["fps"] = *req.FPS }
		if err := s.db.Model(&storage.Device{}).Where("id = ?", id).Updates(updates).Error; err != nil {
			Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
			return
		}
		s.updateSubDevices(id, req.SubDevices, now)
		s.audit(c, "update_device_online", "device", int64(id), "status="+itoa(int(status)))
		auditDone(c)
		OK(c, gin.H{"device_id": id, "last_seen_at": now, "status": status})
		return
	}

	// 用户 token：需 admin / operator
	role := c.GetString(ctxUserRole)
	if role != "admin" && role != "operator" {
		Fail(c, http.StatusForbidden, CodeForbid, "forbidden: admin/operator required to edit device")
		return
	}
	updates := map[string]any{"updated_at": now}
	if req.Name != "" {
		updates["name"] = req.Name
	}
	if req.DeviceKey != "" {
		updates["device_key"] = req.DeviceKey
	}
	if req.Status != nil {
		updates["status"] = *req.Status
	}
	res := s.db.Model(&storage.Device{}).Where("id = ?", id).Updates(updates)
	if res.Error != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, res.Error.Error())
		return
	}
	if res.RowsAffected == 0 {
		Fail(c, http.StatusNotFound, CodeNotFound, "device not found")
		return
	}
	s.updateSubDevices(id, req.SubDevices, now)
	s.audit(c, "update_device", "device", int64(id), "name="+req.Name+" key="+req.DeviceKey)
	auditDone(c)
	OK(c, gin.H{"device_id": id, "updated_at": now})
}

// updateSubDevices 托管子设备在线状态（由父设备代报）
func (s *Server) updateSubDevices(parentID uint64, subs []subDeviceState, now int64) {
	for _, sd := range subs {
		s.db.Model(&storage.Device{}).
			Where("parent_id = ? AND device_type = ? AND device_key = ?", parentID, sd.Type, sd.DeviceKey).
			Updates(map[string]any{"status": boolToInt8(sd.Online), "last_seen_at": now})
	}
}