package api_test

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"admin-backend/internal/api"
	"admin-backend/internal/config"
	"admin-backend/internal/security"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// regEdge 注册一台边缘盒并返回 device_id + token
func regEdge(t *testing.T, base, name string, storeID uint64) (uint64, string) {
	t.Helper()
	out, status := doJSON(t, http.MethodPost, base+"/devices/register", map[string]any{
		"device_type": 1, "name": name, "store_id": storeID, "psk": "test-psk",
	}, "")
	if status != 200 || out.Code != 0 {
		t.Fatalf("register %s: %d %s", name, out.Code, out.Message)
	}
	var d struct {
		DeviceID uint64 `json:"device_id"`
		Token    string `json:"token"`
	}
	if err := json.Unmarshal(out.Data, &d); err != nil || d.DeviceID == 0 || d.Token == "" {
		t.Fatalf("register %s response invalid: %v", name, err)
	}
	return d.DeviceID, d.Token
}

// TestDeviceTokenCannotImpersonate 回归：设备 token 不得伪造其他设备数据
func TestDeviceTokenCannotImpersonate(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	edgeA, tokenA := regEdge(t, base, "边缘盒-A", 1)
	edgeB, _ := regEdge(t, base, "边缘盒-B", 1)
	adminToken := login(t, base, "admin", "admin123")

	// 1. 设备 A 不得以设备 B 身份上报识别记录 → 403
	rec := map[string]any{
		"device_id": edgeB,
		"records": []map[string]any{
			{"track_id": "T-X", "person_type": 0, "face_feature": featureBase64(),
				"direction": 0, "camera_id": "cam-1", "created_at": "2026-09-18T10:00:00Z"},
		},
	}
	if _, status := doJSON(t, http.MethodPost, base+"/records/recognition/batch", rec, tokenA); status != 403 {
		t.Fatalf("device A report as B should 403, got %d", status)
	}

	// 2. 设备 A 不得编辑（刷新在线状态）设备 B → 403
	if _, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeB),
		map[string]any{"status": 1}, tokenA); status != 403 {
		t.Fatalf("device A update B should 403, got %d", status)
	}

	// 3. 设备 A 不得读取设备 B 配置 → 403；读自己配置 → 200
	if _, status := doJSON(t, http.MethodGet, fmt.Sprintf("%s/devices/%d/config", base, edgeB), nil, tokenA); status != 403 {
		t.Fatalf("device A read B config should 403, got %d", status)
	}
	if _, status := doJSON(t, http.MethodGet, fmt.Sprintf("%s/devices/%d/config", base, edgeA), nil, tokenA); status != 200 {
		t.Fatalf("device A read own config should 200, got %d", status)
	}

	// 4. 用户 token（admin）不受限：可代为上报 / 编辑任意设备
	if _, status := doJSON(t, http.MethodPost, base+"/records/recognition/batch", rec, adminToken); status != 200 {
		t.Fatalf("admin report for B should 200, got %d", status)
	}
	if _, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeB),
		map[string]any{"status": 1}, adminToken); status != 200 {
		t.Fatalf("admin update B should 200, got %d", status)
	}
}

// newEncryptedTestServer 带真实 AES 密钥的测试服务（用于验证加密字段搜索）
func newEncryptedTestServer(t *testing.T) (*httptest.Server, func()) {
	t.Helper()
	gin.SetMode(gin.TestMode)
	cfg := config.Load()
	cfg.DBDSN = "sqlite::memory:"
	cfg.DevicePSK = "test-psk"
	cfg.JWTSecret = "test-secret"
	cfg.AdminPassword = "admin123"
	cfg.AESKey = strings.Repeat("ab", 32) // 64 hex chars = 32 bytes

	cip, err := security.NewCipher(cfg.AESKey)
	if err != nil {
		t.Fatalf("cipher: %v", err)
	}
	db, err := storage.Open(cfg.DBDSN)
	if err != nil {
		t.Fatalf("db: %v", err)
	}
	if err := storage.AutoMigrate(db); err != nil {
		t.Fatalf("migrate: %v", err)
	}
	srv := api.NewServer(cfg, db, cip)
	if err := srv.EnsureAdmin(); err != nil {
		t.Fatalf("ensure admin: %v", err)
	}
	ts := httptest.NewServer(srv.Router())
	return ts, ts.Close
}

// TestCustomerNameSearchEncrypted 回归：姓名 AES 加密后按姓名搜索不再 500，且过滤正确
func TestCustomerNameSearchEncrypted(t *testing.T) {
	ts, close := newEncryptedTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"
	token := login(t, base, "admin", "admin123")

	mk := func(personType int8, name, idCard, staffNo string) {
		body := map[string]any{"person_type": personType, "name": name, "face_feature": featureBase64()}
		if personType == 0 {
			body["id_card_no"] = idCard
		}
		if staffNo != "" {
			body["staff_no"] = staffNo
		}
		if _, status := doJSON(t, http.MethodPost, base+"/customers", body, token); status != 200 {
			t.Fatalf("create %s: %d", name, status)
		}
	}
	mk(0, "张三", "110101199001011234", "")
	mk(0, "李四", "110101199202022345", "")

	// 姓名模糊检索 → 仅命中张三
	out, status := doJSON(t, http.MethodGet, base+"/customers?name=%E5%BC%A0", nil, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("search by name should 200, got %d %s", out.Code, out.Message)
	}
	var list struct {
		Total int64                    `json:"total"`
		Items []map[string]interface{} `json:"items"`
	}
	if err := json.Unmarshal(out.Data, &list); err != nil {
		t.Fatal(err)
	}
	if list.Total != 1 || list.Items[0]["name"] != "张三" {
		t.Fatalf("name search mismatch: total=%d items=%+v", list.Total, list.Items)
	}

	// 无命中 → 200 且 total=0
	out, status = doJSON(t, http.MethodGet, base+"/customers?name=%E7%8E%8B", nil, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("no-hit search should 200, got %d %s", out.Code, out.Message)
	}
	if err := json.Unmarshal(out.Data, &list); err != nil {
		t.Fatal(err)
	}
	if list.Total != 0 {
		t.Fatalf("no-hit search total should 0, got %d", list.Total)
	}
}

// TestHistorySearchReturnsSnapshot 回归：历史回查匹配明细携带抓拍快照
func TestHistorySearchReturnsSnapshot(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	edgeID, _ := regEdge(t, base, "边缘盒-快照", 1)
	token := login(t, base, "admin", "admin123")
	feat := featureBase64()
	snap := base64.StdEncoding.EncodeToString([]byte("fake-jpeg-bytes"))

	// 上报一条带快照的识别记录（开发模式 obj=nil → 快照原样文本入库）
	_, status := doJSON(t, http.MethodPost, base+"/records/recognition/batch", map[string]any{
		"device_id": edgeID,
		"records": []map[string]any{
			{"track_id": "T-SNAP", "person_type": 0, "face_feature": feat, "direction": 0,
				"camera_id": "cam-1", "snapshot": snap, "snapshot_mime": "image/jpeg",
				"created_at": "2026-09-18T10:00:00Z"},
		},
	}, token)
	if status != 200 {
		t.Fatalf("batch: %d", status)
	}

	// 手动回查 → 匹配明细应带 snapshot
	out, status := doJSON(t, http.MethodPost, base+"/history/search", map[string]any{
		"face_feature": feat, "similarity_threshold": 0.01,
	}, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("history search: %d %s", out.Code, out.Message)
	}
	var res struct {
		Matched []map[string]interface{} `json:"matched_records"`
	}
	if err := json.Unmarshal(out.Data, &res); err != nil {
		t.Fatal(err)
	}
	if len(res.Matched) == 0 {
		t.Fatal("expected matched record")
	}
	if got, _ := res.Matched[0]["snapshot"].(string); got != snap {
		t.Fatalf("snapshot mismatch: got %q want %q", got, snap)
	}
	if got, _ := res.Matched[0]["snapshot_mime"].(string); got != "image/jpeg" {
		t.Fatalf("snapshot_mime mismatch: %q", got)
	}
}
// TestUpdateDeviceOnline 回归：设备 token 通过编辑设备接口刷新"最后在线时间"
func TestUpdateDeviceOnline(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	edgeID, token := regEdge(t, base, "边缘盒-在线", 1)
	camKey := "cam-01"
	if _, status := doJSON(t, http.MethodPost, base+"/devices/register", map[string]any{
		"device_type": 3, "parent_id": edgeID, "device_key": camKey,
		"name": camKey, "store_id": 1, "psk": "test-psk",
	}, ""); status != 200 {
		t.Fatalf("register cam: %d", status)
	}

	// 1. 设备 token 更新自己：刷新在线 + 子设备上线
	out, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"status": 1, "sub_devices": []map[string]any{
			{"device_key": camKey, "type": 3, "online": true},
		}}, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("device update self: %d %s", out.Code, out.Message)
	}
	var up struct {
		LastSeenAt int64 `json:"last_seen_at"`
		Status        int8  `json:"status"`
	}
	if err := json.Unmarshal(out.Data, &up); err != nil {
		t.Fatal(err)
	}
	if up.LastSeenAt == 0 || up.Status != 1 {
		t.Fatalf("device online not refreshed: %+v", up)
	}

	// 2. 设备树中主设备和子设备均在线
	out, status = doJSON(t, http.MethodGet, base+"/devices", nil, token)
	if status != 200 {
		t.Fatalf("device tree: %d", status)
	}
	var tree struct {
		Items []map[string]interface{} `json:"items"`
	}
	if err := json.Unmarshal(out.Data, &tree); err != nil {
		t.Fatal(err)
	}
	if len(tree.Items) != 1 || tree.Items[0]["status"].(float64) != 1 {
		t.Fatalf("edge not online: %+v", tree.Items)
	}

	// 3. 设备 token 尝试改 name → 应被忽略（仍返回 200，但名称不变）
	_, status = doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"name": "被篡改名"}, token)
	if status != 200 {
		t.Fatalf("device name attempt should 200(ignored), got %d", status)
	}
	out, _ = doJSON(t, http.MethodGet, base+"/devices", nil, token)
	if err := json.Unmarshal(out.Data, &tree); err != nil {
		t.Fatal(err)
	}
	if tree.Items[0]["name"] == "被篡改名" {
		t.Fatal("device token must not change name")
	}

	// 4. admin 可编辑基本信息（改名成功）；viewer 无权限 → 403
	adminToken := login(t, base, "admin", "admin123")
	out, status = doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"name": "边缘盒-改名"}, adminToken)
	if status != 200 || out.Code != 0 {
		t.Fatalf("admin edit name: %d %s", out.Code, out.Message)
	}
	if _, status := doJSON(t, http.MethodPost, base+"/users", map[string]any{
		"username": "viewer-up", "password": "viewer123", "role": 2, "status": 1,
	}, adminToken); status != 200 {
		t.Fatalf("create viewer: %d", status)
	}
	viewerToken := login(t, base, "viewer-up", "viewer123")
	if _, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"name": "x"}, viewerToken); status != http.StatusForbidden {
		t.Fatalf("viewer edit device should 403, got %d", status)
	}
}
